#include <Processors/Transforms/FilterTransform.h>

#include <Interpreters/ExpressionActions.h>
#include <Interpreters/Cache/QueryConditionCache.h>
#include <Columns/ColumnsCommon.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeLowCardinality.h>
#include <Processors/Merges/Algorithms/ReplacingSortedAlgorithm.h>
#include <Storages/MergeTree/MergeTreeData.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_COLUMN_FOR_FILTER;
}

bool FilterTransform::canUseType(const DataTypePtr & filter_type)
{
    return filter_type->onlyNull() || isUInt8(removeLowCardinalityAndNullable(filter_type));
}

Block FilterTransform::transformHeader(
    const Block & header, const ActionsDAG * expression, const String & filter_column_name, bool remove_filter_column)
{
    Block result = expression ? expression->updateHeader(header) : header;

    auto filter_type = result.getByName(filter_column_name).type;
    if (!canUseType(filter_type))
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_COLUMN_FOR_FILTER,
            "Illegal type {} of column {} for filter. Must be UInt8 or Nullable(UInt8).",
            filter_type->getName(), filter_column_name);

    if (remove_filter_column)
        result.erase(filter_column_name);

    return result;
}

FilterTransform::FilterTransform(
    const Block & header_,
    ExpressionActionsPtr expression_,
    String filter_column_name_,
    bool remove_filter_column_,
    bool on_totals_,
    std::shared_ptr<std::atomic<size_t>> rows_filtered_,
    std::optional<size_t> condition_hash_)
    : ISimpleTransform(
            header_,
            transformHeader(header_, expression_ ? &expression_->getActionsDAG() : nullptr, filter_column_name_, remove_filter_column_),
            true)
    , expression(std::move(expression_))
    , filter_column_name(std::move(filter_column_name_))
    , remove_filter_column(remove_filter_column_)
    , on_totals(on_totals_)
    , rows_filtered(rows_filtered_)
    , condition_hash(condition_hash_)
{
    transformed_header = getInputPort().getHeader();
    if (expression)
        expression->execute(transformed_header);
    filter_column_position = transformed_header.getPositionByName(filter_column_name);

    auto & column = transformed_header.getByPosition(filter_column_position).column;
    if (column)
        constant_filter_description = ConstantFilterDescription(*column);

    if (condition_hash.has_value())
        query_condition_cache = Context::getGlobalContextInstance()->getQueryConditionCache();
}

IProcessor::Status FilterTransform::prepare()
{
    if (!on_totals
        && (constant_filter_description.always_false
            /// Optimization for `WHERE column in (empty set)`.
            /// The result will not change after set was created, so we can skip this check.
            /// It is implemented in prepare() stop pipeline before reading from input port.
            || (!are_prepared_sets_initialized && expression && expression->checkColumnIsAlwaysFalse(filter_column_name))))
    {
        input.close();
        output.finish();
        return Status::Finished;
    }

    auto status = ISimpleTransform::prepare();

    /// Until prepared sets are initialized, output port will be unneeded, and prepare will return PortFull.
    if (status != IProcessor::Status::PortFull)
        are_prepared_sets_initialized = true;

    return status;
}


void FilterTransform::removeFilterIfNeed(Columns & columns) const
{
    if (remove_filter_column)
        columns.erase(columns.begin() + filter_column_position);
}

void FilterTransform::transform(Chunk & chunk)
{
    auto chunk_rows_before = chunk.getNumRows();
    doTransform(chunk);
    if (rows_filtered)
        *rows_filtered += chunk_rows_before - chunk.getNumRows();
}

void FilterTransform::doTransform(Chunk & chunk)
{
    size_t num_rows_before_filtration = chunk.getNumRows();
    auto columns = chunk.detachColumns();
    DataTypes types;

    auto write_into_query_condition_cache = [&]()
    {
        if (!query_condition_cache)
            return;

        auto mark_info = chunk.getChunkInfos().get<MarkRangesInfo>();
        if (!mark_info)
            return;

        const auto & data_part = mark_info->getDataPart();
        auto storage_id = data_part->storage.getStorageID();
        query_condition_cache->write(
                storage_id.uuid,
                data_part->name,
                *condition_hash,
                mark_info->getMarkRanges(),
                data_part->index_granularity->getMarksCount());
    };

    {
        Block block = getInputPort().getHeader().cloneWithColumns(columns);
        columns.clear();

        if (expression)
            expression->execute(block, num_rows_before_filtration);

        columns = block.getColumns();
        types = block.getDataTypes();
    }

    if (constant_filter_description.always_true || on_totals)
    {
        removeFilterIfNeed(columns);
        chunk.setColumns(std::move(columns), num_rows_before_filtration);
        return;
    }

    size_t num_columns = columns.size();
    ColumnPtr filter_column = columns[filter_column_position];

    /** It happens that at the stage of analysis of expressions (in sample_block) the columns-constants have not been calculated yet,
        *  and now - are calculated. That is, not all cases are covered by the code above.
        * This happens if the function returns a constant for a non-constant argument.
        * For example, `ignore` function.
        */
    constant_filter_description = ConstantFilterDescription(*filter_column);

    if (constant_filter_description.always_false)
    {
        write_into_query_condition_cache();

        return; /// Will finish at next prepare call
    }

    if (constant_filter_description.always_true)
    {
        removeFilterIfNeed(columns);
        chunk.setColumns(std::move(columns), num_rows_before_filtration);
        return;
    }

    std::unique_ptr<IFilterDescription> filter_description;
    if (filter_column->isSparse())
        filter_description = std::make_unique<SparseFilterDescription>(*filter_column);
    else
        filter_description = std::make_unique<FilterDescription>(*filter_column);

    /** Let's find out how many rows will be in result.
      * To do this, we filter out the first non-constant column
      *  or calculate number of set bytes in the filter.
      */
    size_t first_non_constant_column = num_columns;
    size_t min_size_in_memory = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < num_columns; ++i)
    {
        DataTypePtr type_not_null = removeNullableOrLowCardinalityNullable(types[i]);
        if (i != filter_column_position && !isColumnConst(*columns[i]) && type_not_null->isValueRepresentedByNumber())
        {
            size_t size_in_memory = type_not_null->getSizeOfValueInMemory() + (isNullableOrLowCardinalityNullable(types[i]) ? 1 : 0);
            if (size_in_memory < min_size_in_memory)
            {
                min_size_in_memory = size_in_memory;
                first_non_constant_column = i;
            }
        }
    }
    (void)min_size_in_memory; /// Suppress error of clang-analyzer-deadcode.DeadStores

    size_t num_filtered_rows = 0;
    if (first_non_constant_column != num_columns)
    {
        columns[first_non_constant_column] = filter_description->filter(*columns[first_non_constant_column], -1);
        num_filtered_rows = columns[first_non_constant_column]->size();
    }
    else
        num_filtered_rows = filter_description->countBytesInFilter();

    /// If the current block is completely filtered out, let's move on to the next one.
    if (num_filtered_rows == 0)
    {
        write_into_query_condition_cache();

        /// SimpleTransform will skip it.
        return;
    }

    /// If all the rows pass through the filter.
    if (num_filtered_rows == num_rows_before_filtration)
    {
        /// No need to touch the rest of the columns.
        removeFilterIfNeed(columns);
        chunk.setColumns(std::move(columns), num_rows_before_filtration);
        return;
    }

    /// Filter the rest of the columns.
    for (size_t i = 0; i < num_columns; ++i)
    {
        auto & current_column = columns[i];

        if (i == filter_column_position && remove_filter_column)
            continue;

        if (i == first_non_constant_column)
            continue;

        if (isColumnConst(*current_column))
            current_column = current_column->cut(0, num_filtered_rows);
        else
            current_column = filter_description->filter(*current_column, num_filtered_rows);
    }

    removeFilterIfNeed(columns);
    chunk.setColumns(std::move(columns), num_filtered_rows);
}


}

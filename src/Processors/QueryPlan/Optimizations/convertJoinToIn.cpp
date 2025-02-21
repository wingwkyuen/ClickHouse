#include <Processors/QueryPlan/CreatingSetsStep.h>
#include <Processors/QueryPlan/Optimizations/Optimizations.h>
#include <Processors/QueryPlan/FilterStep.h>
#include <Processors/QueryPlan/JoinStepLogical.h>
#include <Columns/ColumnSet.h>
#include <Columns/ColumnConst.h>
#include <DataTypes/DataTypeSet.h>
#include <Functions/FunctionFactory.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/IJoin.h>
#include <Interpreters/PreparedSets.h>
#include <Interpreters/TableJoin.h>


namespace DB::Setting
{
    extern const SettingsBool transform_null_in;
    extern const SettingsUInt64 use_index_for_in_with_subqueries_max_values;
}

namespace DB::QueryPlanOptimizations
{

size_t tryConvertJoinToIn(QueryPlan::Node * parent_node, QueryPlan::Nodes & nodes, const Optimization::ExtraSettings & /*settings*/)
{
    auto & parent = parent_node->step;
    auto * join = typeid_cast<JoinStepLogical *>(parent.get());
    if (!join)
        return 0;
    auto & join_info = join->getJoinInfo();
    if (join_info.strictness != JoinStrictness::All)
        return 0;
    /// Todo: investigate
    if (join->getJoinSettings().join_use_nulls)
        return 0;

    auto left_input_header = join->getInputHeaders().front();
    auto right_input_header = join->getInputHeaders().back();
    const auto & output_header = join->getOutputHeader();

    bool left = false;
    bool right = false;
    for (const auto & column_with_type_and_name : output_header)
    {
        left |= left_input_header.has(column_with_type_and_name.name);
        right |= right_input_header.has(column_with_type_and_name.name);
    }

    if (left && right)
        return 0;


    ActionsDAG actions(left_input_header.getColumnsWithTypeAndName());

    std::vector<const ActionsDAG::Node *> left_columns = actions.getOutputs();
    const ActionsDAG::Node * in_lhs_arg = left_columns.front();

    auto context = join->getContext();
    const auto & settings = context->getSettingsRef();
    auto future_set = std::make_shared<FutureSetFromSubquery>(
        CityHash_v1_0_2::uint128(), nullptr,
        nullptr, nullptr,
        settings[Setting::transform_null_in],
        PreparedSets::getSizeLimitsForSet(settings),
        settings[Setting::use_index_for_in_with_subqueries_max_values]);

    chassert(future_set->get() == nullptr);
    auto column_set = ColumnSet::create(1, future_set);
    ColumnPtr set_col = std::move(column_set);
    const ActionsDAG::Node * in_rhs_arg = &actions.addColumn({set_col, std::make_shared<DataTypeSet>(), "set column"});

    auto func_in = FunctionFactory::instance().get("in", context);
    auto & in_node = actions.addFunction(func_in, {in_lhs_arg, in_rhs_arg}, "");
    actions.getOutputs().push_back(&in_node);

    auto where_step = std::make_unique<FilterStep>(left_input_header,
        std::move(actions),
        in_node.result_name,
        false);
    where_step->setStepDescription("WHERE");

    /// Replace JoinStepLogical with FilterStep
    parent_node->step = std::move(where_step);
    QueryPlan::Node * right_tree = parent_node->children[1];
    parent_node->children.pop_back();

    // PreparedSets::Subqueries subqueries(1, future_set);
    // auto delayed_creating_sets_step = std::make_unique<DelayedCreatingSetsStep>(
    //     output_header,
    //     std::move(subqueries),
    //     context);
    // delayed_creating_sets_step->setStepDescription("DelayedCreatingSetsStep");

    Headers input_headers{output_header};
    auto creating_sets_step = std::make_unique<CreatingSetsStep>(input_headers);
    creating_sets_step->setStepDescription("Create sets before main query execution");

    // auto creating_set_step = std::make_unique<CreatingSetStep>(
    //     plan->getCurrentHeader(),
    //     set_and_key,
    //     nullptr,
    //     SizeLimits(settings[Setting::max_rows_to_transfer],
    //                settings[Setting::max_bytes_to_transfer],
    //                settings[Setting::transfer_overflow_mode]),
    //     context);
    // creating_set_step->setStepDescription("Create set for subquery");
    auto creating_set_step = future_set->build(right_input_header, context);

    /// CreatingSetsStep should be the root, so use swap
    auto & last_node = nodes.back();
    auto & new_node = nodes.emplace_back();
    std::swap(last_node, new_node);
    // last_node.step = std::move(delayed_creating_sets_step);
    last_node.step = std::move(creating_sets_step);
    last_node.children.push_back(&new_node);

    auto & creating_set_node = nodes.emplace_back();
    creating_set_node.step = std::move(creating_set_step);
    creating_set_node.children.push_back(right_tree);
    last_node.children.push_back(&creating_set_node);

    return 1;
}

}

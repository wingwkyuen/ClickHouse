#pragma once
#include <Processors/ISimpleTransform.h>

namespace DB
{

struct LazilyReadInfo;
using LazilyReadInfoPtr = std::shared_ptr<LazilyReadInfo>;

class MergeTreeLazilyReader;
using MergeTreeLazilyReaderPtr = std::unique_ptr<MergeTreeLazilyReader>;

class ColumnLazyTransform : public ISimpleTransform
{
public:
    explicit ColumnLazyTransform(
        const Block & header_, const LazilyReadInfoPtr & lazily_read_info_, MergeTreeLazilyReaderPtr lazy_column_reader_);

    static Block transformHeader(Block header);

    String getName() const override { return "ColumnLazyTransform"; }

protected:
    void transform(Chunk & chunk) override;

private:
    LazilyReadInfoPtr lazily_read_info;
    MergeTreeLazilyReaderPtr lazy_column_reader;
};

}

#pragma once

#include <Common/CacheBase.h>
#include <Common/HashTable/Hash.h>
#include <Common/thread_local_rng.h>
#include <IO/BufferWithOwnMemory.h>

/// "Userspace page cache"
/// A cache for contents of remote files.
/// Intended mainly for caching data retrieved from distributed cache or web disks.
/// Probably not useful when reading local files or when using file cache, the OS page cache works
/// well in those cases.
///
/// Similar to the OS page cache, we want this cache to use most of the available memory.
/// To that end, the cache size is periodically adjusted from background thread (MemoryWorker) based
/// on current memory usage.

namespace DB
{

/// Identifies a chunk of a file or object.
/// We assume that contents of such file/object don't change (without file_version changing), so
/// cache invalidation is not needed.
struct PageCacheKey
{
    /// Path, usually prefixed with storage system name and anything else needed to make it unique.
    /// E.g. "s3:<bucket>/<path>"
    std::string path;
    /// Optional string with ETag, or file modification time, or anything else.
    std::string file_version {};

    /// Byte range in the file: [offset, offset + size).
    ///
    /// Note: for simplicity, PageCache doesn't do any interval-based lookup to handle partially
    /// overlapping ranges.
    /// E.g. if someone puts range [0, 100] to the cache, then someone else does getOrSet for
    /// range [0, 50], it'll be a cache miss, and the cache will end up with two ranges:
    /// [0, 100] and [0, 50].
    /// This is ok for correctness, but would be bad for performance if happens often.
    /// In practice this limitation causes no trouble as all users of page cache use aligned blocks
    /// of fixed size anyway (server setting page_cache_block_size).
    size_t offset = 0;
    size_t size = 0;

    UInt128 hash() const;
    std::string toString() const;
};

class PageCacheCell
{
public:
    PageCacheKey key;

    size_t size() const { return m_size; }
    const char * data() const { return m_data; }
    char * data() { return m_data; }

    ~PageCacheCell();
    PageCacheCell(const PageCacheCell &) = delete;
    PageCacheCell & operator=(const PageCacheCell &) = delete;

    PageCacheCell(PageCacheKey key_, bool temporary);

private:
    size_t m_size = 0;
    char * m_data = nullptr;
    bool m_temporary = false;
};

struct PageCacheWeightFunction
{
    size_t operator()(const PageCacheCell & x) const
    {
        return x.size();
    }
};

extern template class CacheBase<UInt128, PageCacheCell, UInt128TrivialHash, PageCacheWeightFunction>;

/// The key is hash of PageCacheKey.
/// Private inheritance because we have to add MemoryTrackerBlockerInThread to all operations that
/// lock the mutex and allocate memory, to avoid deadlocking if MemoryTracker calls autoResize().
class PageCache : private CacheBase<UInt128, PageCacheCell, UInt128TrivialHash, PageCacheWeightFunction>
{
public:
    using Base = CacheBase<UInt128, PageCacheCell, UInt128TrivialHash, PageCacheWeightFunction>;
    using Key = typename Base::Key;
    using Mapped = typename Base::Mapped;
    using MappedPtr = typename Base::MappedPtr;

    PageCache(size_t default_block_size_, size_t default_lookahead_blocks_, std::chrono::milliseconds history_window_, const String & cache_policy, double size_ratio, size_t min_size_in_bytes_, size_t max_size_in_bytes_, double free_memory_ratio_);

    /// Get or insert a chunk for the given key.
    ///
    /// If detached_if_missing = true, and the key is not present in the cache, the returned chunk
    /// will be just a standalone PageCacheCell not connected to the cache.
    MappedPtr getOrSet(const PageCacheKey & key, bool detached_if_missing, bool inject_eviction, std::function<void(const MappedPtr &)> load);

    bool contains(const PageCacheKey & key, bool inject_eviction) const;

    void autoResize(size_t memory_usage, size_t memory_limit);

    size_t defaultBlockSize() const { return default_block_size; }
    size_t defaultLookaheadBlocks() const { return default_lookahead_blocks; }

    void clear();
    size_t sizeInBytes() const;
    size_t count() const;
    size_t maxSizeInBytes() const;

private:
    size_t default_block_size;
    size_t default_lookahead_blocks;

    /// Cache size is automatically adjusted by background thread, within this range,
    /// targeting cache size (total_memory_limit * (1 - free_memory_ratio) - memory_used_excluding_cache).
    size_t min_size_in_bytes = 0;
    size_t max_size_in_bytes = 0;
    double free_memory_ratio = 1.;

    /// To avoid overreacting to brief drops in memory usage, we use peak memory usage over the last
    /// `history_window` milliseconds. It's calculated using this "sliding" (leapfrogging?) window.
    /// If history_window <= 0, there's no window and we just use current memory usage.
    std::chrono::milliseconds history_window;
    std::array<size_t, 2> peak_memory_buckets {0, 0};
    int64_t cur_bucket = 0;

    void onRemoveOverflowWeightLoss(size_t /*weight_loss*/) override;
};

using PageCachePtr = std::shared_ptr<PageCache>;

}

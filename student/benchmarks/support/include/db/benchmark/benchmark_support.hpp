#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/storage/disk_manager.hpp"

namespace db::benchmark_support {

inline constexpr std::uint64_t kWorkloadSeed = 0xF377'B00F'2026'0817ULL;

class DeterministicRandom final {
public:
    explicit DeterministicRandom(std::uint64_t seed = kWorkloadSeed) noexcept;

    auto next_u64() noexcept -> std::uint64_t;
    auto uniform(std::size_t upper_exclusive) noexcept -> std::size_t;

private:
    std::uint64_t state_;
};

class BenchmarkDirectory final {
public:
    static auto create(std::string_view label) -> base::Result<BenchmarkDirectory>;

    BenchmarkDirectory(const BenchmarkDirectory&) = delete;
    auto operator=(const BenchmarkDirectory&) -> BenchmarkDirectory& = delete;
    BenchmarkDirectory(BenchmarkDirectory&& other) noexcept;
    auto operator=(BenchmarkDirectory&& other) noexcept -> BenchmarkDirectory&;
    ~BenchmarkDirectory();

    auto path() const noexcept -> const std::filesystem::path&;
    auto join(std::string_view child) const -> std::filesystem::path;

private:
    explicit BenchmarkDirectory(std::filesystem::path path) noexcept;
    void cleanup() noexcept;

    std::filesystem::path path_;
};

struct DiskCounters final {
    std::uint64_t reads{0U};
    std::uint64_t writes{0U};
    std::uint64_t allocations{0U};
    std::uint64_t syncs{0U};

    auto operator==(const DiskCounters&) const noexcept -> bool = default;
};

// Benchmark-only deterministic disk. Copying a 4 KiB page deliberately keeps
// cache-miss work realistic while avoiding filesystem and kernel-cache noise.
class CountingMemoryDisk final : public storage::DiskManager {
public:
    CountingMemoryDisk() = default;

    auto read_page(storage::PageAddress address) -> base::Result<storage::Page> override;
    auto write_page(storage::PageAddress address, const storage::Page& page)
        -> base::Status override;
    auto allocate_page(
        base::RelationId relation_id,
        storage::PageKind kind,
        std::size_t special_size = 0U
    ) -> base::Result<storage::PageAddress> override;
    auto page_count(base::RelationId relation_id) -> base::Result<std::uint64_t> override;
    auto sync_relation(base::RelationId relation_id) -> base::Status override;
    auto sync_all() -> base::Status override;
    auto remove_relation(base::RelationId relation_id, bool missing_ok = false)
        -> base::Status override;

    void reset_counters() noexcept;
    auto counters() const noexcept -> DiskCounters;

private:
    struct Relation final {
        std::vector<storage::Page::Storage> pages;
    };

    mutable std::shared_mutex mutex_;
    std::map<base::RelationId, Relation> relations_;
    std::atomic<std::uint64_t> reads_{0U};
    std::atomic<std::uint64_t> writes_{0U};
    std::atomic<std::uint64_t> allocations_{0U};
    std::atomic<std::uint64_t> syncs_{0U};
};

}  // namespace db::benchmark_support

#include "db/benchmark/benchmark_support.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "db/base/error.hpp"

namespace db::benchmark_support {
namespace {

auto next_directory_sequence() noexcept -> std::uint64_t {
    static std::atomic<std::uint64_t> sequence{0U};
    return sequence.fetch_add(1U, std::memory_order_relaxed);
}

auto sanitized_label(const std::string_view label) -> std::string {
    std::string sanitized;
    sanitized.reserve(label.size());
    for (const auto character : label) {
        const auto byte = static_cast<unsigned char>(character);
        sanitized.push_back(std::isalnum(byte) != 0 ? character : '-');
    }
    return sanitized.empty() ? "benchmark" : sanitized;
}

}  // namespace

DeterministicRandom::DeterministicRandom(const std::uint64_t seed) noexcept
    : state_{seed} {}

auto DeterministicRandom::next_u64() noexcept -> std::uint64_t {
    state_ += 0x9E37'79B9'7F4A'7C15ULL;
    auto value = state_;
    value = (value ^ (value >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    return value ^ (value >> 31U);
}

auto DeterministicRandom::uniform(const std::size_t upper_exclusive) noexcept -> std::size_t {
    return upper_exclusive == 0U ? 0U : next_u64() % upper_exclusive;
}

BenchmarkDirectory::BenchmarkDirectory(std::filesystem::path path) noexcept
    : path_{std::move(path)} {}

auto BenchmarkDirectory::create(const std::string_view label) -> base::Result<BenchmarkDirectory> {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error);
    if (error) {
        return base::fail(base::Error{base::ErrorCode::kIo, "cannot resolve temporary directory"});
    }
    const auto sequence = next_directory_sequence();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = root
                / ("db-bench-" + sanitized_label(label) + "-"
                   + std::to_string(static_cast<std::uint64_t>(::getpid())) + "-"
                   + std::to_string(tick) + "-" + std::to_string(sequence));
    if (!std::filesystem::create_directories(path, error) || error) {
        return base::fail(base::Error{base::ErrorCode::kIo, "cannot create benchmark directory"});
    }
    return BenchmarkDirectory{std::move(path)};
}

BenchmarkDirectory::BenchmarkDirectory(BenchmarkDirectory&& other) noexcept
    : path_{std::move(other.path_)} {
    other.path_.clear();
}

auto BenchmarkDirectory::operator=(BenchmarkDirectory&& other) noexcept -> BenchmarkDirectory& {
    if (this != &other) {
        cleanup();
        path_ = std::move(other.path_);
        other.path_.clear();
    }
    return *this;
}

BenchmarkDirectory::~BenchmarkDirectory() {
    cleanup();
}

auto BenchmarkDirectory::path() const noexcept -> const std::filesystem::path& {
    return path_;
}

auto BenchmarkDirectory::join(const std::string_view child) const -> std::filesystem::path {
    return path_ / child;
}

void BenchmarkDirectory::cleanup() noexcept {
    if (path_.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    path_.clear();
}

auto CountingMemoryDisk::read_page(const storage::PageAddress address)
    -> base::Result<storage::Page> {
    reads_.fetch_add(1U, std::memory_order_relaxed);
    storage::Page::Storage bytes{};
    {
        const std::shared_lock lock{mutex_};
        const auto relation = relations_.find(address.relation_id);
        if (relation == relations_.end()
            || address.page_id.value() >= relation->second.pages.size()) {
            return base::fail(base::Error{base::ErrorCode::kNotFound, "benchmark page not found"});
        }
        bytes = relation->second.pages[address.page_id.value()];
    }
    return storage::Page::load(bytes, address);
}

auto CountingMemoryDisk::write_page(const storage::PageAddress address, const storage::Page& page)
    -> base::Status {
    writes_.fetch_add(1U, std::memory_order_relaxed);
    const std::unique_lock lock{mutex_};
    const auto relation = relations_.find(address.relation_id);
    if (relation == relations_.end() || address.page_id.value() >= relation->second.pages.size()) {
        return base::fail(base::Error{base::ErrorCode::kNotFound, "benchmark page not found"});
    }
    relation->second.pages[address.page_id.value()] = page.bytes();
    return {};
}

auto CountingMemoryDisk::allocate_page(
    const base::RelationId relation_id,
    const storage::PageKind kind,
    const std::size_t special_size
) -> base::Result<storage::PageAddress> {
    if (relation_id == base::kInvalidRelationId) {
        return base::fail(base::Error{base::ErrorCode::kInvalidArgument, "invalid relation"});
    }
    const std::unique_lock lock{mutex_};
    auto& pages = relations_[relation_id].pages;
    if (pages.size() >= base::kInvalidPageId.value()) {
        return base::fail(base::Error{base::ErrorCode::kOutOfRange, "too many benchmark pages"});
    }
    const storage::PageAddress address{
        relation_id,
        base::PageId{static_cast<std::uint32_t>(pages.size())},
    };
    auto page = storage::Page::create(kind, address, special_size);
    if (!page) {
        return base::fail(std::move(page).error());
    }
    pages.push_back(page->bytes());
    allocations_.fetch_add(1U, std::memory_order_relaxed);
    return address;
}

auto CountingMemoryDisk::page_count(const base::RelationId relation_id)
    -> base::Result<std::uint64_t> {
    const std::shared_lock lock{mutex_};
    const auto relation = relations_.find(relation_id);
    return relation == relations_.end() ? 0U : relation->second.pages.size();
}

auto CountingMemoryDisk::sync_relation(const base::RelationId /*relation_id*/) -> base::Status {
    syncs_.fetch_add(1U, std::memory_order_relaxed);
    return {};
}

auto CountingMemoryDisk::sync_all() -> base::Status {
    syncs_.fetch_add(1U, std::memory_order_relaxed);
    return {};
}

auto CountingMemoryDisk::remove_relation(const base::RelationId relation_id, const bool missing_ok)
    -> base::Status {
    const std::unique_lock lock{mutex_};
    if (relations_.erase(relation_id) == 0U && !missing_ok) {
        return base::fail(base::Error{base::ErrorCode::kNotFound, "benchmark relation not found"});
    }
    return {};
}

void CountingMemoryDisk::reset_counters() noexcept {
    reads_.store(0U, std::memory_order_relaxed);
    writes_.store(0U, std::memory_order_relaxed);
    allocations_.store(0U, std::memory_order_relaxed);
    syncs_.store(0U, std::memory_order_relaxed);
}

auto CountingMemoryDisk::counters() const noexcept -> DiskCounters {
    return DiskCounters{
        .reads = reads_.load(std::memory_order_relaxed),
        .writes = writes_.load(std::memory_order_relaxed),
        .allocations = allocations_.load(std::memory_order_relaxed),
        .syncs = syncs_.load(std::memory_order_relaxed),
    };
}

}  // namespace db::benchmark_support

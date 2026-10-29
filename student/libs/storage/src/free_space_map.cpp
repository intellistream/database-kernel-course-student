#include "db/storage/free_space_map.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "detail/durable_metadata.hpp"

namespace db::storage {
namespace {

constexpr std::size_t kPayloadHeaderSize = 16;
constexpr std::uint16_t kClassCount = 256;
constexpr std::uint32_t kInitialCapacity = 64;

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)};
}

[[maybe_unused]] auto inactive(std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, "free-space map is inactive"}.with_context(
        "storage.free_space_map",
        std::move(operation)
    );
}

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "storage.free_space_map",
        "decode persistent image"
    );
}

[[maybe_unused]] auto classify([[maybe_unused]] const std::uint16_t free_bytes) noexcept
    -> std::uint8_t {
    // LAB-S01.Q06:R08-BEGIN
    base::lab::missing_algorithm("S01.Q06:R08");
    // LAB-S01.Q06:R08-END
}

class FreeSpaceTree final {
public:
    FreeSpaceTree()
        : values_(std::size_t{2} * kInitialCapacity),
          capacity_{kInitialCapacity} {}

    [[maybe_unused]] explicit FreeSpaceTree(
        [[maybe_unused]] const std::vector<std::uint8_t>& categories
    )
        : FreeSpaceTree{} {
        // LAB-S01.Q06:R09-BEGIN
        base::lab::missing_algorithm("S01.Q06:R09");
        // LAB-S01.Q06:R09-END
    }

    [[maybe_unused]] auto find([[maybe_unused]] const std::uint8_t needed_class) const
        -> std::optional<base::PageId> {
        // LAB-S01.Q06:R10-BEGIN
        base::lab::missing_algorithm("S01.Q06:R10");
        // LAB-S01.Q06:R10-END
    }

    [[maybe_unused]] void update(
        [[maybe_unused]] const base::PageId page_id,
        [[maybe_unused]] const std::uint8_t category
    ) {
        // LAB-S01.Q06:R11-BEGIN
        base::lab::missing_algorithm("S01.Q06:R11");
        // LAB-S01.Q06:R11-END
    }

    [[maybe_unused]] auto category([[maybe_unused]] const base::PageId page_id) const
        -> std::optional<std::uint8_t> {
        // LAB-S01.Q06:R12-BEGIN
        base::lab::missing_algorithm("S01.Q06:R12");
        // LAB-S01.Q06:R12-END
    }

    [[maybe_unused]] auto tracked_pages() const noexcept -> std::size_t {
        // LAB-S01.Q06:R13-BEGIN
        base::lab::missing_algorithm("S01.Q06:R13");
        // LAB-S01.Q06:R13-END
    }

    [[maybe_unused]] auto categories() const -> std::vector<std::uint8_t> {
        // LAB-S01.Q06:R14-BEGIN
        base::lab::missing_algorithm("S01.Q06:R14");
        // LAB-S01.Q06:R14-END
    }

private:
    [[maybe_unused]] void grow([[maybe_unused]] const std::uint32_t page) {
        // LAB-S01.Q06:R15-BEGIN
        base::lab::missing_algorithm("S01.Q06:R15");
        // LAB-S01.Q06:R15-END
    }

    [[maybe_unused]] void rebuild() noexcept {
        // LAB-S01.Q06:R16-BEGIN
        base::lab::missing_algorithm("S01.Q06:R16");
        // LAB-S01.Q06:R16-END
    }

    std::vector<std::uint8_t> values_;
    std::uint32_t capacity_;
    std::optional<std::uint32_t> max_page_;
};

[[maybe_unused]] auto encode_payload(const FreeSpaceTree& tree)
    -> base::Result<std::vector<std::byte>> {
    const auto categories = tree.categories();
    std::vector<std::byte> payload(kPayloadHeaderSize + categories.size());
    base::LittleEndianWriter writer{payload};
    auto status = writer.write_u32(static_cast<std::uint32_t>(categories.size()));
    if (status) {
        status = writer.write_u16(kMaximumPageFreeSpace);
    }
    if (status) {
        status = writer.write_u16(kClassCount);
    }
    if (status) {
        status = writer.write_u32(kPayloadHeaderSize);
    }
    if (status) {
        status = writer.write_u32(0);
    }
    for (const auto category : categories) {
        if (status) {
            status = writer.write_u8(category);
        }
    }
    if (!status) {
        return base::fail(std::move(status).error());
    }
    return payload;
}

[[maybe_unused]] auto decode_payload(const base::Bytes payload) -> base::Result<FreeSpaceTree> {
    if (payload.size() < kPayloadHeaderSize) {
        return base::fail(corrupt("FSM payload header is truncated"));
    }
    base::LittleEndianReader reader{payload};
    auto count = reader.read_u32();
    auto maximum = reader.read_u16();
    auto classes = reader.read_u16();
    auto header_size = reader.read_u32();
    auto reserved = reader.read_u32();
    if (!count || !maximum || !classes || !header_size || !reserved) {
        return base::fail(corrupt("FSM payload header is truncated"));
    }
    if (maximum.value() != kMaximumPageFreeSpace || classes.value() != kClassCount
        || header_size.value() != kPayloadHeaderSize || reserved.value() != 0) {
        return base::fail(corrupt("FSM payload parameters are unsupported"));
    }
    if (count.value() > kMaximumFreeSpaceMapPages || reader.remaining() != count.value()) {
        return base::fail(corrupt("FSM payload entry count does not match its size"));
    }
    std::vector<std::uint8_t> categories;
    categories.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto category = reader.read_u8();
        if (!category) {
            return base::fail(corrupt("FSM category array is truncated"));
        }
        categories.push_back(category.value());
    }
    if (auto status = reader.finish(); !status) {
        return base::fail(corrupt("FSM payload has trailing data"));
    }
    return FreeSpaceTree{categories};
}

[[maybe_unused]] auto validate_page(const base::PageId page_id) -> base::Status {
    if (page_id == base::kInvalidPageId || page_id.value() >= kMaximumFreeSpaceMapPages) {
        return base::fail(invalid("page id is outside the supported FSM range"));
    }
    return {};
}

[[maybe_unused]] auto validate_free_bytes([[maybe_unused]] const std::uint16_t free_bytes)
    -> base::Status {
    // LAB-S01.Q06:R17-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q06:R17"});
    // LAB-S01.Q06:R17-END
}

}  // namespace

struct FreeSpaceMap::Impl final {
    Impl(
        FreeSpaceTree initial_tree,
        std::optional<std::filesystem::path> persistent_path,
        const std::uint64_t initial_generation
    ) noexcept
        : tree{std::move(initial_tree)},
          path{std::move(persistent_path)},
          generation{initial_generation} {}

    mutable std::shared_mutex mutex;
    FreeSpaceTree tree;
    std::optional<std::filesystem::path> path;
    std::uint64_t generation;
};

FreeSpaceMap::FreeSpaceMap()
    : implementation_{std::make_unique<Impl>(FreeSpaceTree{}, std::nullopt, 0)} {}

FreeSpaceMap::FreeSpaceMap(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

FreeSpaceMap::FreeSpaceMap(FreeSpaceMap&& other) noexcept = default;
auto FreeSpaceMap::operator=(FreeSpaceMap&& other) noexcept -> FreeSpaceMap& = default;
FreeSpaceMap::~FreeSpaceMap() = default;

[[maybe_unused]] auto FreeSpaceMap::create([[maybe_unused]] const std::filesystem::path& path)
    -> base::Result<FreeSpaceMap> {
    // LAB-S01.Q06:R18-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q06:R18"});
    // LAB-S01.Q06:R18-END
}

[[maybe_unused]] auto FreeSpaceMap::open([[maybe_unused]] const std::filesystem::path& path)
    -> base::Result<FreeSpaceMap> {
    // LAB-S01.Q06:R19-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q06:R19"});
    // LAB-S01.Q06:R19-END
}

auto FreeSpaceMap::valid() const noexcept -> bool {
    return implementation_ != nullptr;
}

[[maybe_unused]] auto
FreeSpaceMap::find_page([[maybe_unused]] const std::uint16_t needed_bytes) const
    -> base::Result<std::optional<base::PageId>> {
    // LAB-S01.Q06:R20-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q06:R20"});
    // LAB-S01.Q06:R20-END
}

[[maybe_unused]] auto FreeSpaceMap::update(
    [[maybe_unused]] const base::PageId page_id,
    [[maybe_unused]] const std::uint16_t free_bytes
) -> base::Status {
    // LAB-S01.Q06:R21-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q06:R21"});
    // LAB-S01.Q06:R21-END
}

[[maybe_unused]] auto FreeSpaceMap::record_and_find(
    [[maybe_unused]] const base::PageId page_id,
    [[maybe_unused]] const std::uint16_t observed_free_bytes,
    [[maybe_unused]] const std::uint16_t needed_bytes
) -> base::Result<std::optional<base::PageId>> {
    // LAB-S01.Q06:R22-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q06:R22"});
    // LAB-S01.Q06:R22-END
}

[[maybe_unused]] auto
FreeSpaceMap::free_space_class([[maybe_unused]] const base::PageId page_id) const
    -> std::optional<std::uint8_t> {
    // LAB-S01.Q06:R23-BEGIN
    base::lab::missing_algorithm("S01.Q06:R23");
    // LAB-S01.Q06:R23-END
}

[[maybe_unused]] auto FreeSpaceMap::tracked_pages() const -> std::size_t {
    // LAB-S01.Q06:R24-BEGIN
    base::lab::missing_algorithm("S01.Q06:R24");
    // LAB-S01.Q06:R24-END
}

auto FreeSpaceMap::generation() const -> std::uint64_t {
    if (!valid()) {
        return 0U;
    }
    const std::shared_lock lock{implementation_->mutex};
    return implementation_->generation;
}

auto FreeSpaceMap::is_persistent() const noexcept -> bool {
    return valid() && implementation_->path.has_value();
}

[[maybe_unused]] auto FreeSpaceMap::persist() -> base::Status {
    // LAB-S01.Q06:R25-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q06:R25"});
    // LAB-S01.Q06:R25-END
}

}  // namespace db::storage

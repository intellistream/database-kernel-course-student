#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"

namespace db::storage::detail {

inline constexpr std::size_t kDurableMetadataHeaderSize = 24;

struct DurableMetadata final {
    std::uint64_t generation;
    std::vector<std::byte> payload;
};

struct MetadataReadOptions final {
    std::uint32_t expected_magic;
    std::uint16_t supported_version;
    std::size_t maximum_payload_size;
};

auto publish_metadata(
    const std::filesystem::path& path,
    std::uint32_t magic,
    std::uint16_t version,
    std::uint64_t generation,
    base::Bytes payload,
    std::uint32_t permissions = 0640U
) -> base::Status;

auto load_metadata(const std::filesystem::path& path, MetadataReadOptions options)
    -> base::Result<DurableMetadata>;

}  // namespace db::storage::detail

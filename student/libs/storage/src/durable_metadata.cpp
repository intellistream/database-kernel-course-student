#include "detail/durable_metadata.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/io/durable.hpp"
#include "db/io/posix.hpp"

namespace db::storage::detail {
namespace {

constexpr std::size_t kChecksumOffset = 12;

auto metadata_error(const std::string_view message) -> base::Error {
    return base::Error{
        base::ErrorCode::kCorruption,
        std::string{message}
    }.with_context("storage", "load durable metadata");
}

struct MetadataHeader final {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t payload_size;
    std::uint64_t generation;
};

auto decode_header(const base::Bytes input) -> base::Result<MetadataHeader> {
    if (input.size() != kDurableMetadataHeaderSize) {
        return base::fail(metadata_error("durable metadata header is truncated"));
    }
    base::LittleEndianReader reader{input};
    auto magic = reader.read_u32();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto payload_size = reader.read_u32();
    auto checksum = reader.read_u32();
    auto generation = reader.read_u64();
    if (!magic || !version || !header_size || !payload_size || !checksum || !generation) {
        return base::fail(metadata_error("durable metadata header is truncated"));
    }
    if (header_size.value() != kDurableMetadataHeaderSize) {
        return base::fail(metadata_error("durable metadata header size is unsupported"));
    }
    return MetadataHeader{
        .magic = magic.value(),
        .version = version.value(),
        .payload_size = payload_size.value(),
        .generation = generation.value(),
    };
}

}  // namespace

auto publish_metadata(
    const std::filesystem::path& path,
    const std::uint32_t magic,
    const std::uint16_t version,
    const std::uint64_t generation,
    const base::Bytes payload,
    const std::uint32_t permissions
) -> base::Status {
    if (magic == 0 || version == 0) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "durable metadata magic and version must be nonzero",
            }
        );
    }
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "durable metadata payload exceeds its 32-bit length field",
            }
        );
    }

    std::vector<std::byte> image(kDurableMetadataHeaderSize + payload.size());
    base::LittleEndianWriter writer{image};
    auto status = writer.write_u32(magic);
    if (status) {
        status = writer.write_u16(version);
    }
    if (status) {
        status = writer.write_u16(kDurableMetadataHeaderSize);
    }
    if (status) {
        status = writer.write_u32(static_cast<std::uint32_t>(payload.size()));
    }
    if (status) {
        status = writer.write_u32(0);
    }
    if (status) {
        status = writer.write_u64(generation);
    }
    if (status) {
        status = writer.write_bytes(payload);
    }
    if (!status) {
        return status;
    }
    auto checksummed = base::seal_crc32c(image, kChecksumOffset);
    if (!checksummed) {
        return checksummed;
    }
    return io::atomic_replace(path, image, permissions);
}

auto load_metadata(const std::filesystem::path& path, const MetadataReadOptions options)
    -> base::Result<DurableMetadata> {
    auto file = io::PosixFile::open(path);
    if (!file) {
        return base::fail(std::move(file).error());
    }
    auto file_size = file.value().size();
    if (!file_size) {
        return base::fail(std::move(file_size).error());
    }
    if (file_size.value() < kDurableMetadataHeaderSize
        || file_size.value() - kDurableMetadataHeaderSize > options.maximum_payload_size) {
        return base::fail(
            metadata_error("durable metadata file size is outside configured bounds")
        );
    }

    std::vector<std::byte> image(file_size.value());
    auto read = file.value().read_exact_at(0, image);
    if (!read) {
        return base::fail(std::move(read).error());
    }
    auto header = decode_header(base::Bytes{image}.first(kDurableMetadataHeaderSize));
    if (!header) {
        return base::fail(std::move(header).error());
    }
    if (header.value().magic != options.expected_magic) {
        return base::fail(metadata_error("durable metadata magic does not match"));
    }
    if (header.value().version != options.supported_version) {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "durable metadata version is unsupported",
            }
                .with_context("storage", "load durable metadata")
        );
    }
    if (header.value().payload_size != image.size() - kDurableMetadataHeaderSize) {
        return base::fail(
            metadata_error("durable metadata payload length does not match file size")
        );
    }
    if (auto status = base::verify_crc32c(image, kChecksumOffset); !status) {
        return base::fail(metadata_error("durable metadata checksum does not match"));
    }
    return DurableMetadata{
        .generation = header.value().generation,
        .payload = std::vector<std::byte>{
            image.begin() + static_cast<std::ptrdiff_t>(kDurableMetadataHeaderSize),
            image.end()
        },
    };
}

}  // namespace db::storage::detail

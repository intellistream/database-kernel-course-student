#include "db/wal/control_store.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/io/durable.hpp"
#include "db/io/posix.hpp"
#include "db/wal/format.hpp"

namespace db::wal {
namespace {

inline constexpr std::string_view kCurrentFilename = "control";
inline constexpr std::string_view kStagedFilename = "control.next";

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.control_store",
        "validate"
    );
}

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.control_store",
        "open"
    );
}

[[maybe_unused]] auto publication_unavailable(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, std::move(message)}.with_context(
        "wal.control_store",
        "publish"
    );
}

[[maybe_unused]] auto inactive() -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, "WAL control store has been moved from"}
        .with_context("wal.control_store", "access store");
}

struct CheckedControlImage final {
    WalControlFileBytes bytes;
    WalControlImage image;
};

[[maybe_unused]] auto seal_image(WalControlImage image) -> base::Result<CheckedControlImage> {
    image.checksum = 0;
    auto encoded = encode_control_image(image);
    if (!encoded) {
        return base::fail(
            std::move(encoded).error().with_context("wal.control_store", "encode control image")
        );
    }
    if (auto status = base::seal_crc32c(encoded.value(), kWalControlChecksumOffset); !status) {
        return base::fail(
            std::move(status).error().with_context("wal.control_store", "seal control image")
        );
    }
    base::LittleEndianReader checksum_reader{
        base::Bytes{encoded.value()}.subspan(kWalControlChecksumOffset, sizeof(std::uint32_t))
    };
    auto checksum = checksum_reader.read_u32();
    if (!checksum) {
        return base::fail(
            std::move(checksum).error().with_context("wal.control_store", "read sealed checksum")
        );
    }
    image.checksum = checksum.value();
    return CheckedControlImage{
        .bytes = std::move(encoded).value(),
        .image = image,
    };
}

[[maybe_unused]] auto load_image(const std::filesystem::path& path, const std::string_view role)
    -> base::Result<CheckedControlImage> {
    auto file = io::PosixFile::open(path);
    if (!file) {
        return base::fail(
            std::move(file).error().with_context("wal.control_store", "open " + std::string{role})
        );
    }
    auto size = file->size();
    if (!size) {
        return base::fail(std::move(size).error());
    }
    if (size.value() != kWalControlFileSize) {
        return base::fail(corrupt(std::string{role} + " has the wrong physical length"));
    }
    WalControlFileBytes bytes{};
    if (auto status = file->read_exact_at(0, bytes); !status) {
        return base::fail(
            std::move(status).error().with_context("wal.control_store", "read " + std::string{role})
        );
    }
    if (auto status = base::verify_crc32c(bytes, kWalControlChecksumOffset); !status) {
        return base::fail(
            std::move(status).error().with_context(
                "wal.control_store",
                "verify " + std::string{role}
            )
        );
    }
    auto decoded = decode_control_image(bytes);
    if (!decoded) {
        return base::fail(
            std::move(decoded).error().with_context(
                "wal.control_store",
                "decode " + std::string{role}
            )
        );
    }
    if (decoded->generation == 0) {
        return base::fail(corrupt(std::string{role} + " has generation zero"));
    }
    return CheckedControlImage{
        .bytes = bytes,
        .image = std::move(decoded).value(),
    };
}

[[maybe_unused]] auto ensure_absent(
    [[maybe_unused]] const std::filesystem::path& path,
    [[maybe_unused]] const std::string_view role
) -> base::Status {
    // LAB-S10.Q12:R101010-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q12:R101010"});
    // LAB-S10.Q12:R101010-END
}

[[maybe_unused]] auto
same_serialized_image(const CheckedControlImage& left, const CheckedControlImage& right) noexcept
    -> bool {
    return left.bytes == right.bytes;
}

[[maybe_unused]] auto validate_successor(
    const WalControlImage& authoritative,
    const WalControlImage& next,
    const bool decoding
) -> base::Status {
    const auto error = [decoding](std::string message) {
        return base::fail(decoding ? corrupt(std::move(message)) : invalid(std::move(message)));
    };
    if (next.system_identifier != authoritative.system_identifier) {
        return error("staged control image changes the system identifier");
    }
    if (authoritative.generation == std::numeric_limits<std::uint64_t>::max()) {
        return error("control generation is exhausted");
    }
    if (next.generation != authoritative.generation + 1U) {
        return error("staged control image is not the next generation");
    }
    return {};
}

}  // namespace

struct WalControlStore::Inner final {
    Inner(
        std::filesystem::path control_directory,
        CheckedControlImage current_image,
        std::optional<CheckedControlImage> staged_image
    )
        : directory{std::move(control_directory)},
          authoritative{current_image},
          staged{staged_image} {}

    auto current_path() const -> std::filesystem::path {
        return directory / kCurrentFilename;
    }

    auto staged_path() const -> std::filesystem::path {
        return directory / kStagedFilename;
    }

    [[maybe_unused]] auto stage_locked([[maybe_unused]] WalControlImage next) -> base::Status {
        // LAB-S10.Q07:R1011-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1011"});
        // LAB-S10.Q07:R1011-END
    }

    [[maybe_unused]] auto publish_staged_locked() -> base::Status {
        // LAB-S10.Q07:R1012-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1012"});
        // LAB-S10.Q07:R1012-END
    }

    std::filesystem::path directory;
    mutable std::mutex mutex;
    CheckedControlImage authoritative;
    std::optional<CheckedControlImage> staged;
    bool publication_uncertain{false};
};

namespace {

[[maybe_unused]] auto
load_staged(const std::filesystem::path& path, const CheckedControlImage& authoritative)
    -> base::Result<std::optional<CheckedControlImage>> {
    auto staged = load_image(path, "staged control image");
    if (!staged) {
        if (staged.error().code() == base::ErrorCode::kNotFound) {
            return std::optional<CheckedControlImage>{};
        }
        return base::fail(std::move(staged).error());
    }
    if (staged->image.system_identifier != authoritative.image.system_identifier) {
        return base::fail(corrupt("staged control image belongs to another system"));
    }
    if (staged->image.generation == authoritative.image.generation) {
        if (!same_serialized_image(staged.value(), authoritative)) {
            return base::fail(corrupt("equal control generations contain different images"));
        }
        return std::optional<CheckedControlImage>{};
    }
    if (auto status = validate_successor(authoritative.image, staged->image, true); !status) {
        return base::fail(std::move(status).error());
    }
    return std::optional<CheckedControlImage>{std::move(staged).value()};
}

}  // namespace

[[maybe_unused]] auto WalControlStore::create(
    [[maybe_unused]] const std::filesystem::path& directory,
    [[maybe_unused]] WalControlImage initial
) -> base::Result<WalControlStore> {
    // LAB-S10.Q12:R101011-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q12:R101011"});
    // LAB-S10.Q12:R101011-END
}

[[maybe_unused]] auto WalControlStore::open([[maybe_unused]] const std::filesystem::path& directory)
    -> base::Result<WalControlStore> {
    // LAB-S10.Q12:R101012-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q12:R101012"});
    // LAB-S10.Q12:R101012-END
}

WalControlStore::WalControlStore(std::unique_ptr<Inner> inner) noexcept
    : inner_{std::move(inner)} {}

WalControlStore::~WalControlStore() = default;

WalControlStore::WalControlStore(WalControlStore&& other) noexcept = default;

auto WalControlStore::operator=(WalControlStore&& other) noexcept -> WalControlStore& = default;

auto WalControlStore::valid() const noexcept -> bool {
    return inner_ != nullptr;
}

auto WalControlStore::directory() const noexcept -> const std::filesystem::path& {
    static const std::filesystem::path kEmptyPath;
    return inner_ == nullptr ? kEmptyPath : inner_->directory;
}

auto WalControlStore::current_path() const -> std::filesystem::path {
    return inner_ == nullptr ? std::filesystem::path{} : inner_->current_path();
}

auto WalControlStore::staged_path() const -> std::filesystem::path {
    return inner_ == nullptr ? std::filesystem::path{} : inner_->staged_path();
}

auto WalControlStore::current() const -> WalControlImage {
    if (inner_ == nullptr) {
        return {};
    }
    const std::scoped_lock lock{inner_->mutex};
    return inner_->authoritative.image;
}

auto WalControlStore::has_staged_update() const -> bool {
    if (inner_ == nullptr) {
        return false;
    }
    const std::scoped_lock lock{inner_->mutex};
    return inner_->staged.has_value();
}

auto WalControlStore::staged() const -> base::Result<WalControlImage> {
    if (inner_ == nullptr) {
        return base::fail(inactive());
    }
    const std::scoped_lock lock{inner_->mutex};
    if (!inner_->staged.has_value()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kNotFound,
                "no staged control generation is available",
            }
        );
    }
    return inner_->staged->image;
}

[[maybe_unused]] auto WalControlStore::stage([[maybe_unused]] WalControlImage next)
    -> base::Status {
    // LAB-S10.Q07:R1013-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1013"});
    // LAB-S10.Q07:R1013-END
}

[[maybe_unused]] auto WalControlStore::publish_staged() -> base::Status {
    // LAB-S10.Q07:R1014-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1014"});
    // LAB-S10.Q07:R1014-END
}

[[maybe_unused]] auto WalControlStore::publish([[maybe_unused]] WalControlImage next)
    -> base::Status {
    // LAB-S10.Q07:R1015-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1015"});
    // LAB-S10.Q07:R1015-END
}

}  // namespace db::wal

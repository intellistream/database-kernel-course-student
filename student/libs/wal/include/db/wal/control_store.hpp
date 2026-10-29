#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "db/base/result.hpp"
#include "db/wal/control.hpp"

namespace db::wal {

inline constexpr std::uint64_t kInitialWalControlGeneration = 1;

// Move-only, thread-safe owner of the authoritative WAL control image and its
// durable but non-authoritative staged successor. Multi-process writers require
// an external database-directory lock.
class WalControlStore final {
public:
    static auto create(const std::filesystem::path& directory, WalControlImage initial)
        -> base::Result<WalControlStore>;
    static auto open(const std::filesystem::path& directory) -> base::Result<WalControlStore>;

    ~WalControlStore();

    WalControlStore(const WalControlStore&) = delete;
    auto operator=(const WalControlStore&) -> WalControlStore& = delete;
    WalControlStore(WalControlStore&& other) noexcept;
    auto operator=(WalControlStore&& other) noexcept -> WalControlStore&;

    // Moving transfers ownership and leaves the source as an inert value.
    auto valid() const noexcept -> bool;
    auto directory() const noexcept -> const std::filesystem::path&;
    auto current_path() const -> std::filesystem::path;
    auto staged_path() const -> std::filesystem::path;
    auto current() const -> WalControlImage;
    auto has_staged_update() const -> bool;
    auto staged() const -> base::Result<WalControlImage>;

    // Stage requires the same system identifier and current generation + 1.
    // The staged image is durable but remains non-authoritative after success.
    auto stage(WalControlImage next) -> base::Status;

    // Publish re-reads and validates the staged bytes before durably replacing
    // the authoritative image. A failed publication requires reopen before a
    // caller decides whether to retry because rename may already have happened.
    auto publish_staged() -> base::Status;
    auto publish(WalControlImage next) -> base::Status;

private:
    struct Inner;

    explicit WalControlStore(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::wal

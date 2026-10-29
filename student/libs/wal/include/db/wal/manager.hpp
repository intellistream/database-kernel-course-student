#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/control.hpp"
#include "db/wal/record.hpp"
#include "db/wal/startup_validation.hpp"

namespace db::wal {

inline constexpr std::size_t kDefaultWalRingPages = 128;

class WalControlStore;
class WalStorageFaultPort;

struct WalManagerOptions final {
    std::uint64_t system_identifier{0};
    std::uint32_t timeline{1};
    std::size_t ring_page_count{kDefaultWalRingPages};
    base::Lsn retention_lsn{base::kInvalidLsn};
    base::Lsn redo_lsn{base::kInvalidLsn};
    bool create{false};
    std::shared_ptr<WalStorageFaultPort> storage_fault_port{};
};

struct WalInsertResult final {
    base::Lsn start_lsn{base::kInvalidLsn};
    base::Lsn end_lsn{base::kInvalidLsn};

    auto operator==(const WalInsertResult&) const noexcept -> bool = default;
};

enum class WalPageImagePolicy : std::uint8_t {
    kAutomatic,
    kForce,
    kSuppress,
};

// The caller retains the image storage (and any page pin/latch protecting it)
// for the complete insertion call. page_lsn and hole are immutable snapshots
// taken under that protection; WAL does not interpret a storage page header.
// keep_delta preserves block delta bytes even when an FPI covers the block.
// A resource manager that enables it must provide an image of the pre-delta
// base: recovery restores that image and then applies the retained delta.
// Normal post-mutation FPI registrations leave this false.
struct WalPageImageRegistration final {
    const WalPageImage* image{nullptr};
    base::Lsn page_lsn{base::kInvalidLsn};
    WalPageImageHole hole{};
    WalPageImagePolicy policy{WalPageImagePolicy::kAutomatic};
    bool keep_delta{false};
};

// Record-oriented WAL facade. It owns a background completed-page writer and
// provides explicit flush and shutdown durability boundaries.
class WalManager final {
public:
    static auto open(const std::filesystem::path& directory, const WalManagerOptions& options)
        -> base::Result<WalManager>;

    ~WalManager();

    WalManager(const WalManager&) = delete;
    auto operator=(const WalManager&) -> WalManager& = delete;
    WalManager(WalManager&& other) noexcept;
    auto operator=(WalManager&& other) -> WalManager& = delete;

    // Moving transfers the background lifecycle owner and leaves the source inert.
    auto valid() const noexcept -> bool;
    auto directory() const noexcept -> const std::filesystem::path&;
    auto end_lsn() const noexcept -> base::Lsn;
    auto written_lsn() const noexcept -> base::Lsn;
    auto flushed_lsn() const noexcept -> base::Lsn;
    auto redo_lsn() const noexcept -> base::Lsn;
    auto retention_lsn() const noexcept -> base::Lsn;
    // This proof is immutable. It becomes stale if the manager subsequently
    // appends WAL or advances retention; recovery readers detect that by
    // comparing their own immutable snapshot before replay.
    auto startup_validation() const noexcept -> WalStartupValidation;
    auto successful_sync_count() const noexcept -> std::uint64_t;
    auto is_shutdown() const -> bool;

    auto insert(const WalRecord& record) -> base::Result<WalInsertResult>;
    auto insert_with_page_images(
        WalRecord record,
        std::span<const WalPageImageRegistration> registrations
    ) -> base::Result<WalInsertResult>;

    // Inserts the checkpoint-redo marker and atomically moves the FPI horizon
    // to its start LSN with respect to every ordinary insertion.
    auto begin_checkpoint() -> base::Result<WalInsertResult>;

    auto flush_through(base::Lsn target_lsn) -> base::Status;
    auto flush_commit(base::Lsn target_lsn, std::chrono::microseconds leader_delay) -> base::Status;

    // Completes the crash-safe order: checkpoint WAL, authoritative control,
    // then removal of whole segments before the new durable redo horizon.
    auto complete_checkpoint(
        WalControlStore& control_store,
        WalControlImage next_control,
        base::Lsn checkpoint_end_lsn
    ) -> base::Status;
    auto recycle_before(base::Lsn durable_redo_lsn) -> base::Status;

    // The owning composition root calls shutdown only after joining every
    // session/worker that can use this manager. It durably flushes the complete
    // prefix, stops and joins the background writer, and is idempotent.
    auto shutdown() -> base::Status;

private:
    struct Inner;

    explicit WalManager(std::shared_ptr<Inner> inner) noexcept;

    std::shared_ptr<Inner> inner_;
};

}  // namespace db::wal

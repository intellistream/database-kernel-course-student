#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/visibility.hpp"

namespace db::benchmarks {
namespace {

constexpr std::size_t kHotBatchSize = 4'096U;
constexpr std::size_t kConcurrentBatchSize = 256U;
constexpr std::size_t kLockConcurrentBatchSize = 64U;
constexpr std::size_t kStatusWorkingSet = 512U;
constexpr auto kFirstXid = base::Xid{3U};
constexpr auto kRelationId = base::RelationId{7U};

void skip_with_error(benchmark::State& state, const base::Error& error) {
    state.SkipWithError(error.to_string());
}

auto populate_committed_statuses(benchmark::State& state, transaction::CommitLog& commit_log)
    -> bool {
    const auto last_xid =
        base::Xid{kFirstXid.value() + static_cast<std::uint32_t>(kStatusWorkingSet - 1U)};
    if (auto allocated = commit_log.ensure_xid_allocated(last_xid); !allocated) {
        skip_with_error(state, allocated.error());
        return false;
    }
    for (std::size_t index = 0U; index < kStatusWorkingSet; ++index) {
        const auto xid = base::Xid{kFirstXid.value() + static_cast<std::uint32_t>(index)};
        if (auto published = commit_log.set_status(xid, transaction::TransactionStatus::kCommitted);
            !published) {
            skip_with_error(state, published.error());
            return false;
        }
    }
    return true;
}

void clog_status_cache_hit(benchmark::State& state) {
    transaction::CommitLog commit_log;
    if (!populate_committed_statuses(state, commit_log)) {
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        for (std::size_t index = 0U; index < kHotBatchSize; ++index) {
            const auto xid = base::Xid{
                kFirstXid.value() + static_cast<std::uint32_t>(index % kStatusWorkingSet)
            };
            benchmark::DoNotOptimize(commit_log.status(xid));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kHotBatchSize));
}

void clog_publish(benchmark::State& state) {
    constexpr auto last_xid =
        base::Xid{kFirstXid.value() + static_cast<std::uint32_t>(kHotBatchSize - 1U)};
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        transaction::CommitLog commit_log;
        auto allocated = commit_log.ensure_xid_allocated(last_xid);
        state.ResumeTiming();
        if (!allocated) {
            skip_with_error(state, allocated.error());
            return;
        }
        for (std::size_t index = 0U; index < kHotBatchSize; ++index) {
            const auto xid = base::Xid{kFirstXid.value() + static_cast<std::uint32_t>(index)};
            if (auto published =
                    commit_log.set_status(xid, transaction::TransactionStatus::kCommitted);
                !published) {
                skip_with_error(state, published.error());
                return;
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kHotBatchSize));
}

void clog_publish_contended(benchmark::State& state) {
    // Shared deliberately across Google Benchmark worker threads and repetitions.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static transaction::CommitLog commit_log;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static std::atomic<std::uint32_t> next_xid{100'000U};
    for (auto iteration : state) {
        static_cast<void>(iteration);
        for (std::size_t index = 0U; index < kConcurrentBatchSize; ++index) {
            const auto xid = base::Xid{next_xid.fetch_add(1U, std::memory_order_relaxed)};
            if (auto published =
                    commit_log.set_status(xid, transaction::TransactionStatus::kCommitted);
                !published) {
                skip_with_error(state, published.error());
                return;
            }
        }
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(kConcurrentBatchSize)
        * static_cast<std::int64_t>(state.threads())
    );
}

void visibility_cache_hit(benchmark::State& state) {
    transaction::CommitLog commit_log;
    if (!populate_committed_statuses(state, commit_log)) {
        return;
    }
    const transaction::Snapshot snapshot{
        .xmin = base::Xid{1'000U},
        .xmax = base::Xid{2'000U},
        .in_progress = {},
        .command_id = 1U,
    };

    for (auto iteration : state) {
        static_cast<void>(iteration);
        for (std::size_t index = 0U; index < kHotBatchSize; ++index) {
            const auto creator = base::Xid{
                kFirstXid.value() + static_cast<std::uint32_t>(index % kStatusWorkingSet)
            };
            const transaction::TupleVersionState tuple{
                .xmin = creator,
                .xmax = base::kInvalidXid,
                .command_id = 0U,
            };
            benchmark::DoNotOptimize(
                transaction::tuple_visible_in_snapshot(
                    tuple,
                    base::Xid{2'001U},
                    snapshot,
                    commit_log
                )
            );
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kHotBatchSize));
}

struct ProcArrayFixture final {
    std::unique_ptr<transaction::ProcArray> proc_array{};
    std::vector<transaction::ProcSlotLease> leases{};
    base::Xid owner_xid{base::kInvalidXid};
};

auto make_proc_array_fixture(std::size_t active_count) -> base::Result<ProcArrayFixture> {
    auto proc_array = std::make_unique<transaction::ProcArray>();
    std::vector<transaction::ProcSlotLease> leases;
    leases.reserve(active_count);
    base::Xid owner_xid{base::kInvalidXid};
    for (std::size_t index = 0U; index < active_count; ++index) {
        auto lease = proc_array->register_session();
        if (!lease) {
            return base::fail(std::move(lease).error());
        }
        auto xid = proc_array->assign_xid(lease.value());
        if (!xid) {
            return base::fail(std::move(xid).error());
        }
        if (index == 0U) {
            owner_xid = xid.value();
        }
        leases.push_back(std::move(lease).value());
    }

    auto completing_lease = proc_array->register_session();
    if (!completing_lease) {
        return base::fail(std::move(completing_lease).error());
    }
    auto completing_xid = proc_array->assign_xid(completing_lease.value());
    if (!completing_xid) {
        return base::fail(std::move(completing_xid).error());
    }
    if (auto completed =
            proc_array->publish_completion(completing_lease.value(), completing_xid.value());
        !completed) {
        return base::fail(std::move(completed).error());
    }
    if (auto closed = completing_lease->close(); !closed) {
        return base::fail(std::move(closed).error());
    }
    return ProcArrayFixture{
        .proc_array = std::move(proc_array),
        .leases = std::move(leases),
        .owner_xid = owner_xid,
    };
}

void proc_array_snapshot(benchmark::State& state) {
    const auto active_count = static_cast<std::size_t>(state.range(0));
    auto fixture = make_proc_array_fixture(active_count);
    if (!fixture) {
        skip_with_error(state, fixture.error());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto snapshot = fixture->proc_array->snapshot(fixture->owner_xid);
        if (!snapshot) {
            skip_with_error(state, snapshot.error());
            return;
        }
        benchmark::DoNotOptimize(snapshot->in_progress.data());
        benchmark::ClobberMemory();
    }
    state.counters["active_xids"] = static_cast<double>(active_count);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(active_count));
}

void proc_array_snapshot_reuse(benchmark::State& state) {
    const auto active_count = static_cast<std::size_t>(state.range(0));
    auto fixture = make_proc_array_fixture(active_count);
    if (!fixture) {
        skip_with_error(state, fixture.error());
        return;
    }
    auto first = fixture->proc_array->snapshot_and_register(fixture->leases.front());
    if (!first) {
        skip_with_error(state, first.error());
        return;
    }
    auto previous = std::move(first->registered_snapshot);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto capture =
            fixture->proc_array->snapshot_and_register(fixture->leases.front(), &previous);
        if (!capture) {
            skip_with_error(state, capture.error());
            return;
        }
        benchmark::DoNotOptimize(capture->reused);
        previous = std::move(capture->registered_snapshot);
    }
    state.counters["active_xids"] = static_cast<double>(active_count);
    state.SetItemsProcessed(state.iterations());
}

void read_only_transaction_cycle(benchmark::State& state) {
    transaction::TransactionManager manager;
    auto lease = manager.register_session();
    if (!lease) {
        skip_with_error(state, lease.error());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        for (std::size_t index = 0U; index < kHotBatchSize; ++index) {
            auto xid = manager.assign_xid(lease.value());
            if (!xid) {
                skip_with_error(state, xid.error());
                return;
            }
            if (auto completed = manager.complete_read_only(lease.value(), xid.value());
                !completed) {
                skip_with_error(state, completed.error());
                return;
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kHotBatchSize));
}

void read_only_transaction_cycle_contended(benchmark::State& state) {
    // Shared deliberately across Google Benchmark worker threads and repetitions.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static transaction::TransactionManager manager;
    auto lease = manager.register_session();
    if (!lease) {
        skip_with_error(state, lease.error());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        for (std::size_t index = 0U; index < kConcurrentBatchSize; ++index) {
            auto xid = manager.assign_xid(lease.value());
            if (!xid) {
                skip_with_error(state, xid.error());
                return;
            }
            if (auto completed = manager.complete_read_only(lease.value(), xid.value());
                !completed) {
                skip_with_error(state, completed.error());
                return;
            }
        }
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(kConcurrentBatchSize)
        * static_cast<std::int64_t>(state.threads())
    );
}

void row_lock_batch(benchmark::State& state) {
    const auto row_count = static_cast<std::size_t>(state.range(0));
    transaction::LockManager manager;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        if (auto table =
                manager
                    .acquire_table(kFirstXid, kRelationId, transaction::TableLockMode::kRowShare);
            !table) {
            skip_with_error(state, table.error());
            return;
        }
        for (std::size_t index = 0U; index < row_count; ++index) {
            const auto rid = base::Rid{
                base::PageId{static_cast<std::uint32_t>(index)},
                base::SlotId{1U},
            };
            if (auto row = manager.acquire_row(
                    kFirstXid,
                    kRelationId,
                    rid,
                    transaction::RowLockMode::kKeyShare
                );
                !row) {
                skip_with_error(state, row.error());
                return;
            }
        }
        if (auto released = manager.release_all(kFirstXid); !released) {
            skip_with_error(state, released.error());
            return;
        }
    }
    state.counters["rows/transaction"] = static_cast<double>(row_count);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(row_count + 1U));
}

void row_lock_cycle_contended(benchmark::State& state, bool shared_target) {
    // Shared deliberately across Google Benchmark worker threads and repetitions.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static transaction::LockManager manager;
    const auto thread_index = static_cast<std::uint32_t>(state.thread_index());
    const auto xid = base::Xid{1'000'000U + thread_index};
    const auto relation_id =
        shared_target ? kRelationId : base::RelationId{kRelationId.value() + thread_index + 1U};
    const auto rid = base::Rid{
        base::PageId{shared_target ? 1U : thread_index + 1U},
        base::SlotId{1U},
    };

    for (auto iteration : state) {
        static_cast<void>(iteration);
        for (std::size_t index = 0U; index < kLockConcurrentBatchSize; ++index) {
            if (auto table =
                    manager.acquire_table(xid, relation_id, transaction::TableLockMode::kRowShare);
                !table) {
                skip_with_error(state, table.error());
                return;
            }
            if (auto row =
                    manager.acquire_row(xid, relation_id, rid, transaction::RowLockMode::kKeyShare);
                !row) {
                skip_with_error(state, row.error());
                return;
            }
            if (auto released = manager.release_all(xid); !released) {
                skip_with_error(state, released.error());
                return;
            }
        }
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(kLockConcurrentBatchSize)
        * static_cast<std::int64_t>(state.threads())
    );
}

void row_lock_disjoint_contended(benchmark::State& state) {
    row_lock_cycle_contended(state, false);
}

void row_lock_shared_contended(benchmark::State& state) {
    row_lock_cycle_contended(state, true);
}

void row_lock_cycle_with_unrelated_targets(benchmark::State& state) {
    const auto unrelated_count = static_cast<std::size_t>(state.range(0));
    transaction::LockManager manager;
    for (std::size_t index = 0U; index < unrelated_count; ++index) {
        const auto xid = base::Xid{10'000U + static_cast<std::uint32_t>(index)};
        const auto relation_id = base::RelationId{10'000U + static_cast<std::uint32_t>(index)};
        if (auto table =
                manager.acquire_table(xid, relation_id, transaction::TableLockMode::kAccessShare);
            !table) {
            skip_with_error(state, table.error());
            return;
        }
    }

    constexpr auto xid = base::Xid{2'000'000U};
    constexpr auto rid = base::Rid{base::PageId{2'000'000U}, base::SlotId{1U}};
    for (auto iteration : state) {
        static_cast<void>(iteration);
        if (auto table =
                manager.acquire_table(xid, kRelationId, transaction::TableLockMode::kRowShare);
            !table) {
            skip_with_error(state, table.error());
            return;
        }
        if (auto row =
                manager.acquire_row(xid, kRelationId, rid, transaction::RowLockMode::kKeyShare);
            !row) {
            skip_with_error(state, row.error());
            return;
        }
        if (auto released = manager.release_all(xid); !released) {
            skip_with_error(state, released.error());
            return;
        }
    }
    state.counters["unrelated_targets"] = static_cast<double>(unrelated_count);
    state.SetItemsProcessed(state.iterations());
}

void ssi_conflict_cycle(benchmark::State& state) {
    transaction::SerializableConflictManager manager;
    std::uint32_t next_xid = kFirstXid.value();
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto reader_xid = base::Xid{next_xid++};
        const auto writer_xid = base::Xid{next_xid++};
        const transaction::Snapshot snapshot{
            .xmin = reader_xid,
            .xmax = base::Xid{next_xid},
            .in_progress = {reader_xid, writer_xid},
            .command_id = 1U,
        };
        auto reader_registered = manager.register_transaction(reader_xid);
        auto writer_registered = manager.register_transaction(writer_xid);
        if (!reader_registered || !writer_registered) {
            state.SkipWithError("failed to register SSI benchmark transactions");
            return;
        }
        if (auto set_reader = manager.set_snapshot(reader_xid, snapshot); !set_reader) {
            skip_with_error(state, set_reader.error());
            return;
        }
        if (auto set_writer = manager.set_snapshot(writer_xid, snapshot); !set_writer) {
            skip_with_error(state, set_writer.error());
            return;
        }
        const auto target = transaction::SsiReadTarget::for_tuple(
            kRelationId,
            base::Rid{base::PageId{next_xid}, base::SlotId{1U}}
        );
        if (auto read =
                manager.record_read(reader_xid, transaction::SsiReadRecord{.target = target});
            !read) {
            skip_with_error(state, read.error());
            return;
        }
        if (auto write = manager.check_write_conflict(writer_xid, target); !write) {
            skip_with_error(state, write.error());
            return;
        }
        if (auto reader_aborted = manager.abort(reader_xid); !reader_aborted) {
            skip_with_error(state, reader_aborted.error());
            return;
        }
        if (auto writer_aborted = manager.abort(writer_xid); !writer_aborted) {
            skip_with_error(state, writer_aborted.error());
            return;
        }
    }
    state.SetItemsProcessed(state.iterations());
}

void ssi_conflict_cycle_contended(benchmark::State& state, bool share_relation) {
    // Shared deliberately across Google Benchmark worker threads and repetitions.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static transaction::SerializableConflictManager manager;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static std::atomic<std::uint32_t> next_xid{100'000'000U};
    const auto relation_id =
        share_relation
            ? kRelationId
            : base::RelationId{
                  kRelationId.value() + static_cast<std::uint32_t>(state.thread_index()) + 1U
              };

    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto first_xid = next_xid.fetch_add(2U, std::memory_order_relaxed);
        const auto reader_xid = base::Xid{first_xid};
        const auto writer_xid = base::Xid{first_xid + 1U};
        const transaction::Snapshot snapshot{
            .xmin = reader_xid,
            .xmax = base::Xid{first_xid + 2U},
            .in_progress = {reader_xid, writer_xid},
            .command_id = 1U,
        };
        if (!manager.register_transaction(reader_xid)
            || !manager.register_transaction(writer_xid)) {
            state.SkipWithError("failed to register contended SSI benchmark transactions");
            return;
        }
        if (auto set_reader = manager.set_snapshot(reader_xid, snapshot); !set_reader) {
            skip_with_error(state, set_reader.error());
            return;
        }
        if (auto set_writer = manager.set_snapshot(writer_xid, snapshot); !set_writer) {
            skip_with_error(state, set_writer.error());
            return;
        }
        const auto target = transaction::SsiReadTarget::for_tuple(
            relation_id,
            base::Rid{base::PageId{first_xid}, base::SlotId{1U}}
        );
        if (auto read =
                manager.record_read(reader_xid, transaction::SsiReadRecord{.target = target});
            !read) {
            skip_with_error(state, read.error());
            return;
        }
        if (auto write = manager.check_write_conflict(writer_xid, target); !write) {
            skip_with_error(state, write.error());
            return;
        }
        if (auto reader_aborted = manager.abort(reader_xid); !reader_aborted) {
            skip_with_error(state, reader_aborted.error());
            return;
        }
        if (auto writer_aborted = manager.abort(writer_xid); !writer_aborted) {
            skip_with_error(state, writer_aborted.error());
            return;
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(state.threads()));
}

void ssi_conflict_cycle_disjoint_contended(benchmark::State& state) {
    ssi_conflict_cycle_contended(state, false);
}

void ssi_conflict_cycle_same_relation_contended(benchmark::State& state) {
    ssi_conflict_cycle_contended(state, true);
}

// Google Benchmark owns the process-lifetime registration object.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(clog_status_cache_hit)->Name("transaction/clog_status_cache_hit");
BENCHMARK(clog_publish)->Name("transaction/clog_publish")->Iterations(32);
BENCHMARK(clog_publish_contended)
    ->Name("transaction/clog_publish_contended")
    ->Threads(1)
    ->Threads(8)
    ->Threads(32)
    ->UseRealTime();
BENCHMARK(visibility_cache_hit)->Name("transaction/visibility_cache_hit");
BENCHMARK(proc_array_snapshot)
    ->Name("transaction/proc_array_snapshot")
    ->ArgName("active")
    ->Arg(1)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);
BENCHMARK(proc_array_snapshot_reuse)
    ->Name("transaction/proc_array_snapshot_reuse")
    ->ArgName("active")
    ->Arg(1)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);
BENCHMARK(read_only_transaction_cycle)->Name("transaction/read_only_cycle");
BENCHMARK(read_only_transaction_cycle_contended)
    ->Name("transaction/read_only_cycle_contended")
    ->Threads(1)
    ->Threads(8)
    ->Threads(32)
    ->UseRealTime();
BENCHMARK(row_lock_batch)
    ->Name("transaction/row_lock_batch")
    ->ArgName("rows")
    ->Arg(1)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);
BENCHMARK(row_lock_disjoint_contended)
    ->Name("transaction/row_lock_disjoint_contended")
    ->Threads(1)
    ->Threads(8)
    ->Threads(32)
    ->UseRealTime();
BENCHMARK(row_lock_shared_contended)
    ->Name("transaction/row_lock_shared_contended")
    ->Threads(1)
    ->Threads(8)
    ->Threads(32)
    ->UseRealTime();
BENCHMARK(row_lock_cycle_with_unrelated_targets)
    ->Name("transaction/row_lock_cycle_with_unrelated_targets")
    ->ArgName("unrelated")
    ->Arg(0)
    ->Arg(64)
    ->Arg(1'024)
    ->Arg(4'096);
BENCHMARK(ssi_conflict_cycle)->Name("transaction/ssi_conflict_cycle");
BENCHMARK(ssi_conflict_cycle_disjoint_contended)
    ->Name("transaction/ssi_conflict_cycle_disjoint_contended")
    ->Threads(1)
    ->Threads(8)
    ->Threads(32)
    ->UseRealTime();
BENCHMARK(ssi_conflict_cycle_same_relation_contended)
    ->Name("transaction/ssi_conflict_cycle_same_relation_contended")
    ->Threads(1)
    ->Threads(8)
    ->Threads(32)
    ->UseRealTime();
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks

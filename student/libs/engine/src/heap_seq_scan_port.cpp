#include <type_traits>

#include "db/access/heap_table.hpp"
#include "db/base/lab_missing.hpp"

#include "heap_row_decoder.hpp"
#include "logical_scan_ssi.hpp"
#include "storage_query_binding.hpp"
#include "storage_scan_ports.hpp"

namespace db::engine::detail {

namespace {

struct EmptyColumnUsage final {};

template <bool KProjected>
class HeapSeqScanPort final : public execution::SeqScanPort {
public:
    HeapSeqScanPort(
        QueryExecutionContext& context,
        std::optional<execution::ExpressionProgram> logical_filter
    ) noexcept
        : context_{context},
          logical_filter_{std::move(logical_filter)} {}

    [[maybe_unused]] auto configure([[maybe_unused]] execution::SeqScanRequest request)
        -> base::Status override {
        // LAB-S02.Q07:R30-BEGIN
        base::lab::missing_algorithm("S02.Q07:R30");
        // LAB-S02.Q07:R30-END
    }

    [[maybe_unused]] auto open([[maybe_unused]] execution::ExecContext& context)
        -> base::Status override {
        // LAB-S02.Q07:R31-BEGIN
        base::lab::missing_algorithm("S02.Q07:R31");
        // LAB-S02.Q07:R31-END
    }

    [[maybe_unused]] auto next(
        [[maybe_unused]] execution::ExecContext& context,
        [[maybe_unused]] std::vector<types::Value> recycled_values
    ) -> base::Result<std::optional<execution::Tuple>> override {
        // LAB-S02.Q07:R32-BEGIN
        base::lab::missing_algorithm("S02.Q07:R32");
        // LAB-S02.Q07:R32-END
    }

    [[maybe_unused]] auto rescan([[maybe_unused]] execution::ExecContext& context)
        -> base::Status override {
        // LAB-S02.Q07:R33-BEGIN
        base::lab::missing_algorithm("S02.Q07:R33");
        // LAB-S02.Q07:R33-END
    }

    [[maybe_unused]] auto close(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        scan_.reset();
        logical_qualification_.reset();
        heap_.reset();
        descriptor_.reset();
        if constexpr (KProjected) {
            column_usage_.clear();
        }
        tuple_schema_.reset();
        return {};
    }

private:
    [[maybe_unused]] auto begin_scan([[maybe_unused]] execution::ExecContext& context)
        -> base::Status {
        // LAB-S02.Q07:R34-BEGIN
        base::lab::missing_algorithm("S02.Q07:R34");
        // LAB-S02.Q07:R34-END
    }

    QueryExecutionContext& context_;
    execution::SeqScanRequest request_{};
    std::optional<execution::ExpressionProgram> logical_filter_;
    std::unique_ptr<access::HeapScanQualificationPort> logical_qualification_;
    std::optional<access::TupleSchema> tuple_schema_;
    execution::TupleDescriptorHandle descriptor_;
    using ColumnUsageStorage =
        std::conditional_t<KProjected, std::vector<access::TupleColumnUsage>, EmptyColumnUsage>;
    [[no_unique_address]] ColumnUsageStorage column_usage_;
    std::optional<access::HeapTable> heap_;
    std::optional<access::HeapScan> scan_;
    access::HeapScanTuple scan_tuple_;
    bool configured_{false};
};

}  // namespace

auto make_heap_seq_scan_port(
    QueryExecutionContext& context,
    std::optional<execution::ExpressionProgram> logical_filter,
    const bool projected
) -> std::unique_ptr<execution::SeqScanPort> {
    if (projected) {
        return std::make_unique<HeapSeqScanPort<true>>(context, std::move(logical_filter));
    }
    return std::make_unique<HeapSeqScanPort<false>>(context, std::move(logical_filter));
}

}  // namespace db::engine::detail

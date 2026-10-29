#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/execution/tuple.hpp"
#include "db/types/data_type.hpp"
#include "db/types/operator.hpp"
#include "db/types/value.hpp"

namespace db::execution {

class ExecContext;

// Execution-port positions are deliberately distinct from planner positions.
// Plan states translate them once while compiling immutable planner metadata.
class ScanColumnIndex final {
public:
    constexpr explicit ScanColumnIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const ScanColumnIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_;
};

class ScanKeyIndex final {
public:
    constexpr explicit ScanKeyIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const ScanKeyIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_;
};

// A scan request is an immutable physical description supplied to a port at
// open time. The optional list is null for full-row materialization and
// engaged (including empty) for the exact set derived by planner setrefs.
struct SeqScanRequest final {
    base::Oid relation_oid{base::kInvalidOid};
    std::optional<std::vector<ScanColumnIndex>> required_columns{};
};

struct IndexScanRequest final {
    base::Oid relation_oid{base::kInvalidOid};
    base::Oid index_oid{base::kInvalidOid};
    std::vector<ScanColumnIndex> key_columns;
    std::vector<types::DataType> key_types;
    bool unique{false};
    std::optional<std::vector<ScanColumnIndex>> required_columns{};
};

// Values resolved by IndexScanState before a physical cursor is opened or
// rebound. A bound has the key's base TypeId but retains the expression's own
// typmod and payload. A port owns access-method details and never receives
// planner IR or expression objects.
struct IndexScanBound final {
    ScanKeyIndex key_index{};
    types::BinaryOperator comparison{types::BinaryOperator::kEqual};
    types::Value value{types::Value::null(types::kUnknownType)};
};

// Consumer-owned lifecycle boundary for one physical scan. Implementations
// may wrap heap/index access, a remote source, or a deterministic test source.
// Returned tuples are owned values; no pin, latch, page view, or catalog
// handle may escape a call. `recycled_values` transfers storage released by
// the prior candidate; an implementation may refill it or ignore it. The tuple
// descriptor may be independently owned;
// scan states validate its shape and re-materialize into their stable slot
// descriptor before publishing output. close() must be safe after any failed
// configure, bind, or open step and must tolerate repeated calls; this lets the
// owning PlanState unwind one partially acquired tree through its normal
// close-only failure path.
class ScanPort {
public:
    ScanPort(const ScanPort&) = delete;
    auto operator=(const ScanPort&) -> ScanPort& = delete;
    ScanPort(ScanPort&&) = delete;
    auto operator=(ScanPort&&) -> ScanPort& = delete;
    virtual ~ScanPort() = default;

    virtual auto open(ExecContext& context) -> base::Status = 0;
    virtual auto next(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> = 0;
    virtual auto rescan(ExecContext& context) -> base::Status = 0;
    virtual auto close(ExecContext& context) -> base::Status = 0;

protected:
    ScanPort() = default;

    // A concrete port that binds its physical decoder and logical descriptor
    // from the same schema during open() may materialize decoded values
    // without repeating Tuple::create()'s public per-value type scan. The
    // returned tuple remains fully owned; this does not let page-backed state
    // escape the port call.
    static auto materialize_tuple(
        TupleDescriptorHandle descriptor,
        std::vector<types::Value> values,
        std::optional<base::Rid> rid = std::nullopt
    ) noexcept -> Tuple {
        return Tuple{std::move(descriptor), std::move(values), rid};
    }
};

class SeqScanPort : public ScanPort {
public:
    virtual auto configure(SeqScanRequest request) -> base::Status = 0;

protected:
    SeqScanPort() = default;
};

class IndexScanPort : public ScanPort {
public:
    virtual auto configure(IndexScanRequest request) -> base::Status = 0;
    virtual auto bind(std::span<const IndexScanBound> bounds) -> base::Status = 0;

protected:
    IndexScanPort() = default;
};

}  // namespace db::execution

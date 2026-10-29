#pragma once

#include <cstddef>
#include <cstdint>

namespace db::sql {

// A SourceSpan is a zero-based half-open range over SourceScript::source_text.
// It is measured in UTF-8 bytes; user-facing line/column conversion belongs
// to the parser boundary rather than to every AST node.
struct SourceSpan final {
    std::size_t begin_byte{0U};
    std::size_t end_byte{0U};

    constexpr auto operator==(const SourceSpan&) const -> bool = default;
};

// A ParameterSlot preserves the one-based PostgreSQL-style spelling of $n.
// Zero is an invalid sentinel and is never produced by the parser.
class ParameterSlot final {
public:
    constexpr explicit ParameterSlot(const std::uint32_t one_based = 0U) noexcept
        : one_based_{one_based} {}

    constexpr auto one_based() const noexcept -> std::uint32_t {
        return one_based_;
    }

    constexpr auto valid() const noexcept -> bool {
        return one_based_ != 0U;
    }

    constexpr auto operator==(const ParameterSlot&) const -> bool = default;

private:
    std::uint32_t one_based_{0U};
};

inline constexpr ParameterSlot kInvalidParameterSlot{};

// These enums describe source SQL syntax. Binding converts scalar and boolean
// operators to db::types semantic enums; downstream layers therefore do
// not depend on parser-owned labels.
enum class UnaryOperator : std::uint8_t {
    kPlus,
    kMinus,
    kNot,
};

enum class BinaryOperator : std::uint8_t {
    kEqual,
    kNotEqual,
    kLess,
    kLessEqual,
    kGreater,
    kGreaterEqual,
    kAdd,
    kSubtract,
    kMultiply,
    kDivide,
    kModulo,
};

enum class BooleanOperator : std::uint8_t {
    kAnd,
    kOr,
    kNot,
};

enum class JoinKind : std::uint8_t {
    kInner,
    kLeft,
    kRight,
    kFull,
    kCross,
};

enum class SortDirection : std::uint8_t {
    kDefault,
    kAscending,
    kDescending,
};

enum class NullOrder : std::uint8_t {
    kDefault,
    kFirst,
    kLast,
};

enum class TransactionKind : std::uint8_t {
    kBegin,
    kCommit,
    kRollback,
};

enum class IsolationLevel : std::uint8_t {
    kReadCommitted,
    kRepeatableRead,
    kSerializable,
};

}  // namespace db::sql

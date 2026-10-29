#include "db/sql/parser.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <pg_functions.hpp>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nodes/parsenodes.hpp>
#include <nodes/pg_list.hpp>
#include <parser/scansup.hpp>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/text.hpp"

namespace db::sql {
namespace {

using duckdb_libpgquery::parse_result;
using duckdb_libpgquery::PGAConst;
using duckdb_libpgquery::PGAExpr;
using duckdb_libpgquery::PGAExpr_Kind;
using duckdb_libpgquery::PGAlias;
using duckdb_libpgquery::PGAStar;
using duckdb_libpgquery::PGBoolExpr;
using duckdb_libpgquery::PGBoolExprType;
using duckdb_libpgquery::PGColumnDef;
using duckdb_libpgquery::PGColumnRef;
using duckdb_libpgquery::PGCopyStmt;
using duckdb_libpgquery::PGCreateStmt;
using duckdb_libpgquery::PGDefElem;
using duckdb_libpgquery::PGDeleteStmt;
using duckdb_libpgquery::PGDropStmt;
using duckdb_libpgquery::PGExplainStmt;
using duckdb_libpgquery::PGFuncCall;
using duckdb_libpgquery::PGGroupingSet;
using duckdb_libpgquery::PGIndexElem;
using duckdb_libpgquery::PGIndexStmt;
using duckdb_libpgquery::PGInsertStmt;
using duckdb_libpgquery::PGJoinExpr;
using duckdb_libpgquery::PGList;
using duckdb_libpgquery::PGListCell;
using duckdb_libpgquery::PGNode;
using duckdb_libpgquery::PGNullTest;
using duckdb_libpgquery::PGParamRef;
using duckdb_libpgquery::PGRangeSubselect;
using duckdb_libpgquery::PGRangeVar;
using duckdb_libpgquery::PGRawStmt;
using duckdb_libpgquery::PGResTarget;
using duckdb_libpgquery::PGSelectStmt;
using duckdb_libpgquery::PGSortBy;
using duckdb_libpgquery::PGSubLink;
using duckdb_libpgquery::PGSubLinkType;
using duckdb_libpgquery::PGTransactionStmt;
using duckdb_libpgquery::PGTransactionStmtKind;
using duckdb_libpgquery::PGTypeCast;
using duckdb_libpgquery::PGTypeName;
using duckdb_libpgquery::PGUpdateStmt;
using duckdb_libpgquery::PGVacuumStmt;
using duckdb_libpgquery::PGValue;
using duckdb_libpgquery::PGVariableSetStmt;

constexpr std::size_t kMaximumParserSourceBytes =
    static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
constexpr std::size_t kMaximumAstNodes = static_cast<std::size_t>(32U) * 1024U;
constexpr std::size_t kMaximumRawListItems = static_cast<std::size_t>(8U) * 1024U;
constexpr std::size_t kMaximumAstNesting = 256U;

[[maybe_unused]] auto next_utf8_boundary(
    [[maybe_unused]] const std::string_view text,
    [[maybe_unused]] const std::size_t position
) -> base::Result<std::size_t> {
    static_cast<void>(text);
    static_cast<void>(position);
    // LAB-S02.Q01:R10-BEGIN
    base::lab::missing_algorithm("S02.Q01:R10");
    // LAB-S02.Q01:R10-END
}

[[maybe_unused]] auto validate_utf8([[maybe_unused]] const std::string_view text) -> base::Status {
    static_cast<void>(text);
    // LAB-S02.Q01:R11-BEGIN
    base::lab::missing_algorithm("S02.Q01:R11");
    // LAB-S02.Q01:R11-END
}

[[maybe_unused]] auto validate_sql_text([[maybe_unused]] const std::string_view text)
    -> base::Status {
    static_cast<void>(text);
    // LAB-S02.Q01:R12-BEGIN
    base::lab::missing_algorithm("S02.Q01:R12");
    // LAB-S02.Q01:R12-END
}

[[maybe_unused]] auto parse_error_at(const std::string_view source, const int character_position)
    -> base::Error {
    std::string message{"SQL parse error"};
    if (character_position > 0) {
        std::size_t position = 0U;
        int current = 1;
        while (position < source.size() && current < character_position) {
            const auto next = next_utf8_boundary(source, position);
            if (!next) {
                break;
            }
            position = next.value();
            ++current;
        }
        auto location = source_position(source, position);
        if (location) {
            message.append(" at ");
            message.append(std::to_string(location->line));
            message.push_back(':');
            message.append(std::to_string(location->column));
        }
    }
    return {base::ErrorCode::kParse, std::move(message)};
}

class ParseArena final {
public:
    ParseArena()
        : initialized_{initialize()} {}

    ~ParseArena() noexcept {
        if (initialized_) {
            duckdb_libpgquery::pg_parser_cleanup();
        }
    }

    ParseArena(const ParseArena&) = delete;
    auto operator=(const ParseArena&) -> ParseArena& = delete;
    ParseArena(ParseArena&&) = delete;
    auto operator=(ParseArena&&) -> ParseArena& = delete;

private:
    [[maybe_unused]] static auto initialize() -> bool {
        duckdb_libpgquery::pg_parser_init();
        try {
            duckdb_libpgquery::set_preserve_identifier_case(true);
        } catch (...) {
            duckdb_libpgquery::pg_parser_cleanup();
            throw;
        }
        return true;
    }

    bool initialized_{false};
};

[[maybe_unused]] auto ascii_case_equal(
    [[maybe_unused]] const std::string_view left,
    [[maybe_unused]] const std::string_view right
) noexcept -> bool {
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S02.Q01:R13-BEGIN
    base::lab::missing_algorithm("S02.Q01:R13");
    // LAB-S02.Q01:R13-END
}

[[maybe_unused]] auto words_are(
    [[maybe_unused]] const std::vector<std::string_view>& words,
    [[maybe_unused]] const std::initializer_list<std::string_view> expected
) noexcept -> bool {
    static_cast<void>(words);
    static_cast<void>(expected);
    // LAB-S02.Q01:R14-BEGIN
    base::lab::missing_algorithm("S02.Q01:R14");
    // LAB-S02.Q01:R14-END
}

[[maybe_unused]] auto ascii_word_start([[maybe_unused]] const char character) noexcept -> bool {
    static_cast<void>(character);
    // LAB-S02.Q01:R15-BEGIN
    base::lab::missing_algorithm("S02.Q01:R15");
    // LAB-S02.Q01:R15-END
}

[[maybe_unused]] auto ascii_word_continue([[maybe_unused]] const char character) noexcept -> bool {
    static_cast<void>(character);
    // LAB-S02.Q01:R16-BEGIN
    base::lab::missing_algorithm("S02.Q01:R16");
    // LAB-S02.Q01:R16-END
}

[[maybe_unused]] auto ascii_space([[maybe_unused]] const char character) noexcept -> bool {
    static_cast<void>(character);
    // LAB-S02.Q01:R17-BEGIN
    base::lab::missing_algorithm("S02.Q01:R17");
    // LAB-S02.Q01:R17-END
}

[[maybe_unused]] auto skip_line_comment(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] std::size_t position
) -> std::size_t {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R18-BEGIN
    base::lab::missing_algorithm("S02.Q01:R18");
    // LAB-S02.Q01:R18-END
}

[[maybe_unused]] auto skip_block_comment(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] std::size_t position
) -> std::optional<std::size_t> {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R19-BEGIN
    base::lab::missing_algorithm("S02.Q01:R19");
    // LAB-S02.Q01:R19-END
}

[[maybe_unused]] auto skip_utility_separator(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] std::size_t position
) -> std::optional<std::size_t> {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R20-BEGIN
    base::lab::missing_algorithm("S02.Q01:R20");
    // LAB-S02.Q01:R20-END
}

[[maybe_unused]] auto tokenize_utility_sql([[maybe_unused]] const std::string_view source)
    -> std::optional<std::vector<std::string_view>> {
    static_cast<void>(source);
    // LAB-S02.Q01:R21-BEGIN
    base::lab::missing_algorithm("S02.Q01:R21");
    // LAB-S02.Q01:R21-END
}

[[maybe_unused]] auto
simple_unquoted_identifier([[maybe_unused]] const std::string_view word) noexcept -> bool {
    static_cast<void>(word);
    // LAB-S02.Q01:R22-BEGIN
    base::lab::missing_algorithm("S02.Q01:R22");
    // LAB-S02.Q01:R22-END
}

[[maybe_unused]] auto checked_identifier(
    [[maybe_unused]] const std::string_view spelling,
    [[maybe_unused]] const ParseLimits& limits
) -> base::Result<std::string> {
    static_cast<void>(spelling);
    static_cast<void>(limits);
    // LAB-S02.Q01:R23-BEGIN
    base::lab::missing_algorithm("S02.Q01:R23");
    // LAB-S02.Q01:R23-END
}

[[maybe_unused]] auto utility_identifier(
    [[maybe_unused]] const std::string_view spelling,
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const ParseLimits& limits
) -> base::Result<SourceIdentifier> {
    static_cast<void>(spelling);
    static_cast<void>(source);
    static_cast<void>(limits);
    // LAB-S02.Q01:R24-BEGIN
    base::lab::missing_algorithm("S02.Q01:R24");
    // LAB-S02.Q01:R24-END
}

[[maybe_unused]] auto single_name(SourceIdentifier identifier) -> SourceQualifiedName {
    const SourceSpan span = identifier.span;
    return SourceQualifiedName{
        .span = span,
        .parts = {std::move(identifier)},
    };
}

struct UtilityReplacement final {
    std::size_t raw_begin_byte{0U};
    SourceSpan source_span{};
    SourceStatement::Node node;
    bool consumed{false};
};

[[maybe_unused]] auto recognized_utility_node(
    [[maybe_unused]] const std::string_view full_source,
    [[maybe_unused]] const std::string_view segment,
    [[maybe_unused]] const ParseLimits& limits
) -> base::Result<std::optional<SourceStatement::Node>> {
    static_cast<void>(full_source);
    static_cast<void>(segment);
    static_cast<void>(limits);
    // LAB-S02.Q01:R25-BEGIN
    base::lab::missing_algorithm("S02.Q01:R25");
    // LAB-S02.Q01:R25-END
}

[[maybe_unused]] auto escape_string_prefix(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const std::size_t quote
) -> bool {
    static_cast<void>(source);
    static_cast<void>(quote);
    // LAB-S02.Q01:R26-BEGIN
    base::lab::missing_algorithm("S02.Q01:R26");
    // LAB-S02.Q01:R26-END
}

[[maybe_unused]] auto skip_single_quoted_literal(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] std::size_t position
) -> std::optional<std::size_t> {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R27-BEGIN
    base::lab::missing_algorithm("S02.Q01:R27");
    // LAB-S02.Q01:R27-END
}

[[maybe_unused]] auto skip_double_quoted_identifier(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] std::size_t position
) -> std::optional<std::size_t> {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R28-BEGIN
    base::lab::missing_algorithm("S02.Q01:R28");
    // LAB-S02.Q01:R28-END
}

[[maybe_unused]] auto dollar_quote_delimiter(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const std::size_t position
) -> std::optional<std::string_view> {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R29-BEGIN
    base::lab::missing_algorithm("S02.Q01:R29");
    // LAB-S02.Q01:R29-END
}

[[maybe_unused]] auto skip_dollar_quoted_literal(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const std::size_t position
) -> std::optional<std::size_t> {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R30-BEGIN
    base::lab::missing_algorithm("S02.Q01:R30");
    // LAB-S02.Q01:R30-END
}

// Returns the unchanged position when it does not begin a lexical region that
// needs to be skipped.  An absent result means a region began but was not
// terminated, so callers can leave syntax diagnostics to the raw parser.
[[maybe_unused]] auto skip_lexical_region(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const std::size_t position
) -> std::optional<std::size_t> {
    static_cast<void>(source);
    static_cast<void>(position);
    // LAB-S02.Q01:R31-BEGIN
    base::lab::missing_algorithm("S02.Q01:R31");
    // LAB-S02.Q01:R31-END
}

// This is intentionally not a second SQL grammar.  It only skips lexical
// regions in which a keyword must not be interpreted and returns bare ASCII
// words so raw-parser gaps can be mapped to DB's documented unsupported
// surface without classifying malformed strings or comments as SQL forms.
[[maybe_unused]] auto lexical_words([[maybe_unused]] const std::string_view source)
    -> std::optional<std::vector<std::string_view>> {
    static_cast<void>(source);
    // LAB-S02.Q01:R32-BEGIN
    base::lab::missing_algorithm("S02.Q01:R32");
    // LAB-S02.Q01:R32-END
}

[[maybe_unused]] auto words_contain_in_order(
    [[maybe_unused]] const std::vector<std::string_view>& words,
    [[maybe_unused]] const std::initializer_list<std::string_view> expected
) noexcept -> bool {
    static_cast<void>(words);
    static_cast<void>(expected);
    // LAB-S02.Q01:R33-BEGIN
    base::lab::missing_algorithm("S02.Q01:R33");
    // LAB-S02.Q01:R33-END
}

[[maybe_unused]] auto
contains_copy_terminator_line([[maybe_unused]] const std::string_view source) noexcept -> bool {
    static_cast<void>(source);
    // LAB-S02.Q01:R34-BEGIN
    base::lab::missing_algorithm("S02.Q01:R34");
    // LAB-S02.Q01:R34-END
}

// libpg_query intentionally has a narrower grammar than the reference in a
// few already-documented, intentionally unsupported forms.  Do not turn a
// general syntax error into kUnsupported: only recognize complete lexical
// signatures whose absence is part of DB's SQL surface contract.
[[maybe_unused]] auto unsupported_raw_parse_shape([[maybe_unused]] const std::string_view source)
    -> std::optional<std::string_view> {
    static_cast<void>(source);
    // LAB-S02.Q01:R35-BEGIN
    base::lab::missing_algorithm("S02.Q01:R35");
    // LAB-S02.Q01:R35-END
}

[[maybe_unused]] auto inspect_utility_segment(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const std::size_t begin,
    [[maybe_unused]] const std::size_t end,
    [[maybe_unused]] const ParseLimits& limits,
    [[maybe_unused]] std::vector<UtilityReplacement>& replacements
) -> base::Status {
    static_cast<void>(source);
    static_cast<void>(begin);
    static_cast<void>(end);
    static_cast<void>(limits);
    static_cast<void>(replacements);
    // LAB-S02.Q01:R36-BEGIN
    base::lab::missing_algorithm("S02.Q01:R36");
    // LAB-S02.Q01:R36-END
}

[[maybe_unused]] auto utility_replacements(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const ParseLimits& limits
) -> base::Result<std::vector<UtilityReplacement>> {
    static_cast<void>(source);
    static_cast<void>(limits);
    // LAB-S02.Q01:R37-BEGIN
    base::lab::missing_algorithm("S02.Q01:R37");
    // LAB-S02.Q01:R37-END
}

[[maybe_unused]] void replace_utility_segments(
    [[maybe_unused]] std::string& source,
    [[maybe_unused]] const std::vector<UtilityReplacement>& replacements
) {
    static_cast<void>(source);
    static_cast<void>(replacements);
    // LAB-S02.Q01:R38-BEGIN
    base::lab::missing_algorithm("S02.Q01:R38");
    // LAB-S02.Q01:R38-END
}

// libpg_query deliberately exposes C-style tagged records through PGNode.
// Copy a checked record into a real C++ object before reading its fields, so
// the adapter does not type-pun a PGNode reference as one of its siblings.
// This is confined to the third-party lifetime boundary.
template <typename RawNode>
[[maybe_unused]] auto copy_raw_node(const void* const raw_node) noexcept -> RawNode {
    static_assert(std::is_trivially_copyable_v<RawNode>);
    RawNode copy{};
    std::memcpy(&copy, raw_node, sizeof(copy));
    return copy;
}

// DuckDB's parser exports PGListCell and PGValue payloads as C tagged unions.
// The raw tag is checked before these helpers are called. Copying the active
// payload representation keeps that ABI detail inside this adapter and avoids
// exposing union access to the C++ SQL model.
[[maybe_unused]] auto list_cell_pointer(const PGListCell& raw_cell) noexcept -> const void* {
    void* pointer = nullptr;
    static_assert(sizeof(pointer) == sizeof(long));
    std::memcpy(
        static_cast<void*>(std::addressof(pointer)),
        std::addressof(raw_cell.data),
        sizeof(pointer)
    );
    return pointer;
}

[[maybe_unused]] auto string_payload(const PGValue& raw_value) noexcept -> const char* {
    const char* string = nullptr;
    static_assert(sizeof(string) == sizeof(long));
    std::memcpy(
        static_cast<void*>(std::addressof(string)),
        std::addressof(raw_value.val),
        sizeof(string)
    );
    return string;
}

[[maybe_unused]] auto integer_payload(const PGValue& raw_value) noexcept -> long {
    long integer = 0;
    std::memcpy(std::addressof(integer), std::addressof(raw_value.val), sizeof(integer));
    return integer;
}

[[maybe_unused]] auto unsupported_shape(const std::string_view shape) -> base::Error {
    std::string message{"SQL "};
    message.append(shape);
    message.append(" is outside the DB parser surface");
    return {base::ErrorCode::kUnsupported, std::move(message)};
}

[[maybe_unused]] auto invalid_raw_shape(const std::string_view shape) -> base::Error {
    std::string message{"third-party parser returned an invalid "};
    message.append(shape);
    return {base::ErrorCode::kInternal, std::move(message)};
}

class AstProjector final {
public:
    AstProjector(
        const std::string_view source,
        const ParseLimits& limits,
        std::vector<SourceParameterSlot>& parameter_slots
    )
        : source_{source},
          limits_{limits},
          parameter_slots_{parameter_slots} {}

    [[maybe_unused]] auto
    statement([[maybe_unused]] const PGNode& raw_statement, [[maybe_unused]] const SourceSpan span)
        -> base::Result<SourceStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R01-BEGIN
        base::lab::missing_algorithm("S02.Q01:R01");
        // LAB-S02.Q01:R01-END
    }

private:
    template <typename StatementNode>
    [[maybe_unused]] static auto as_statement_node(base::Result<StatementNode> projected)
        -> base::Result<SourceStatement::Node> {
        if (!projected) {
            return base::fail(std::move(projected).error());
        }
        return SourceStatement::Node{std::move(projected).value()};
    }

    [[maybe_unused]] auto select_statement_node(const PGNode& raw_statement, const SourceSpan span)
        -> base::Result<SourceStatement::Node> {
        auto projected = query(raw_statement, span);
        if (!projected) {
            return base::fail(std::move(projected).error());
        }
        return SourceStatement::Node{SourceSelectStatement{.query = std::move(projected).value()}};
    }

    [[maybe_unused]] auto project_statement_node(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceStatement::Node> {
        // LAB-S02.Q01:R2001-BEGIN
        base::lab::missing_algorithm("S02.Q01:R2001");
        // LAB-S02.Q01:R2001-END
    }

    class RecursionScope final {
    public:
        explicit RecursionScope(AstProjector& projector)
            : projector_{projector} {}

        [[maybe_unused]] auto enter() -> base::Status {
            // LAB-S02.Q01:R39-BEGIN
            base::lab::missing_algorithm("S02.Q01:R39");
            // LAB-S02.Q01:R39-END
        }

        ~RecursionScope() {
            if (entered_) {
                --projector_.depth_;
            }
        }

        RecursionScope(const RecursionScope&) = delete;
        auto operator=(const RecursionScope&) -> RecursionScope& = delete;
        RecursionScope(RecursionScope&&) = delete;
        auto operator=(RecursionScope&&) -> RecursionScope& = delete;

    private:
        AstProjector& projector_;
        bool entered_{false};
    };

    [[maybe_unused]] auto consume_node() -> base::Status {
        // LAB-S02.Q01:R40-BEGIN
        base::lab::missing_algorithm("S02.Q01:R40");
        // LAB-S02.Q01:R40-END
    }

    [[maybe_unused]] auto checked_list_length(
        [[maybe_unused]] const PGList* const raw_list,
        [[maybe_unused]] const std::string_view name
    ) const -> base::Result<std::size_t> {
        static_cast<void>(raw_list);
        static_cast<void>(name);
        // LAB-S02.Q01:R41-BEGIN
        base::lab::missing_algorithm("S02.Q01:R41");
        // LAB-S02.Q01:R41-END
    }

    [[maybe_unused]] auto
    span_at([[maybe_unused]] const int location, [[maybe_unused]] const SourceSpan fallback) const
        -> base::Result<SourceSpan> {
        static_cast<void>(location);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R42-BEGIN
        base::lab::missing_algorithm("S02.Q01:R42");
        // LAB-S02.Q01:R42-END
    }

    [[maybe_unused]] auto identifier(
        [[maybe_unused]] const char* const spelling,
        [[maybe_unused]] const int location,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceIdentifier> {
        static_cast<void>(spelling);
        static_cast<void>(location);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R43-BEGIN
        base::lab::missing_algorithm("S02.Q01:R43");
        // LAB-S02.Q01:R43-END
    }

    [[maybe_unused]] auto value_identifier(
        [[maybe_unused]] const void* const raw_value,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceIdentifier> {
        static_cast<void>(raw_value);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R44-BEGIN
        base::lab::missing_algorithm("S02.Q01:R44");
        // LAB-S02.Q01:R44-END
    }

    [[maybe_unused]] auto name_list(
        [[maybe_unused]] const PGList* const raw_names,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceQualifiedName> {
        static_cast<void>(raw_names);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R45-BEGIN
        base::lab::missing_algorithm("S02.Q01:R45");
        // LAB-S02.Q01:R45-END
    }

    [[maybe_unused]] auto range_variable_name(
        [[maybe_unused]] const void* const raw_range_variable,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceQualifiedName> {
        static_cast<void>(raw_range_variable);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R46-BEGIN
        base::lab::missing_algorithm("S02.Q01:R46");
        // LAB-S02.Q01:R46-END
    }

    [[maybe_unused]] auto range_variable_alias(
        [[maybe_unused]] const PGRangeVar& range_variable,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<std::optional<SourceIdentifier>> {
        static_cast<void>(range_variable);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R47-BEGIN
        base::lab::missing_algorithm("S02.Q01:R47");
        // LAB-S02.Q01:R47-END
    }

    [[maybe_unused]] auto type_name(
        [[maybe_unused]] const void* const raw_type,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceTypeName> {
        static_cast<void>(raw_type);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R48-BEGIN
        base::lab::missing_algorithm("S02.Q01:R48");
        // LAB-S02.Q01:R48-END
    }

    [[maybe_unused]] auto expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R02-BEGIN
        base::lab::missing_algorithm("S02.Q01:R02");
        // LAB-S02.Q01:R02-END
    }

    [[maybe_unused]] auto constant_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R49-BEGIN
        base::lab::missing_algorithm("S02.Q01:R49");
        // LAB-S02.Q01:R49-END
    }

    [[maybe_unused]] auto column_reference_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R50-BEGIN
        base::lab::missing_algorithm("S02.Q01:R50");
        // LAB-S02.Q01:R50-END
    }

    [[maybe_unused]] auto parameter_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R51-BEGIN
        base::lab::missing_algorithm("S02.Q01:R51");
        // LAB-S02.Q01:R51-END
    }

    [[maybe_unused]] auto operator_spelling([[maybe_unused]] const PGList* const raw_names) const
        -> base::Result<std::string> {
        static_cast<void>(raw_names);
        // LAB-S02.Q01:R52-BEGIN
        base::lab::missing_algorithm("S02.Q01:R52");
        // LAB-S02.Q01:R52-END
    }

    [[maybe_unused]] static auto
    binary_operator([[maybe_unused]] const std::string_view spelling) noexcept
        -> std::optional<BinaryOperator> {
        static_cast<void>(spelling);
        // LAB-S02.Q01:R53-BEGIN
        base::lab::missing_algorithm("S02.Q01:R53");
        // LAB-S02.Q01:R53-END
    }

    [[maybe_unused]] auto expression_list(
        [[maybe_unused]] const PGList* const raw_list,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<std::vector<SourceExpressionPtr>> {
        static_cast<void>(raw_list);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R54-BEGIN
        base::lab::missing_algorithm("S02.Q01:R54");
        // LAB-S02.Q01:R54-END
    }

    [[maybe_unused]] auto a_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R55-BEGIN
        base::lab::missing_algorithm("S02.Q01:R55");
        // LAB-S02.Q01:R55-END
    }

    [[maybe_unused]] auto parenthesized_a_expression(
        [[maybe_unused]] const PGAExpr& raw,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw);
        static_cast<void>(span);
        // LAB-S02.Q01:R56-BEGIN
        base::lab::missing_algorithm("S02.Q01:R56");
        // LAB-S02.Q01:R56-END
    }

    [[maybe_unused]] auto in_list_a_expression(
        [[maybe_unused]] const PGAExpr& raw,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw);
        static_cast<void>(span);
        // LAB-S02.Q01:R57-BEGIN
        base::lab::missing_algorithm("S02.Q01:R57");
        // LAB-S02.Q01:R57-END
    }

    [[maybe_unused]] auto between_a_expression(
        [[maybe_unused]] const PGAExpr& raw,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw);
        static_cast<void>(span);
        // LAB-S02.Q01:R58-BEGIN
        base::lab::missing_algorithm("S02.Q01:R58");
        // LAB-S02.Q01:R58-END
    }

    [[maybe_unused]] auto operator_a_expression(
        [[maybe_unused]] const PGAExpr& raw,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw);
        static_cast<void>(span);
        // LAB-S02.Q01:R59-BEGIN
        base::lab::missing_algorithm("S02.Q01:R59");
        // LAB-S02.Q01:R59-END
    }

    [[maybe_unused]] auto unary_a_expression(
        [[maybe_unused]] const PGNode* const raw_operand,
        [[maybe_unused]] const std::string_view spelling,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_operand);
        static_cast<void>(spelling);
        static_cast<void>(span);
        // LAB-S02.Q01:R60-BEGIN
        base::lab::missing_algorithm("S02.Q01:R60");
        // LAB-S02.Q01:R60-END
    }

    [[maybe_unused]] auto binary_a_expression(
        [[maybe_unused]] const PGNode* const raw_left,
        [[maybe_unused]] const PGNode* const raw_right,
        [[maybe_unused]] const std::string_view spelling,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_left);
        static_cast<void>(raw_right);
        static_cast<void>(spelling);
        static_cast<void>(span);
        // LAB-S02.Q01:R61-BEGIN
        base::lab::missing_algorithm("S02.Q01:R61");
        // LAB-S02.Q01:R61-END
    }

    [[maybe_unused]] auto cast_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R62-BEGIN
        base::lab::missing_algorithm("S02.Q01:R62");
        // LAB-S02.Q01:R62-END
    }

    [[maybe_unused]] auto function_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R63-BEGIN
        base::lab::missing_algorithm("S02.Q01:R63");
        // LAB-S02.Q01:R63-END
    }

    [[maybe_unused]] auto boolean_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R64-BEGIN
        base::lab::missing_algorithm("S02.Q01:R64");
        // LAB-S02.Q01:R64-END
    }

    [[maybe_unused]] auto null_test_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R65-BEGIN
        base::lab::missing_algorithm("S02.Q01:R65");
        // LAB-S02.Q01:R65-END
    }

    [[maybe_unused]] auto sublink_expression(
        [[maybe_unused]] const PGNode& raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S07.Q08:R01-BEGIN
        base::lab::missing_algorithm("S07.Q08:R01");
        // LAB-S07.Q08:R01-END
    }

    [[maybe_unused]] auto wildcard_projection(
        [[maybe_unused]] const PGAStar& raw_star,
        [[maybe_unused]] const SourceSpan span
    ) const -> base::Result<SourceWildcardProjection> {
        static_cast<void>(raw_star);
        static_cast<void>(span);
        // LAB-S02.Q01:R66-BEGIN
        base::lab::missing_algorithm("S02.Q01:R66");
        // LAB-S02.Q01:R66-END
    }

    [[maybe_unused]] auto column_reference_wildcard(
        [[maybe_unused]] const PGColumnRef& raw_column,
        [[maybe_unused]] const SourceSpan span
    ) const -> base::Result<std::optional<SourceWildcardProjection>> {
        static_cast<void>(raw_column);
        static_cast<void>(span);
        // LAB-S02.Q01:R67-BEGIN
        base::lab::missing_algorithm("S02.Q01:R67");
        // LAB-S02.Q01:R67-END
    }

    [[maybe_unused]] auto expression_projection(
        [[maybe_unused]] const PGNode& raw_value,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceProjectionItem::Node> {
        static_cast<void>(raw_value);
        static_cast<void>(span);
        // LAB-S02.Q01:R68-BEGIN
        base::lab::missing_algorithm("S02.Q01:R68");
        // LAB-S02.Q01:R68-END
    }

    [[maybe_unused]] auto projection_node(
        [[maybe_unused]] const PGNode& raw_value,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceProjectionItem::Node> {
        static_cast<void>(raw_value);
        static_cast<void>(span);
        // LAB-S02.Q01:R69-BEGIN
        base::lab::missing_algorithm("S02.Q01:R69");
        // LAB-S02.Q01:R69-END
    }

    [[maybe_unused]] auto projection_item(
        [[maybe_unused]] const void* const raw_target,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceProjectionItem> {
        static_cast<void>(raw_target);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R70-BEGIN
        base::lab::missing_algorithm("S02.Q01:R70");
        // LAB-S02.Q01:R70-END
    }

    [[maybe_unused]] auto table_reference(
        [[maybe_unused]] const PGNode& raw_table,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceTableReferencePtr> {
        // LAB-S02.Q01:R2002-BEGIN
        base::lab::missing_algorithm("S02.Q01:R2002");
        // LAB-S02.Q01:R2002-END
    }

    [[maybe_unused]] auto named_table(
        [[maybe_unused]] const PGNode& raw_table,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<SourceTableReferencePtr> {
        static_cast<void>(raw_table);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R03-BEGIN
        base::lab::missing_algorithm("S02.Q01:R03");
        // LAB-S02.Q01:R03-END
    }

    [[maybe_unused]] auto join_kind([[maybe_unused]] const duckdb_libpgquery::PGJoinType raw) const
        -> base::Result<JoinKind> {
        static_cast<void>(raw);
        // LAB-S07.Q01:R01-BEGIN
        base::lab::missing_algorithm("S07.Q01:R01");
        // LAB-S07.Q01:R01-END
    }

    [[maybe_unused]] auto
    join_table([[maybe_unused]] const PGNode& raw_table, [[maybe_unused]] const SourceSpan fallback)
        -> base::Result<SourceTableReferencePtr> {
        static_cast<void>(raw_table);
        static_cast<void>(fallback);
        // LAB-S07.Q01:R02-BEGIN
        base::lab::missing_algorithm("S07.Q01:R02");
        // LAB-S07.Q01:R02-END
    }

    [[maybe_unused]] auto derived_table(
        [[maybe_unused]] const PGNode& raw_table,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceTableReferencePtr> {
        static_cast<void>(raw_table);
        static_cast<void>(fallback);
        // LAB-S06.Q05:R05-BEGIN
        base::lab::missing_algorithm("S06.Q05:R05");
        // LAB-S06.Q05:R05-END
    }

    [[maybe_unused]] auto from_list(
        [[maybe_unused]] const PGList* const raw_from,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceTableReferencePtr> {
        static_cast<void>(raw_from);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R71-BEGIN
        base::lab::missing_algorithm("S02.Q01:R71");
        // LAB-S02.Q01:R71-END
    }

    [[maybe_unused]] auto sort_item(
        [[maybe_unused]] const void* const raw_sort,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceSortItem> {
        static_cast<void>(raw_sort);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R72-BEGIN
        base::lab::missing_algorithm("S02.Q01:R72");
        // LAB-S02.Q01:R72-END
    }

    [[maybe_unused]] auto validate_query_form([[maybe_unused]] const PGSelectStmt& raw) const
        -> base::Status {
        static_cast<void>(raw);
        // LAB-S02.Q01:R73-BEGIN
        base::lab::missing_algorithm("S02.Q01:R73");
        // LAB-S02.Q01:R73-END
    }

    [[maybe_unused]] auto optional_expression(
        [[maybe_unused]] const PGNode* const raw_expression,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(raw_expression);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R74-BEGIN
        base::lab::missing_algorithm("S02.Q01:R74");
        // LAB-S02.Q01:R74-END
    }

    [[maybe_unused]] auto projection_list(
        [[maybe_unused]] const PGList* const raw_projection,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<std::vector<SourceProjectionItem>> {
        static_cast<void>(raw_projection);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R75-BEGIN
        base::lab::missing_algorithm("S02.Q01:R75");
        // LAB-S02.Q01:R75-END
    }

    [[maybe_unused]] auto group_by_list(
        [[maybe_unused]] const PGList* const raw_group_by,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<std::vector<SourceExpressionPtr>> {
        static_cast<void>(raw_group_by);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R76-BEGIN
        base::lab::missing_algorithm("S02.Q01:R76");
        // LAB-S02.Q01:R76-END
    }

    [[maybe_unused]] auto order_by_list(
        [[maybe_unused]] const PGList* const raw_order_by,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<std::vector<SourceSortItem>> {
        static_cast<void>(raw_order_by);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R77-BEGIN
        base::lab::missing_algorithm("S02.Q01:R77");
        // LAB-S02.Q01:R77-END
    }

    [[maybe_unused]] auto distinct_flag([[maybe_unused]] const PGList* const raw_distinct) const
        -> base::Result<bool> {
        static_cast<void>(raw_distinct);
        // LAB-S02.Q01:R78-BEGIN
        base::lab::missing_algorithm("S02.Q01:R78");
        // LAB-S02.Q01:R78-END
    }

    [[maybe_unused]] auto values_query(
        [[maybe_unused]] const PGSelectStmt& raw,
        [[maybe_unused]] const SourceSpan fallback,
        [[maybe_unused]] const bool allow_top_level_values
    ) -> base::Result<SourceQueryPtr> {
        static_cast<void>(raw);
        static_cast<void>(fallback);
        static_cast<void>(allow_top_level_values);
        // LAB-S02.Q01:R79-BEGIN
        base::lab::missing_algorithm("S02.Q01:R79");
        // LAB-S02.Q01:R79-END
    }

    [[maybe_unused]] auto select_query(
        [[maybe_unused]] const PGSelectStmt& raw,
        [[maybe_unused]] const SourceSpan fallback
    ) -> base::Result<SourceQueryPtr> {
        static_cast<void>(raw);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R80-BEGIN
        base::lab::missing_algorithm("S02.Q01:R80");
        // LAB-S02.Q01:R80-END
    }

    [[maybe_unused]] auto query(
        [[maybe_unused]] const PGNode& raw_query,
        [[maybe_unused]] const SourceSpan fallback,
        [[maybe_unused]] const bool allow_top_level_values = false
    ) -> base::Result<SourceQueryPtr> {
        static_cast<void>(fallback);
        static_cast<void>(allow_top_level_values);
        if (raw_query.type == duckdb_libpgquery::T_PGSelectStmt) {
            const auto raw = copy_raw_node<PGSelectStmt>(&raw_query);
            if (raw.op == duckdb_libpgquery::PG_SETOP_UNION) {
                // LAB-S06.Q01:R02-BEGIN
                base::lab::missing_algorithm("S06.Q01:R02");
                // LAB-S06.Q01:R02-END
            }
        }
        // LAB-S02.Q01:R96-BEGIN
        base::lab::missing_algorithm("S02.Q01:R96");
        // LAB-S02.Q01:R96-END
    }

    [[maybe_unused]] auto target_columns(
        [[maybe_unused]] const PGList* const raw_columns,
        [[maybe_unused]] const SourceSpan fallback
    ) const -> base::Result<std::vector<SourceIdentifier>> {
        static_cast<void>(raw_columns);
        static_cast<void>(fallback);
        // LAB-S02.Q01:R81-BEGIN
        base::lab::missing_algorithm("S02.Q01:R81");
        // LAB-S02.Q01:R81-END
    }

    [[maybe_unused]] auto insert_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceInsertStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R04-BEGIN
        base::lab::missing_algorithm("S02.Q01:R04");
        // LAB-S02.Q01:R04-END
    }

    [[maybe_unused]] auto update_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceUpdateStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R05-BEGIN
        base::lab::missing_algorithm("S02.Q01:R05");
        // LAB-S02.Q01:R05-END
    }

    [[maybe_unused]] auto delete_statement_node(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceDeleteStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R06-BEGIN
        base::lab::missing_algorithm("S02.Q01:R06");
        // LAB-S02.Q01:R06-END
    }

    [[maybe_unused]] auto fillfactor_option(
        [[maybe_unused]] const PGList* const options,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExpressionPtr> {
        static_cast<void>(options);
        static_cast<void>(span);
        // LAB-S02.Q01:R82-BEGIN
        base::lab::missing_algorithm("S02.Q01:R82");
        // LAB-S02.Q01:R82-END
    }

    [[maybe_unused]] auto create_table_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceCreateTableStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R07-BEGIN
        base::lab::missing_algorithm("S02.Q01:R07");
        // LAB-S02.Q01:R07-END
    }

    [[maybe_unused]] auto object_names(
        [[maybe_unused]] const PGList* const raw_names,
        [[maybe_unused]] const SourceSpan span
    ) const -> base::Result<std::vector<SourceQualifiedName>> {
        static_cast<void>(raw_names);
        static_cast<void>(span);
        // LAB-S02.Q01:R83-BEGIN
        base::lab::missing_algorithm("S02.Q01:R83");
        // LAB-S02.Q01:R83-END
    }

    [[maybe_unused]] auto drop_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) const -> base::Result<SourceStatement::Node> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R08-BEGIN
        base::lab::missing_algorithm("S02.Q01:R08");
        // LAB-S02.Q01:R08-END
    }

    [[maybe_unused]] auto create_index_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) const -> base::Result<SourceCreateIndexStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R09-BEGIN
        base::lab::missing_algorithm("S02.Q01:R09");
        // LAB-S02.Q01:R09-END
    }

    [[maybe_unused]] auto transaction_statement([[maybe_unused]] const PGNode& raw_statement) const
        -> base::Result<SourceTransactionStatement> {
        static_cast<void>(raw_statement);
        // LAB-S02.Q01:R84-BEGIN
        base::lab::missing_algorithm("S02.Q01:R84");
        // LAB-S02.Q01:R84-END
    }

    [[maybe_unused]] auto
    set_transaction_statement([[maybe_unused]] const PGNode& raw_statement) const
        -> base::Result<SourceSetTransactionStatement> {
        static_cast<void>(raw_statement);
        // LAB-S02.Q01:R85-BEGIN
        base::lab::missing_algorithm("S02.Q01:R85");
        // LAB-S02.Q01:R85-END
    }

    [[maybe_unused]] auto plain_identifier_list(
        [[maybe_unused]] const PGList* const raw_list,
        [[maybe_unused]] const SourceSpan span,
        [[maybe_unused]] const std::string_view label
    ) const -> base::Result<std::vector<SourceIdentifier>> {
        static_cast<void>(raw_list);
        static_cast<void>(span);
        static_cast<void>(label);
        // LAB-S02.Q01:R86-BEGIN
        base::lab::missing_algorithm("S02.Q01:R86");
        // LAB-S02.Q01:R86-END
    }

    [[maybe_unused]] auto copy_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceCopyFromStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R87-BEGIN
        base::lab::missing_algorithm("S02.Q01:R87");
        // LAB-S02.Q01:R87-END
    }

    [[maybe_unused]] auto explain_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) -> base::Result<SourceExplainStatement> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R88-BEGIN
        base::lab::missing_algorithm("S02.Q01:R88");
        // LAB-S02.Q01:R88-END
    }

    [[maybe_unused]] auto vacuum_statement(
        [[maybe_unused]] const PGNode& raw_statement,
        [[maybe_unused]] const SourceSpan span
    ) const -> base::Result<SourceStatement::Node> {
        static_cast<void>(raw_statement);
        static_cast<void>(span);
        // LAB-S02.Q01:R89-BEGIN
        base::lab::missing_algorithm("S02.Q01:R89");
        // LAB-S02.Q01:R89-END
    }

    [[maybe_unused]] auto checkpoint_statement([[maybe_unused]] const SourceSpan span) const
        -> base::Result<SourceStatement::Node> {
        static_cast<void>(span);
        // LAB-S10.Q08:R02-BEGIN
        base::lab::missing_algorithm("S10.Q08:R02");
        // LAB-S10.Q08:R02-END
    }

    std::string_view source_;
    const ParseLimits& limits_;
    std::vector<SourceParameterSlot>& parameter_slots_;
    std::map<std::uint32_t, std::size_t, std::less<>> parameter_slot_index_;
    std::size_t node_count_{0U};
    std::size_t depth_{0U};
};

[[maybe_unused]] auto raw_statement_span(
    [[maybe_unused]] const PGRawStmt& raw_statement,
    [[maybe_unused]] const std::size_t source_size
) -> base::Result<SourceSpan> {
    static_cast<void>(raw_statement);
    static_cast<void>(source_size);
    // LAB-S02.Q01:R90-BEGIN
    base::lab::missing_algorithm("S02.Q01:R90");
    // LAB-S02.Q01:R90-END
}

[[maybe_unused]] auto project_raw_statements(
    [[maybe_unused]] const PGList* const raw_statements,
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const ParseLimits& limits
) -> base::Result<SourceScript> {
    static_cast<void>(raw_statements);
    static_cast<void>(source);
    static_cast<void>(limits);
    // LAB-S02.Q01:R91-BEGIN
    base::lab::missing_algorithm("S02.Q01:R91");
    // LAB-S02.Q01:R91-END
}

[[maybe_unused]] auto parse_with_raw_core(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const std::string& owned_source,
    [[maybe_unused]] const ParseLimits& limits
) -> base::Result<SourceScript> {
    static_cast<void>(source);
    static_cast<void>(owned_source);
    static_cast<void>(limits);
    // LAB-S02.Q01:R92-BEGIN
    base::lab::missing_algorithm("S02.Q01:R92");
    // LAB-S02.Q01:R92-END
}

[[maybe_unused]] auto parse_with_utility_replacements(
    [[maybe_unused]] const std::string_view source,
    [[maybe_unused]] const ParseLimits& limits
) -> base::Result<SourceScript> {
    static_cast<void>(source);
    static_cast<void>(limits);
    // LAB-S02.Q01:R93-BEGIN
    base::lab::missing_algorithm("S02.Q01:R93");
    // LAB-S02.Q01:R93-END
}

}  // namespace

[[maybe_unused]] auto is_static_checkpoint_statement([[maybe_unused]] const std::string_view sql)
    -> bool {
    static_cast<void>(sql);
    // LAB-S10.Q08:R01-BEGIN
    base::lab::missing_algorithm("S10.Q08:R01");
    // LAB-S10.Q08:R01-END
}

[[maybe_unused]] auto source_position(
    [[maybe_unused]] const std::string_view sql,
    [[maybe_unused]] const std::size_t byte_offset
) -> base::Result<SourcePosition> {
    static_cast<void>(sql);
    static_cast<void>(byte_offset);
    // LAB-S02.Q01:R94-BEGIN
    base::lab::missing_algorithm("S02.Q01:R94");
    // LAB-S02.Q01:R94-END
}

[[maybe_unused]] auto
parse_sql([[maybe_unused]] const std::string_view sql, [[maybe_unused]] const ParseLimits& limits)
    -> base::Result<SourceScript> {
    static_cast<void>(sql);
    static_cast<void>(limits);
    // LAB-S02.Q01:R95-BEGIN
    base::lab::missing_algorithm("S02.Q01:R95");
    // LAB-S02.Q01:R95-END
}

}  // namespace db::sql

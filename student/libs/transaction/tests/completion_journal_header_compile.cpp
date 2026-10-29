#include <chrono>
#include <cstdint>
#include <type_traits>

#include "db/transaction/completion_journal.hpp"

namespace {

static_assert(std::is_enum_v<db::transaction::CompletionOutcome>);
static_assert(
    std::is_same_v<std::underlying_type_t<db::transaction::CompletionOutcome>, std::uint8_t>
);
static_assert(!std::is_copy_constructible_v<db::transaction::CompletionJournalPort>);

}  // namespace

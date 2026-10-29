#include <type_traits>

#include "manager_startup.hpp"

static_assert(std::is_move_constructible_v<db::wal::manager_detail::ManagerStartup>);
static_assert(std::is_same_v<
              decltype(&db::wal::manager_detail::prepare_startup),
              db::base::Result<db::wal::manager_detail::ManagerStartup> (*)(
                  const std::filesystem::path&,
                  const db::wal::WalManagerOptions&
              )>);

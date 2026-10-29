#include <type_traits>

#include "manager_checkpoint.hpp"

static_assert(std::is_same_v<
              decltype(&db::wal::manager_detail::complete_checkpoint_publication),
              db::base::Status (*)(
                  db::wal::WalWriter&,
                  db::wal::WalControlStore&,
                  db::base::Lsn,
                  db::base::Lsn,
                  db::wal::WalControlImage,
                  db::base::Lsn
              )>);

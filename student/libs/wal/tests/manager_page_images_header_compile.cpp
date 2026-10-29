#include <type_traits>

#include "manager_page_images.hpp"

static_assert(std::is_same_v<
              decltype(&db::wal::manager_detail::apply_page_image_policy),
              db::base::Result<db::wal::WalRecord> (*)(
                  db::wal::WalRecord,
                  std::span<const db::wal::WalPageImageRegistration>,
                  db::base::Lsn
              )>);

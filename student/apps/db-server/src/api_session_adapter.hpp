#pragma once

#include <memory>

#include "db/api/database.hpp"
#include "db/protocol/session_port.hpp"

namespace db::server {

// Application-owned bridge from the protocol's connection-local port to the
// stable embedded facade. It deliberately lives outside both components: the
// protocol remains database-agnostic, and api never learns wire semantics.
class ApiSessionFactory final : public protocol::SessionFactoryPort {
public:
    explicit ApiSessionFactory(std::shared_ptr<api::Database> database) noexcept;

    auto open_session() -> base::Result<std::unique_ptr<protocol::SessionPort>> override;

private:
    std::shared_ptr<api::Database> database_;
};

}  // namespace db::server

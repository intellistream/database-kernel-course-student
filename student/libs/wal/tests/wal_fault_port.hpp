#pragma once

#include <cstdint>
#include <mutex>
#include <new>
#include <stdexcept>

#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::wal::test {

class OneShotWalStorageFaultPort final : public WalStorageFaultPort {
public:
    void arm(const WalStorageFaultPoint point) {
        const std::scoped_lock lock{mutex_};
        point_ = point;
        armed_ = true;
        triggered_ = false;
    }

    auto triggered() const -> bool {
        const std::scoped_lock lock{mutex_};
        return triggered_;
    }

    auto before_operation(const WalStorageFaultPoint point) -> base::Status override {
        const std::scoped_lock lock{mutex_};
        if (!armed_ || point != point_) {
            return {};
        }
        armed_ = false;
        triggered_ = true;
        return base::fail(
            base::Error{
                base::ErrorCode::kIo,
                "injected WAL storage failure",
            }
        );
    }

private:
    mutable std::mutex mutex_;
    WalStorageFaultPoint point_{WalStorageFaultPoint::kWrite};
    bool armed_{false};
    bool triggered_{false};
};

enum class WalStorageFaultException : std::uint8_t {
    kAllocation,
    kLength,
    kStandard,
    kUnknown,
};

class OneShotThrowingWalStorageFaultPort final : public WalStorageFaultPort {
public:
    void arm(const WalStorageFaultPoint point, const WalStorageFaultException exception) {
        const std::scoped_lock lock{mutex_};
        point_ = point;
        exception_ = exception;
        armed_ = true;
        triggered_ = false;
    }

    auto triggered() const -> bool {
        const std::scoped_lock lock{mutex_};
        return triggered_;
    }

    auto before_operation(const WalStorageFaultPoint point) -> base::Status override {
        WalStorageFaultException exception = WalStorageFaultException::kUnknown;
        {
            const std::scoped_lock lock{mutex_};
            if (!armed_ || point != point_) {
                return {};
            }
            armed_ = false;
            triggered_ = true;
            exception = exception_;
        }
        switch (exception) {
            case WalStorageFaultException::kAllocation:
                throw std::bad_alloc{};
            case WalStorageFaultException::kLength:
                throw std::length_error{"injected WAL storage length failure"};
            case WalStorageFaultException::kStandard:
                throw std::runtime_error{"injected WAL storage exception"};
            case WalStorageFaultException::kUnknown:
                throw 7;
        }
        return {};
    }

private:
    mutable std::mutex mutex_;
    WalStorageFaultPoint point_{WalStorageFaultPoint::kWrite};
    WalStorageFaultException exception_{WalStorageFaultException::kUnknown};
    bool armed_{false};
    bool triggered_{false};
};

}  // namespace db::wal::test

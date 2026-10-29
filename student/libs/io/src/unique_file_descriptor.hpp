#pragma once

#include <unistd.h>
#include <utility>

namespace db::io::internal {

// Private acquisition guard for a POSIX file descriptor. Public I/O handles
// keep their explicit close() contracts, while this type protects the gap
// between a successful system call and construction of that public owner.
class UniqueFileDescriptor final {
public:
    UniqueFileDescriptor() noexcept = default;

    explicit UniqueFileDescriptor(const int descriptor) noexcept
        : descriptor_{descriptor} {}

    ~UniqueFileDescriptor() noexcept {
        reset();
    }

    UniqueFileDescriptor(const UniqueFileDescriptor&) = delete;
    auto operator=(const UniqueFileDescriptor&) -> UniqueFileDescriptor& = delete;

    UniqueFileDescriptor(UniqueFileDescriptor&& other) noexcept
        : descriptor_{other.release()} {}

    auto operator=(UniqueFileDescriptor&& other) noexcept -> UniqueFileDescriptor& {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    auto is_valid() const noexcept -> bool {
        return descriptor_ >= 0;
    }

    auto get() const noexcept -> int {
        return descriptor_;
    }

    auto release() noexcept -> int {
        return std::exchange(descriptor_, -1);
    }

    void reset(const int descriptor = -1) noexcept {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
        }
        descriptor_ = descriptor;
    }

private:
    int descriptor_{-1};
};

}  // namespace db::io::internal

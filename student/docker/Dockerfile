FROM debian:trixie-slim AS base

# Only the compiler, build tools, runner, and guide search tool belong in the
# default image. Sources and build output are mounted at runtime.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake \
        g++-14 \
        ninja-build \
        python3 \
        ripgrep \
        util-linux \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-14 \
    CXX=g++-14 \
    LANG=C.UTF-8 \
    PYTHONDONTWRITEBYTECODE=1

WORKDIR /workspace
CMD ["bash"]

# Optional analysis tools; ./dev.sh builds the smaller dev target by default.
FROM base AS tools

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && curl --fail --silent --show-error --location \
        https://apt.llvm.org/llvm-snapshot.gpg.key \
        --output /usr/share/keyrings/llvm.asc \
    && printf '%s\n' \
        'deb [signed-by=/usr/share/keyrings/llvm.asc] https://apt.llvm.org/trixie/ llvm-toolchain-trixie-22 main' \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends clang-format-22 clang-tidy-22 \
    && rm -rf /var/lib/apt/lists/*

# A plain docker build also produces the minimal image.
FROM base AS dev

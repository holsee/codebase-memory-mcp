# Dev container for codebase-memory-mcp.
#
# Mirrors the CI toolchain (.github/workflows/_lint.yml, _test.yml):
#   - Ubuntu 24.04 (noble) base — same distro as CI runners
#   - clang (distro default) + gcc for the ASan/UBSan/TSan test builds
#   - clang-format-20 / clang-tidy-20 from apt.llvm.org, exposed under the
#     plain names Makefile.cbm expects (CLANG_FORMAT ?= clang-format, ...)
#   - cppcheck 2.20.0 built from source — the exact version CI pins;
#     distro cppcheck is older and produces different findings
#   - zlib1g-dev, cmake, ccache for scripts/build.sh
#   - python3, jq, sqlite3, binutils for scripts/security-*.sh and the
#     docs/elixir-lsp evaluation protocol (sqlite3 metrics queries)
FROM mcr.microsoft.com/devcontainers/base:noble

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        clang \
        cmake \
        ccache \
        zlib1g-dev \
        python3 \
        jq \
        sqlite3 \
        binutils \
        file \
        wget \
        curl \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

# LLVM 20 tools (formatter/tidy only — the compiler stays distro clang, as in CI).
RUN wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
        | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null \
    && echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-20 main" \
        > /etc/apt/sources.list.d/llvm-20.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends clang-format-20 clang-tidy-20 \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-20 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-20 100

# cppcheck 2.20.0 from source — keep in lockstep with the version pinned in
# .github/workflows/_lint.yml so local lint matches CI findings.
RUN git clone --depth 1 --branch 2.20.0 https://github.com/danmar/cppcheck.git /tmp/cppcheck \
    && cmake -S /tmp/cppcheck -B /tmp/cppcheck/build \
        -DCMAKE_BUILD_TYPE=Release -DHAVE_RULES=OFF -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/cppcheck/build -j"$(nproc)" \
    && cmake --install /tmp/cppcheck/build \
    && rm -rf /tmp/cppcheck

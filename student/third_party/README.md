# Third-party dependencies

Every dependency is pinned, license-audited, isolated behind a narrow adapter,
and available to a clean offline build. First-party targets must not rely on a
mutable system-wide package or an unpinned network download.

## GoogleTest / GoogleMock

- Version: 1.17.0
- Tag: `v1.17.0`
- Commit: `52eb8108c5bdec04579160ae17225d66034bd723`
- Source: `https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz`
- Archive SHA-256: `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`
- License: BSD 3-Clause, retained in `googletest/LICENSE`

The source is vendored under `third_party/googletest`. CMake never uses
`FetchContent` or a system package for this dependency. First-party tests link
the official `GTest::gtest_main`/`GTest::gmock` targets through
`cmake/DbTesting.cmake`; product targets do not link GoogleTest.

## Google Benchmark

- Version: 1.9.0
- Tag: `v1.9.0`
- Source: `https://github.com/google/benchmark/archive/refs/tags/v1.9.0.tar.gz`
- Archive SHA-256: `35a77f46cc782b16fac8d3b107fbfbb37dcd645f7c28eee19f3b8e0758b48994`
- License: Apache License 2.0, retained in `google_benchmark/LICENSE`

The source is vendored under `third_party/google_benchmark` and is configured
only when `DB_BUILD_BENCHMARKS=ON`. Standalone benchmark targets link
`benchmark::benchmark_main`; product targets and GoogleTest targets do not.

## DuckDB libpg_query raw parser core

- Upstream: DuckDB `third_party/libpg_query`
- Version / commit: `v1.5.5` / `d8cdaa33fda8df955cc76ef58a280f68f4cd43fa`
- Release archive SHA-256:
  `f33155ff962e6e1e08fd1e9caffa487d4325aa60999e2eabc76feff534d6558b`
- License: BSD 3-Clause (upstream `libpg_query/LICENSE`)

The checked, reproducible 128-file subset is vendored under
`third_party/duckdb_libpg_query`. It retains the BSD-3-Clause license, grammar
and scanner inputs, checked-in generated parser/lexer, and generation helpers;
its precise retained-path hashes and exclusions are in
[`duckdb_libpg_query/UPSTREAM.md`](duckdb_libpg_query/UPSTREAM.md).

The private static `db::duckdb_pg_query` target contains only the nine raw
parser translation units. `db::sql` links it privately through its RAII
adapter; public DB headers do not include or expose a libpg_query type.
Normal CMake configuration builds it offline and does not need DuckDB, Cargo. See [the M8 parser ADR](../docs/SQL_PARSER_ADR.md).

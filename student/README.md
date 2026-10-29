# Ferrum-cpp 数据库实验

这是一套 C++23 数据库实现练习，共 **10 个阶段、83 道题**。源码提供接口、数据结构、固定格式与基础原语；学生完成 `LAB-Sxx.Qyy:Rnn` 标记的算法区间。

从 [题目总览](docs/course/README.md) 选择主题，进入各题指南。每份指南包含学习目标、需要理解的概念、源码入口、小例子、分步目标、关键约束和验证方式。

| 入口 | 用途 |
|---|---|
| [83 题索引](problem.md) | 按题号查找练习 |
| [开始练习](docs/course/getting-started.md) | 从选题、构建到提交的流程 |
| [Docker 环境](docker/README.md) | 用 Debian slim 一条命令配置并检查环境 |
| [共用约定](docs/course/conventions.md) | 类型、所有权、错误与生命周期 |
| [夹具与验收边界](docs/course/fixtures.md) | 区分固定输入、依赖端口与待实现算法 |
| [格式速查](docs/course/formats.md) | 页、元组和 WAL 的固定字节布局 |
| [运行器说明](labs/README.md) | 命令、课程选题、精确测试注册与报告 |

安装并启动 Docker 后，在仓库根目录执行：

```sh
./dev.sh
```

它从 [Dockerfile](docker/Dockerfile) 构建 Debian slim 开发环境，挂载源码并执行完整构建与给定原语检查。后续使用 `./dev.sh list --stage 06`、`./dev.sh unit S06.Q02` 或 `./dev.sh shell`。详细说明见 [Docker 环境](docker/README.md)。

也可在本机 Linux 准备 GCC/g++ 13–15、CMake 3.22 以上、Ninja、Python 3.10 以上和 `flock`。第三方依赖源码已随仓库提供，本机构建命令为：

```sh
python3 labs/run.py list --stage 06
CXX=g++-15 python3 labs/run.py scaffold
# 完成 Append 的算法区间后验收本题。
python3 labs/run.py unit S06.Q02
```

可以选做单题，不要求按阶段顺序实现。第一次练习可从 [Append](docs/course/questions/S06.Q02.md) 或 [Unique](docs/course/questions/S06.Q03.md) 开始，这两题已有独立验收。其余 81 题的独立测试注册或隔离验证仍待补齐；指南中的现有测试链接是阅读素材。初始算法桩被调用时会明确失败。

历史测试记录：完整构建、27 个给定原语测试、23 个运行器测试。范围见 [验证记录](labs/verification.json)。

公共接口、私有实现和组件测试分别位于 `libs/<component>/include/`、`src/`、`tests/`。[应用入口](apps/README.md) 和 [跨组件测试](tests/README.md) 供组合练习时使用；[构建说明](cmake/README.md) 保留编译约束。

第三方代码遵循各自许可证：[GoogleTest](third_party/googletest/LICENSE)、[Google Benchmark](third_party/google_benchmark/LICENSE)、[DuckDB libpg_query](third_party/duckdb_libpg_query/LICENSE)。

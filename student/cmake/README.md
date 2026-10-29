# 构建约束

产品使用 GNU g++ 13–15 和非扩展 C++23。预设继承当前环境的 `CC` / `CXX`；未指定时由 CMake 查找系统编译器。需要 CMake 3.22 以上和 Ninja。Clang 22 用于格式化与静态分析。

[Docker 环境](../docker/README.md) 使用 Debian slim 和 GCC 14，运行 `./dev.sh` 即可配置并检查；容器通过 `CC` / `CXX` 选择编译器，使用独立的构建目录。普通预设默认不要求 Clang；`clang-tidy` 预设显式启用分析工具。

从仓库根目录运行 `CXX=g++-15 python3 labs/run.py scaffold`，会完整编译默认目标、公共和私有头文件检查，再执行登记的给定原语测试。默认目录为 `build/gcc-release`，同一构建目录由运行器加锁。命令与可选参数见 [运行器说明](../labs/README.md)。

组件通过 `db::project_options` 等目标采用警告、sanitizer 和分析策略，不修改全局编译器标志。`DbArchitecture.cmake` 在配置时检查未声明的组件包含、依赖边、私有路径泄露和循环。

实现题目时保留这些检查及警告视为错误的要求。构建通过后，继续运行本题的具名测试；完整构建只验证编译和链接。初次操作流程见 [开始练习](../docs/course/getting-started.md)。

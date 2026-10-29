# 实验运行器

先读 [题目指南](../docs/course/README.md) 选择练习，再用本运行器构建、查看登记和验收。每题只在全部精确登记的测试实际执行并通过后取得权重；源码标记、文件名和编译成功都不作为算法通过证据。

目前 [Append](../docs/course/questions/S06.Q02.md) 与 [Unique](../docs/course/questions/S06.Q03.md) 已配置独立验收，其余 81 题的注册或隔离验证仍待补齐。初始算法桩会明确失败；缺少注册的题目报告错误或未执行。具体范围见 [verification.json](verification.json)。

## 选择命令

所有命令在仓库根目录执行，需要 Python 3.10 以上。

使用 [Docker 环境](../docker/README.md) 时，`./dev.sh` 自动准备工具链并运行 `scaffold`；其余命令可以把 `python3 labs/run.py` 换成 `./dev.sh`。

```sh
python3 labs/run.py list --stage 06
CXX=g++-15 python3 labs/run.py scaffold
python3 labs/run.py unit S06.Q02 S06.Q03
python3 labs/run.py selection --file labs/selections/sample-10.json
python3 labs/run.py plan --profile db-basic-rc
python3 labs/run.py plan --profile db-relational --json
```

| 命令 | 执行范围 |
|---|---|
| `scaffold` | 完整构建默认目标和头文件检查，执行登记的给定原语冒烟。 |
| `list` | 查看题号、源码、夹具、权重、验收范围和注册状态。 |
| `unit <题号...>` | 只验收指定题；不同题分别运行，一题失败后仍继续其他题。 |
| `selection --file <文件>` | 只验收文件所选题目，按题计分，并显示请求组合的缺口。 |
| `plan --profile <组合>` | 只读展开能力图、精确题集、替代选择和测试注册缺口。 |
| `integrate --profile <组合>` | 依次检查题集覆盖、所需单题、真实能力接线和真实 Database/Session SQL；前一步未通过即停止后续步骤。 |
| `all` | 验收 `db-reference` 后执行完整 CTest 回归。 |

`plan` 正常退出只表示计划已生成。三个数据库组合的真实验收仍待补齐。运行器不会提供数据库实现，也不会根据选择自动补全算法。

## 自定义选题

[selections/](selections/) 提供六个示例。最小课程文件如下：

```json
{
  "schema_version": 2,
  "selected_questions": ["S06.Q02", "S06.Q03"],
  "weights": {"S06.Q02": 1, "S06.Q03": 2},
  "requested_profiles": [],
  "choices": {}
}
```

每个所选题必须恰好有一个有限正数权重。`requested_profiles` 和 `choices` 可省略；前者让 `selection` 显示组合状态，不自动运行组合。文件保存固定题集，运行器不会隐式添加未选题。

`db-basic-rc`、`db-relational`、`db-reference` 默认需要 46、76、83 题。前两者默认用 CLOCK，可通过 `--choice storage.replacement=S01.Q03` 改选 LRU。命令行覆盖文件的同名选择；使用文件时也必须将题集改为包含所选实现。缺题会拒绝组合验收。

能力节点使用非空 `all` 或 `any` 列表引用题目或其他节点。整个图必须无环，未选候选分支也参与校验；未知、重复、无关或不属于候选项的选择报错。

## 构建与报告

默认构建目录为 `build/gcc-release`；可用 `LABS_BUILD_DIR` 或命令后的 `--build-dir` 指定。配置采用 Ninja、Release、`BUILD_TESTING=ON` 和 `DB_WARNINGS_AS_ERRORS=ON`。首次配置用 `CXX` 选择 GCC 13–15，后续沿用 CMake 缓存。

构建、枚举和测试共用目录内的 `build.lock` 排他锁。默认并行度为 `min(16, CPU 数)`，`LABS_JOBS` 接受 1–16。不要绕过锁并发修改同一构建目录。

```sh
LABS_JOBS=8 python3 labs/run.py scaffold
python3 labs/run.py unit S06.Q02 --no-build --report build/append-result.json
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s labs/tests -v
```

`--no-build` 使用已有二进制，但仍重新枚举并执行测试；`scaffold` 不能跳过构建。`--timeout` 是每个测试子进程的秒数，默认 300；超时终止进程组并记录失败。`--report` 保存 JSON，`--json` 让标准输出使用 JSON，构建日志写入标准错误。

| 结果或退出码 | 含义 |
|---|---|
| `passed` / `failed` | 本题实际验收通过或失败；全部具名用例通过才计权重。 |
| `not_run` / `not_selected` | 未取得执行结果或未选择，不计通过。 |
| 退出码 `0` | 所请求动作完成；只读计划不等于功能通过。 |
| 退出码 `1` | 构建或实际测试失败。 |
| 退出码 `2` | 输入、注册、枚举或执行条件错误。 |

零匹配、禁用测试、跳过、异常终止、待实现错误和 CTest `WILL_FAIL` 都不能算通过。崩溃后缺少证据的用例保持未执行。单题、能力接线、SQL 组合和完整 CTest 各自计数。

## 登记独立验收

本节供补充课程测试时使用。先按 [夹具契约](../docs/course/fixtures.md) 隔离本题依赖，再登记实际用例。`existing_test_sources` 仅列出素材；真正可验收的题目需要在 `manifest.json` 的 `questions[].unit.test_ids` 填写非空列表：

```json
{"binary": "relative/path/to/test_binary", "name": "ExactSuite.ExactCase"}
```

`binary` 是构建目录内的可执行文件相对路径，不能逃出目录；`name` 必须是 `--gtest_list_tests` 能枚举的精确完整名称，包含参数化后缀。示例只是结构，不是实际注册。不给通配符、阶段正则或整文件分配分数。

运行器先枚举本次全部相关二进制，再分题执行；同题同二进制可合并具名过滤。它清除外部 `GTEST_*` 过滤和分片环境，并核对 XML 中实际名称、数量与状态。零退出码但没有实际结果仍然失败。

`scaffold.test_ids` 同样必须登记非空的真实用例，且只能调用给定原语，不能借用待实现算法或真实数据库。当前 27 个用例覆盖字节处理、CRC、受检算术、错误和基础 I/O。纯 Python 测试验证运行器行为，临时协议夹具不参与数据库验收。

## 登记数据库组合

组合登记在 `integration.profiles[profile_id]`，`kind` 为 `database_session`，包含两组非空且不重叠的精确用例：`capability_test_ids` 验证真实能力预检、未启用功能无副作用及替代实现接线，`test_ids` 通过真实 `Database/Session` 执行 SQL。不能用 mock 数据库代替。`db-reference` 还必须设置 `full_regression: true`。

组合子进程收到 `LABS_PROFILE`，以及 JSON 格式的 `LABS_CAPABILITIES`、`LABS_CHOICES`、`LABS_SELECTED_QUESTIONS`。最后一个变量仅包含本组合要求且已通过单题验收的题集。这些变量传递选择，不会自动实现产品能力开关；缺少真实接线验收时，`product_capabilities` 为 `unverified`，组合被拒绝。

题目与能力信息优先从 [manifest.json](manifest.json) 读取，缺少的公开目录字段可从 [question-bank.json](../docs/course/question-bank.json) 读取。两份清单的 `guide` 都指向对应题目指南。

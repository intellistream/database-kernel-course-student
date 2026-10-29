# 读代码前的共用约定

[题目总览](README.md) · [练习流程](getting-started.md)

这些约定帮助你解释题目里的类型和状态。具体算法要求由对应指南和公共接口决定。

## 题号、区间和文件

`S03.Q07` 表示一道题；`S03.Q07:R01` 表示它拥有的一个实现区间。题目是学习和验收单位，文件是代码组织单位。阅读共享文件时，按 `LAB-题号` 区分责任。

公共声明通常位于 `libs/<component>/include/db/<component>/`，私有实现位于 `src/`，测试位于 `tests/`。可以从题目列出的函数沿 `#include` 找到完整参数和返回类型。

## 类型与三种“相等”

[DataType](../../libs/types/include/db/types/data_type.hpp) 表示类型 ID 和修饰信息，[Value](../../libs/types/include/db/types/value.hpp) 表示一个有类型的值。类型化 NULL 仍携带类型；BOOL、INT64、FLOAT32、CHAR 和 TIMESTAMP 等表示以头文件及类型注册表为准。SQL 各语法位置按绑定规则检查类型。

| 比较语境 | 学习时要区分的规则 | 相关题 |
|---|---|---|
| SQL 谓词 | 可能得到 TRUE、FALSE、UNKNOWN；过滤只接受 TRUE | [表达式求值](questions/S02.Q06.md) |
| Value 结构相等与哈希 | 包含类型化表示；分组和全行去重需遵守 FLOAT 位表示契约 | [分组](questions/S05.Q02.md)、[Unique](questions/S06.Q03.md) |
| 索引键比较 | 处理复合键、前缀、NULL 和持久化排序约束 | [索引键](questions/S03.Q01.md)、[索引边界](questions/S04.Q03.md) |

不要从一种比较关系直接推导另一种关系。例如，按行去重的相等规则不能直接替代 SQL `=` 的 NULL 行为；可计算的 FLOAT 谓词也不一定能安全转换为索引范围。

## 所有权：谁可以保留什么

| 类型或接口 | 需要遵守的边界 |
|---|---|
| `span`、视图、目录端口 | 通常借用外部存储，检查它们的有效期 |
| `Tuple`、结果行、计划描述符 | 需要保留的数据必须有清楚的所有者 |
| `PinGuard` | 维持帧固定；与内容读写闩锁是不同职责 |
| `PageReadGuard` / `PageWriteGuard` | 控制页内容访问，释放后不可继续使用借用的页视图 |
| 只可移动的句柄 | 移动转移资源；源对象的状态按接口约定处理 |

关键接口见 [帧与页守卫](../../libs/storage/include/db/storage/frame.hpp)、[Tuple](../../libs/execution/include/db/execution/tuple.hpp)。取得候选帧后，按同步协议确认替换资格。输出槽中的借用值遵循源对象的有效期。

## 失败是接口的一部分

可恢复失败使用 [Result / Status](../../libs/base/include/db/base/result.hpp) 和 [Error](../../libs/base/include/db/base/error.hpp)。先检查成功状态，再消费结果；补充错误上下文时保留原始原因。

函数返回失败时，要明确输出是否仍可消费、状态是否允许重试，以及资源是否只能关闭。输入校验、算法执行和对外发布可能有不同的失败边界，不能都按“返回错误即可”处理。

给定的析构、移动操作和基本守卫负责机械资源管理。学生实现仍需决定算法何时取得和释放资源，例如等待另一个事务之前必须释放本题持有的页守卫。

## 算子生命周期

[PlanState](../../libs/execution/include/db/execution/plan_state.hpp) 提供调用外壳；每题实现对应算子的行循环。`open` 建立本次状态，`next` 推进行流，`rescan` 开始新的扫描，`close` 完成清理。检查失败、EOF、重复扫描时输出槽和子节点的状态。

[ExecContext](../../libs/execution/include/db/execution/exec_context.hpp) 和 [ParameterNamespace](../../libs/planner/include/db/planner/plan.hpp) 的给定容器负责保存描述信息；参数何时绑定、表达式如何计算、计划是否合法仍由相应题目负责。

## 三类版本号不要混用

| 标识 | 它描述什么 |
|---|---|
| XID、事务 epoch、command ID | 分别标识事务、会话中的事务轮次和事务内命令 |
| 页 LSN、WAL 写入/耐久位置 | 区分页已应用的日志边界、日志写入进度和真正同步后的进度 |
| 目录 generation、脏页 generation、检查点 epoch | 表示各自状态的版本；不能跨领域比较或代替对方 |

内存中的成功、写入操作成功、同步成功和权威状态发布是不同事件。持久化题要说明每个事件发生后，其他线程或重启进程可以相信什么。

## 给定内容与学生算法

给定内容包括强类型 ID、字节序、CRC、固定格式、值和描述符、基础 I/O、通用资源管理及声明的端口。它们帮助你构造输入和表达结果。

决定查询结果、访问路径、空间选择、可见性、锁授予、事务发布和恢复动作的区间属于学生算法。项目给定固定 codec；学生实现流式读取、压缩选择和发布策略。完整目录见 [题库清单](question-bank.json)；实际修改位置见 [manifest](../../labs/manifest.json)。

关于测试中哪些依赖可以被固定输入替代，继续阅读 [夹具与验收边界](fixtures.md)。

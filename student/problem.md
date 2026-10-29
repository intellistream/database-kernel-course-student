# 83 题引导指南索引

选题后直接打开对应指南。每份指南包含源码入口、小例子、实现里程碑、关键约束、验收检查和提示问题。阶段组织知识，不是强制的完成顺序。

[学习入口](docs/course/README.md) · [练习流程](docs/course/getting-started.md) · [运行命令](labs/README.md)

**验收状态：**Append（S06.Q02）与 Unique（S06.Q03）已有独立测试；其余 81 题仍待补齐独立注册或验证。表中的权重是相对工作量，不是预计天数。

## S01 存储与堆组织

[阶段导读](docs/course/stages/S01.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S01.Q01 页文件读写与分配](docs/course/questions/S01.Q01.md) | 2 | 独立验收待补齐 |
| [S01.Q02 CLOCK 替换](docs/course/questions/S01.Q02.md) | 1 | 独立验收待补齐 |
| [S01.Q03 LRU 替换](docs/course/questions/S01.Q03.md) | 1 | 独立验收待补齐 |
| [S01.Q04 缓冲池调入与并发替换](docs/course/questions/S01.Q04.md) | 3 | 独立验收待补齐 |
| [S01.Q05 脏页回写与批次跟踪](docs/course/questions/S01.Q05.md) | 3 | 独立验收待补齐 |
| [S01.Q06 空闲空间映射 FSM](docs/course/questions/S01.Q06.md) | 2 | 独立验收待补齐 |
| [S01.Q07 堆页槽位与空间组织](docs/course/questions/S01.Q07.md) | 2 | 独立验收待补齐 |
| [S01.Q08 堆记录定位与扫描](docs/course/questions/S01.Q08.md) | 3 | 独立验收待补齐 |
| [S01.Q09 堆更新与删除的物理修改](docs/course/questions/S01.Q09.md) | 3 | 独立验收待补齐 |
| [S01.Q10 堆关系建立与打开](docs/course/questions/S01.Q10.md) | 2 | 独立验收待补齐 |

## S02 SQL 前端与基础查询

[阶段导读](docs/course/stages/S02.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S02.Q01 普通 SQL 解析适配](docs/course/questions/S02.Q01.md) | 2 | 独立验收待补齐 |
| [S02.Q02 类型转换与标量运算规则](docs/course/questions/S02.Q02.md) | 2 | 独立验收待补齐 |
| [S02.Q03 名称与作用域绑定](docs/course/questions/S02.Q03.md) | 2 | 独立验收待补齐 |
| [S02.Q04 表达式与语句类型绑定](docs/course/questions/S02.Q04.md) | 3 | 独立验收待补齐 |
| [S02.Q05 基础查询与 DML 降低](docs/course/questions/S02.Q05.md) | 3 | 独立验收待补齐 |
| [S02.Q06 表达式编译与三值求值](docs/course/questions/S02.Q06.md) | 3 | 独立验收待补齐 |
| [S02.Q07 基础算子与投影生命周期](docs/course/questions/S02.Q07.md) | 3 | 独立验收待补齐 |
| [S02.Q08 排序与 LIMIT/OFFSET](docs/course/questions/S02.Q08.md) | 2 | 独立验收待补齐 |
| [S02.Q09 DML 执行协调](docs/course/questions/S02.Q09.md) | 3 | 独立验收待补齐 |
| [S02.Q10 目录管理与表 DDL](docs/course/questions/S02.Q10.md) | 3 | 独立验收待补齐 |
| [S02.Q11 预处理语句与查询调度](docs/course/questions/S02.Q11.md) | 3 | 独立验收待补齐 |
| [S02.Q12 COPY 文本导入](docs/course/questions/S02.Q12.md) | 2 | 独立验收待补齐 |

## S03 B+Tree 与索引一致性

[阶段导读](docs/course/stages/S03.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S03.Q01 索引键与节点页算法](docs/course/questions/S03.Q01.md) | 2 | 独立验收待补齐 |
| [S03.Q02 B+Tree 下降与定位](docs/course/questions/S03.Q02.md) | 2 | 独立验收待补齐 |
| [S03.Q03 插入、分裂与新根](docs/course/questions/S03.Q03.md) | 3 | 独立验收待补齐 |
| [S03.Q04 范围扫描与游标](docs/course/questions/S03.Q04.md) | 2 | 独立验收待补齐 |
| [S03.Q05 排序批量建树](docs/course/questions/S03.Q05.md) | 3 | 独立验收待补齐 |
| [S03.Q06 唯一性与存量冲突检测](docs/course/questions/S03.Q06.md) | 3 | 独立验收待补齐 |
| [S03.Q07 唯一索引等待与重查](docs/course/questions/S03.Q07.md) | 3 | 独立验收待补齐 |
| [S03.Q08 堆索引同步与索引 DDL](docs/course/questions/S03.Q08.md) | 3 | 独立验收待补齐 |

## S04 查询优化与统计

[阶段导读](docs/course/stages/S04.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S04.Q01 表达式预处理与条件归类](docs/course/questions/S04.Q01.md) | 2 | 独立验收待补齐 |
| [S04.Q02 列需求与条件放置](docs/course/questions/S04.Q02.md) | 2 | 独立验收待补齐 |
| [S04.Q03 索引边界推导](docs/course/questions/S04.Q03.md) | 3 | 独立验收待补齐 |
| [S04.Q04 路径选择与有序性证明](docs/course/questions/S04.Q04.md) | 3 | 独立验收待补齐 |
| [S04.Q05 有界 Top-N](docs/course/questions/S04.Q05.md) | 2 | 独立验收待补齐 |
| [S04.Q06 等价类与推导谓词](docs/course/questions/S04.Q06.md) | 2 | 独立验收待补齐 |
| [S04.Q07 ANALYZE 与统计维护](docs/course/questions/S04.Q07.md) | 2 | 独立验收待补齐 |
| [S04.Q08 计划完整性验证](docs/course/questions/S04.Q08.md) | 2 | 独立验收待补齐 |

## S05 聚合与分组

[阶段导读](docs/course/stages/S05.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S05.Q01 标量聚合累加器](docs/course/questions/S05.Q01.md) | 2 | 独立验收待补齐 |
| [S05.Q02 哈希分组表](docs/course/questions/S05.Q02.md) | 2 | 独立验收待补齐 |
| [S05.Q03 每聚合 DISTINCT](docs/course/questions/S05.Q03.md) | 2 | 独立验收待补齐 |
| [S05.Q04 聚合绑定与计划](docs/course/questions/S05.Q04.md) | 3 | 独立验收待补齐 |
| [S05.Q05 聚合执行协调](docs/course/questions/S05.Q05.md) | 2 | 独立验收待补齐 |

## S06 集合与派生关系

[阶段导读](docs/course/stages/S06.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S06.Q01 UNION 解析与共型绑定](docs/course/questions/S06.Q01.md) | 3 | 独立验收待补齐 |
| [S06.Q02 Append 流式串接](docs/course/questions/S06.Q02.md) | 1 | 已注册独立测试 |
| [S06.Q03 全行 Unique](docs/course/questions/S06.Q03.md) | 2 | 已注册独立测试 |
| [S06.Q04 集合计划与输入适配](docs/course/questions/S06.Q04.md) | 3 | 独立验收待补齐 |
| [S06.Q05 派生关系绑定与扫描](docs/course/questions/S06.Q05.md) | 2 | 独立验收待补齐 |

## S07 连接与子查询

[阶段导读](docs/course/stages/S07.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S07.Q01 连接前端与基本计划](docs/course/questions/S07.Q01.md) | 2 | 独立验收待补齐 |
| [S07.Q02 嵌套循环连接](docs/course/questions/S07.Q02.md) | 2 | 独立验收待补齐 |
| [S07.Q03 Hash Join](docs/course/questions/S07.Q03.md) | 3 | 独立验收待补齐 |
| [S07.Q04 Merge Join](docs/course/questions/S07.Q04.md) | 3 | 独立验收待补齐 |
| [S07.Q05 参数化索引连接](docs/course/questions/S07.Q05.md) | 3 | 独立验收待补齐 |
| [S07.Q06 连接顺序与策略选择](docs/course/questions/S07.Q06.md) | 3 | 独立验收待补齐 |
| [S07.Q07 子计划求值与相关参数](docs/course/questions/S07.Q07.md) | 3 | 独立验收待补齐 |
| [S07.Q08 子查询规划与安全改写](docs/course/questions/S07.Q08.md) | 3 | 独立验收待补齐 |

## S08 事务基础与并发控制

[阶段导读](docs/course/stages/S08.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S08.Q01 隐式与显式事务状态机](docs/course/questions/S08.Q01.md) | 2 | 独立验收待补齐 |
| [S08.Q02 XID 与活动事务登记](docs/course/questions/S08.Q02.md) | 3 | 独立验收待补齐 |
| [S08.Q03 CLOG 状态与持久化](docs/course/questions/S08.Q03.md) | 3 | 独立验收待补齐 |
| [S08.Q04 锁兼容与等待队列](docs/course/questions/S08.Q04.md) | 3 | 独立验收待补齐 |
| [S08.Q05 死锁检测](docs/course/questions/S08.Q05.md) | 2 | 独立验收待补齐 |
| [S08.Q06 提交与中止发布](docs/course/questions/S08.Q06.md) | 3 | 独立验收待补齐 |
| [S08.Q07 Backend 与 Session 事务协调](docs/course/questions/S08.Q07.md) | 3 | 独立验收待补齐 |
| [S08.Q08 事务等待与唤醒](docs/course/questions/S08.Q08.md) | 2 | 独立验收待补齐 |

## S09 MVCC、隔离与版本回收

[阶段导读](docs/course/stages/S09.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S09.Q01 ProcArray 快照与回收边界](docs/course/questions/S09.Q01.md) | 3 | 独立验收待补齐 |
| [S09.Q02 RC/SI 快照协调与配置](docs/course/questions/S09.Q02.md) | 3 | 独立验收待补齐 |
| [S09.Q03 元组可见性与索引版本分类](docs/course/questions/S09.Q03.md) | 3 | 独立验收待补齐 |
| [S09.Q04 并发版本更新与 HOT 选择](docs/course/questions/S09.Q04.md) | 3 | 独立验收待补齐 |
| [S09.Q05 SSI 读集与谓词锁](docs/course/questions/S09.Q05.md) | 3 | 独立验收待补齐 |
| [S09.Q06 SSI 冲突图与提交裁决](docs/course/questions/S09.Q06.md) | 3 | 独立验收待补齐 |
| [S09.Q07 HOT 修剪与 VACUUM](docs/course/questions/S09.Q07.md) | 3 | 独立验收待补齐 |

## S10 WAL、检查点与恢复

[阶段导读](docs/course/stages/S10.md)

| 题目指南 | 权重 | 验收状态 |
|---|---:|---|
| [S10.Q01 WAL 环形缓冲](docs/course/questions/S10.Q01.md) | 3 | 独立验收待补齐 |
| [S10.Q02 WAL 分段写入与刷盘](docs/course/questions/S10.Q02.md) | 3 | 独立验收待补齐 |
| [S10.Q03 WAL 读取与尾部分类](docs/course/questions/S10.Q03.md) | 3 | 独立验收待补齐 |
| [S10.Q04 WAL 管理与后台刷盘](docs/course/questions/S10.Q04.md) | 3 | 独立验收待补齐 |
| [S10.Q05 整页镜像、压缩与 delta](docs/course/questions/S10.Q05.md) | 3 | 独立验收待补齐 |
| [S10.Q06 业务变更的 WAL 生成](docs/course/questions/S10.Q06.md) | 3 | 独立验收待补齐 |
| [S10.Q07 普通检查点与 control 发布](docs/course/questions/S10.Q07.md) | 3 | 独立验收待补齐 |
| [S10.Q08 静态检查点接纳与排空](docs/course/questions/S10.Q08.md) | 3 | 独立验收待补齐 |
| [S10.Q09 堆与 B+Tree 页重做](docs/course/questions/S10.Q09.md) | 3 | 独立验收待补齐 |
| [S10.Q10 事务与目录恢复收尾](docs/course/questions/S10.Q10.md) | 3 | 独立验收待补齐 |
| [S10.Q11 启动分析与恢复调度](docs/course/questions/S10.Q11.md) | 3 | 独立验收待补齐 |
| [S10.Q12 数据库初始化与关闭协议](docs/course/questions/S10.Q12.md) | 3 | 独立验收待补齐 |

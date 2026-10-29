# 按题学习 Ferrum-cpp

这里按 **10 个阶段、83 道题**组织学习。每题都有独立指南：从具体问题和小例子开始，定位实际挖空点，再按里程碑实现并核对边界。

第一次使用请读 [练习流程](getting-started.md)。共用资料有 [类型与所有权约定](conventions.md)、[夹具边界](fixtures.md) 和 [格式速查](formats.md)。完整题名索引见 [problem.md](../../problem.md)。

目前 Append、Unique 两题已有独立测试注册与验证；其余 81 题的注册或隔离验证仍待补齐。

## 按主题选择

| 阶段 | 主题 | 题数 |
|---|---|---:|
| [S01](stages/S01.md) | 存储与堆组织 | 10 |
| [S02](stages/S02.md) | SQL 前端与基础查询 | 12 |
| [S03](stages/S03.md) | B+Tree 与索引一致性 | 8 |
| [S04](stages/S04.md) | 查询优化与统计 | 8 |
| [S05](stages/S05.md) | 聚合与分组 | 5 |
| [S06](stages/S06.md) | 集合与派生关系 | 5 |
| [S07](stages/S07.md) | 连接与子查询 | 8 |
| [S08](stages/S08.md) | 事务基础与并发控制 | 8 |
| [S09](stages/S09.md) | MVCC、隔离与版本回收 | 7 |
| [S10](stages/S10.md) | WAL、检查点与恢复 | 12 |

## 一份指南的阅读顺序

```mermaid
flowchart LR
    A[理解问题与输入] --> B[手算一个小例子]
    B --> C[定位本题区间]
    C --> D[按里程碑实现]
    D --> E[检查边界与事件]
    E --> F[运行已注册验收]
```

S06.Q02 和 S06.Q03 可以直接体验完整的独立练习流程。其他题先按指南阅读与实现，验收配套的缺口会明确标注。

## 选多题时

课程文件位于 [labs/selections](../../labs/selections/)。`selection` 只验收文件中列出的题目；数据库组合需要另行满足完整能力要求并取得实际验证结果。参见 [运行器说明](../../labs/README.md) 和 [当前验证记录](../../labs/verification.json)。

## 机器可读清单

[question-bank.json](question-bank.json) 保存题目范围与夹具契约；[manifest.json](../../labs/manifest.json) 保存实际区间与测试注册。每道题的 `guide` 字段指向对应文档。

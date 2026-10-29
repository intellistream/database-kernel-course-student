# 性能测量素材

本目录保存存储、事务、SQL、执行与 WAL 等组件的 Google Benchmark 用例。部分用例直接调用组件，另一些需要完整数据库，使用前先读其输入准备、计时范围和结果校验。

初始学生版的算法尚未实现，基准测试不能用于判断本题是否正确。先按 [题目指南](../docs/course/README.md) 和 [运行器](../labs/README.md) 完成功能验收，再针对相同数据规模、缓存条件和计时边界比较实现。

关注性能时，可结合 [CLOCK](../docs/course/questions/S01.Q02.md)、[LRU](../docs/course/questions/S01.Q03.md)、[Top-N](../docs/course/questions/S04.Q05.md)、[HashJoin](../docs/course/questions/S07.Q03.md) 与 [WAL 写入及组刷盘](../docs/course/questions/S10.Q02.md) 的约束思考额外内存、无效工作和同步次数。速度不能替代边界与失败路径的正确性。

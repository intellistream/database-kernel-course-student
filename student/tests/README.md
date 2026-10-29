# 跨组件测试素材

这里包含集成、SQL 规范输出、互操作、崩溃恢复和压力测试。单个组件的测试位于 `libs/<component>/tests/`；单题指南会链接与本题有关的素材。

`sql-regress/` 保存脚本、调度列表和预期输出。SQL 回归会创建真实持久化数据库，执行脚本并比较结果，依赖多个题目的实现。它不能直接代替一道题的独立测试。

当前精确注册的独立测试是 [Append](../docs/course/questions/S06.Q02.md) 和 [Unique](../docs/course/questions/S06.Q03.md)。其他题的注册或隔离验证仍待补齐；现有测试文件存在，不表示本题已经具备独立验收。

阅读 [夹具与验收边界](../docs/course/fixtures.md) 确定测试需要隔离哪些依赖，再按 [运行器的注册规则](../labs/README.md) 登记实际具名用例。数据库组合必须另行通过真实能力接线和 `Database/Session` SQL 验收。

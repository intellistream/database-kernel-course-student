# 应用入口

`db` 是交互式 shell，`db-server` / `rmdb` 是服务器入口，`tpcc` 是工作负载客户端。它们负责参数解析、组件组装和调用公共 API，供多题完成后的组合练习使用。

初始源码可编译，但依赖的数据库算法尚未实现；启动应用不构成单题验收。先按 [题目指南](../docs/course/README.md) 完成选题，再通过 [运行器](../labs/README.md) 查看组合所需的能力和实际注册状态。

理解应用调用链时，可从 [Session 命令边界](../docs/course/questions/S08.Q07.md)、[SQL 绑定](../docs/course/questions/S02.Q03.md)、[计划验证](../docs/course/questions/S04.Q08.md) 和 [启动恢复](../docs/course/questions/S10.Q11.md) 阅读对应入口。

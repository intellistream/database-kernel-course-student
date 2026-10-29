# 数据库内核实验材料

阅读 [实验手册](实验指导.html) 或 [PDF](实验手册.pdf)，逐项任务位于 [labs](labs/)。课程主线源码位于 [student](../student/README.md)，请在 Linux / WSL2 本机磁盘工作。

L0–L7 共 32 学时。当前只登记了 Append 与 Unique 的独立验收；其余模块的测试与整库接线尚待补齐。按教师公布的可用范围提交，不把未运行写成通过。

`tools/` 中的 RMDB NUL 文本协议工具用于官方原框架对照，不能直接连接 Ferrum Wire。`cases/`、`scenarios/` 是课程公开的小数据与并发语义练习；预期输出用于说明任务合同，不是内核算法实现。工具测试运行：

```sh
python -m unittest discover -s 03_实验材料/tests -v
```

[报告模板](report_template.md) 要求记录源代码 diff、环境、命令、预期与实测及原始日志。

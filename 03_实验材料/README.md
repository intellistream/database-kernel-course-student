# 数据库内核实验材料

阅读 [实验手册](实验指导.html) 或 [PDF](实验手册.pdf)，逐项任务位于 [labs](labs/)。课程主线源码位于 [student](../student/README.md)，请在 Linux / WSL2 本机磁盘工作。

L0–L7 共 32 学时，按任务书完成实现、测试与答辩。测试安排见 [实验状态](../04_Ferrum实验基础/实验状态.md)。

`tools/` 提供 RMDB 文本协议对照工具；`cases/`、`scenarios/` 提供小数据与并发语义练习。工具测试运行：

```sh
python -m unittest discover -s 03_实验材料/tests -v
```

[报告模板](report_template.md) 要求记录源代码 diff、环境、命令、预期与实测及原始日志。

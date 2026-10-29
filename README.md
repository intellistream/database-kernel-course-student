# 数据库内核课程

华中科技大学 2027 年度数据库系统设计与实现课程，面向全国大学生计算机系统能力大赛数据库赛道训练。理论 40 学时，实践 32 学时，采用 Ferrum C++ 学生框架。

## 学习材料

| 材料 | 入口 |
| --- | --- |
| 讲义 | [PDF](05_课程讲义/数据库内核讲义.pdf) · [HTML](05_课程讲义/数据库内核讲义.html) |
| 实验手册 | [PDF](03_实验材料/实验手册.pdf) · [HTML](03_实验材料/实验指导.html) |
| 逐项任务 | [L0–L7](03_实验材料/labs/) · [报告模板](03_实验材料/report_template.md) |
| 起始源码 | [student](student/README.md) · [源码包](04_Ferrum实验基础/Ferrum-cpp-student.tar.gz) |
| 题目指南 | [83 题目录](04_Ferrum实验基础/83题目录.html) · [源码内指南](student/docs/course/README.md) |
| 大纲与课件 | [课程大纲](00_课程大纲/) · [第 6 版课件](01_课件（第6版）/) |
| 历年赛题 | [2023–2026 年资料](02_历年赛题/) · [来源](02_历年赛题/来源索引.json) |

GitHub 可直接阅读 PDF 和 Markdown；HTML 下载后在本地浏览器打开，保留目录结构。

## 开始实验

```sh
git clone https://github.com/intellistream/database-kernel-course-student.git
cd database-kernel-course-student/student
./dev.sh scaffold --report course-results/scaffold.json
./dev.sh list --stage 06
```

源码需要 Linux / WSL2 或 Docker，工具链要求见 [环境说明](student/docker/README.md)。请在本机磁盘编译，避免云盘同步目录。学生实现 `LAB-Sxx.Qyy:Rnn` 标记内的任务；给定接口、基础设施和公开用例可供阅读。

L0 环境与源码阅读、L1 缓冲池、L2 B+树、L3 查询执行、L4 锁与死锁、L5 MVCC、L6 WAL 恢复、L7 集成评测，共 32 学时。完整竞赛数据库需要配套课外开发。

实验与测试安排见 [实验状态](04_Ferrum实验基础/实验状态.md)。2027 年赛事要求按当年规则执行。

## 资料与工具检查

从课程仓库根目录运行：

```sh
python -m unittest discover -s 03_实验材料/tests -v
python scripts/validate_student.py
```

这些检查验证课程工具、材料身份和起始框架。题目实现使用实验手册指定的测试。

本仓库提供学习材料、起始框架和公开样例。作业提交到个人作业仓库。

教材课件、赛题及第三方代码保留原作者署名和各自许可；第三方材料适用各自许可。来源地址、固定版本和文件哈希随材料保存。

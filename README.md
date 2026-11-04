# 数据库内核课程

华中科技大学 2027 年度数据库系统设计与实现课程，面向全国大学生计算机系统能力大赛数据库赛道训练。理论 40 学时，实践 40 学时，采用 Ferrum C++ 学生框架。

任课教师：ZHANG SHUHAO、谢美意、毛言粲。

理论第3–10周每周两次、11–14周每周一次，每次2学时；实验第4–12周及第14周各一次，每次4学时，第12、14周实验置于当周理论之后。

课件共838页，每次理论课40–46页。

[五个模块课件与20次课目录](01_课件（第6版）/README.md) · [10次实验安排](03_实验材料/课堂实验安排.md)

## 学习材料

| 材料 | 入口 |
| --- | --- |
| 专题参考讲义 | [PDF](05_课程讲义/数据库内核讲义.pdf) · [HTML](05_课程讲义/数据库内核讲义.html) |
| 实验手册 | [PDF](03_实验材料/实验手册.pdf) · [HTML](03_实验材料/实验指导.html) |
| 逐项任务 | [L0–L7](03_实验材料/labs/) · [报告模板](03_实验材料/report_template.md) |
| 起始源码 | [student](student/README.md) · [源码包](04_Ferrum实验基础/Ferrum-cpp-student.tar.gz) |
| 题目指南 | [83 题目录](04_Ferrum实验基础/83题目录.html) · [源码内指南](student/docs/course/README.md) |
| 大纲与课件 | [课程大纲](00_课程大纲/) · [模块课件](01_课件（第6版）/) |
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

实验依次完成系统骨架、页与缓冲池、记录与目录、索引访问、基础执行器、查询引擎、锁与死锁、MVCC与快照、日志与恢复、系统综合验收。课内共40学时，配套课外开发与回归。

实验与测试安排见 [实验状态](04_Ferrum实验基础/实验状态.md)。2027 年赛事要求按当年规则执行。

## 资料与工具检查

从课程仓库根目录运行：

```sh
python -m unittest discover -s 03_实验材料/tests -v
python scripts/validate_modules.py
python scripts/validate_student.py
```

这些检查验证课程工具、材料身份和起始框架。题目实现使用实验手册指定的测试。

本仓库提供学习材料、起始框架和公开样例。作业提交到个人作业仓库。

教材课件、赛题及第三方代码保留原作者署名和各自许可；第三方材料适用各自许可。来源地址、固定版本和文件哈希随材料保存。

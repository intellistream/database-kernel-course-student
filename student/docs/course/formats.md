# 页与日志格式速查

[题目总览](README.md) · [共用约定](conventions.md)

这份速查提供读题所需的固定表示约束。使用给定字段编解码器构造测试输入；持久化字节不等于 C++ 结构体的内存布局。算法仍按各题的 `LAB` 区间实现。

## 存储页

数据页固定为 **4096 字节**，通用头为 **32 字节**，整数按小端序编码。具体声明见 [page.hpp](../../libs/storage/include/db/storage/page.hpp)。

| 字节偏移 | 宽度 | 内容 |
|---:|---:|---|
| 0 | 4 | 页魔数 |
| 4 | 2 | 版本 |
| 6 | 2 | 头部大小 |
| 8 | 4 | 保留的旧校验和字段 |
| 12 | 2 | 标志 |
| 14 | 2 | 页类型 |
| 16 / 18 / 20 | 各 2 | `lower` / `upper` / `special` |
| 22 | 2 | 保留字段 |
| 24 | 8 | 页 LSN |

合法初始化布局满足 `32 <= lower <= upper <= special <= 4096`。通用页视图提供原始字节访问，具体页类型的消费者负责相应语义检查。旧页校验和字段保留兼容布局，当前数据页路径不计算或验证它；不要把 WAL、目录等对象的 CRC 规则直接套到数据页。

页的空间组织属于 [S01.Q07](questions/S01.Q07.md)，回写与脏状态属于 [S01.Q05](questions/S01.Q05.md)。

## 堆槽位、RID 与元组

堆页从页头之后保存行指针，从另一端保存元组载荷。每个行指针为 4 字节，包含偏移或重定向目标、状态和长度。状态有 `unused`、`normal`、`redirect`、`dead`。`dead` 与可重新分配的 `unused` 有不同含义。

固定元组头为 24 字节，记录 `xmin`、`xmax`、`ctid`、标志、头长度和 command ID。`ctid` 指向一个 `(page, slot)`；它与当前事务是否看得见该版本是两个问题。

行载荷含 NULL 位图以及按 schema 确定的列存储。类型化 NULL 的载荷与非 NULL 使用相同的固定列位置。应使用 [tuple_codec.hpp](../../libs/access/include/db/access/tuple_codec.hpp) 和 [heap_page.hpp](../../libs/access/include/db/access/heap_page.hpp) 的声明及给定格式原语，避免自行另建一套格式。

槽位组织、物理变更、可见性与回收分别见 [S01.Q07](questions/S01.Q07.md)、[S01.Q09](questions/S01.Q09.md)、[S09.Q03](questions/S09.Q03.md)、[S09.Q07](questions/S09.Q07.md)。

## B+Tree 的格式入口

从 [btree_key.hpp](../../libs/access/include/db/access/btree_key.hpp) 阅读键布局，从 [btree_page.hpp](../../libs/access/include/db/access/btree_page.hpp) 阅读页、槽位和链接描述。页中保存持久化键与 RID/子页引用，测试应覆盖复合键、重复键、前缀和节点容量。

固定字段解释与“比较什么、定位哪里、如何重组”分开阅读。后者的学习入口是 [S03.Q01](questions/S03.Q01.md)，树下降与分裂分别属于 [S03.Q02](questions/S03.Q02.md)、[S03.Q03](questions/S03.Q03.md)。

## WAL 的几何与位置

WAL 页与数据页大小不同。权威常量见 [geometry.hpp](../../libs/wal/include/db/wal/geometry.hpp)，固定头见 [format.hpp](../../libs/wal/include/db/wal/format.hpp)。

| 对象 | 大小 |
|---|---:|
| WAL 页 | 8192 字节 |
| 逻辑段 | 16 MiB |
| 最大记录 | 64 MiB |
| 记录头 / 块头 | 40 / 24 字节 |
| WAL 页头 / 段头 | 32 / 64 字节 |
| control 头 / 完整映像 | 72 / 512 字节 |

LSN 零是无效哨兵，流的引导页占据 `[0, 8192)`。记录负载跨页时需要考虑页头，逻辑 LSN 与物理段文件偏移也不能不加区分。相关推导在 [环形缓冲](questions/S10.Q01.md)、[分段写入](questions/S10.Q02.md)、[流式读取](questions/S10.Q03.md) 中练习。

记录头含长度、前驱 LSN、XID、资源管理器、opcode、块引用数和 CRC。资源管理器声明见 [resource_manager.hpp](../../libs/wal/include/db/wal/resource_manager.hpp)，类型化记录接口见 [record.hpp](../../libs/wal/include/db/wal/record.hpp)。未知类型、截断和不支持版本需要按接口区分。

## 页镜像与差分

全页镜像重建为一个 4096 字节数据页。带空洞的标准页镜像需要恢复省略的范围；差分描述有界、不重叠的页内修改。压缩方法、编码长度和最终页内容都需要一致。

固定 PackBits 风格 RLE 的重复段长度范围为 3–130，字面量段为 1–128；只有至少节省 8 字节时才选择压缩。这些是兼容格式的约束，不是要求改变算法范围。具体接口见 [compression.hpp](../../libs/wal/include/db/wal/compression.hpp)、[delta.hpp](../../libs/wal/include/db/wal/delta.hpp)，实现引导见 [S10.Q05](questions/S10.Q05.md)。

## control 与 CRC

[control.hpp](../../libs/wal/include/db/wal/control.hpp) 定义系统身份、generation、检查点与 redo LSN、XID/OID 前沿和数据库状态。存在有效检查点时，`redo_lsn` 不得大于 `checkpoint_lsn`；没有检查点时不能单独出现 redo 位置。状态发布与文件权威性见 [S10.Q07](questions/S10.Q07.md)、[S10.Q12](questions/S10.Q12.md)。

WAL、目录和其他使用 CRC 的持久化对象各自规定覆盖范围及校验和字段偏移。给定的 [CRC 原语](../../libs/base/include/db/base/crc32c.hpp) 负责计算，格式调用方负责正确的覆盖范围和错误处理。固定 codec 的职责不包含替你决定何时同步或发布文件。

# 组件源码

各组件位于 `libs/<component>/`，公共声明在 `include/db/<component>/`，实现及私有头在 `src/`，组件测试在 `tests/`。CMake 目标通常为 `db_<component>`，别名为 `db::<component>`。

题目可能跨组件，一个文件也可能容纳多题。从 [题目指南](../docs/course/README.md) 的源码链接进入，再按题号搜索全部算法区间：

```sh
# 在仓库根目录执行；把题号替换为自己的选题。
rg -n 'LAB-S06\.Q02:.*-BEGIN' libs
```

[实验清单](../labs/manifest.json) 记录区间、函数与文件；[共用约定](../docs/course/conventions.md) 解释类型和所有权；[夹具说明](../docs/course/fixtures.md) 说明哪些依赖可由固定输入替代。

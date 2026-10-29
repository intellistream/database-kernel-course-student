# 用 Docker 开始练习

在宿主机安装并启动 Docker，然后在仓库根目录执行：

```sh
./dev.sh
```

这条命令构建开发镜像、挂载当前仓库，再执行 `scaffold`：完整编译默认目标和头文件检查，运行给定原语测试。首次运行需要联网下载镜像与软件包，后续复用镜像层和构建目录。Linux 可使用 Docker Engine；macOS 可使用 Docker Desktop；Windows 在 WSL2 的 Bash 中运行。

## 镜像里有什么

基础镜像选用官方 [Debian 13 trixie-slim](https://hub.docker.com/_/debian/)。默认只安装 GCC/g++ 14、CMake、Ninja、Python 3、`flock` 所在的 util-linux，以及指南使用的 ripgrep。GCC 14 在项目允许的 13–15 范围内。

安装使用 `--no-install-recommends`，并在同一层清理 APT 索引。镜像只保存工具链；源码、Git 历史和编译产物通过运行时挂载使用，不放进镜像。无需 Compose 或宿主机 C++ 编译器。

也可以直接使用 Dockerfile：

```sh
docker build -t ferrum-cpp-dev docker/
docker run --rm -it --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$PWD,target=/workspace" \
  -e LABS_JOBS=2 -e LABS_BUILD_DIR=/workspace/build/docker-gcc14-dev \
  ferrum-cpp-dev python3 labs/run.py scaffold
```

直接构建的默认目标同样是精简镜像。`dev.sh` 只是对构建和运行参数的包装。

本次在 Linux/amd64 验证的默认镜像为 **441,634,242 字节，约 442 MB 本地展开大小**。使用 GCC 14.2、CMake 3.31.6 和 Python 3.13.5，完整默认目标与头文件检查通过，27 个给定原语用例及 23 个运行器用例通过。软件源更新后，镜像体积和补丁版本可能变化。

## 日常命令

```sh
./dev.sh image                         # 只配置镜像，暂不编译
./dev.sh list --stage 06
./dev.sh unit S06.Q02                  # 实现本题后验收
./dev.sh plan --profile db-basic-rc
./dev.sh shell                        # 进入容器中的仓库
./dev.sh exec python3 -m unittest discover -s labs/tests -v
```

除 `image`、`shell` 和 `exec` 外，参数原样传给 `labs/run.py`。退出码也原样返回，未实现的题目不会被容器脚本改判为通过。题目指南中的 `python3 labs/run.py ...` 命令可以改写成 `./dev.sh ...`，或进入容器后照原命令执行。

容器以宿主用户的 UID/GID 运行。源码修改和生成的文件保留在宿主仓库，容器退出后自动删除。构建缓存位于 `build/docker-gcc14-dev`，与本机 GCC 构建目录分开；同一目录由实验运行器加锁。

默认用 2 个编译任务，内存充足时可增加：

```sh
LABS_JOBS=8 ./dev.sh
```

## 按需加入分析工具

需要格式化或静态分析时使用可选镜像层：

```sh
./dev.sh --tools shell
```

它从 [LLVM 官方软件源](https://apt.llvm.org/) 安装 `clang-format-22` 和 `clang-tidy-22`，默认镜像不会下载这部分内容。在该容器中开启项目的分析目标：

```sh
cmake -S . -B build/docker-analysis -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DDB_ENABLE_ANALYSIS_TARGETS=ON
cmake --build build/docker-analysis --target tidy-config-check
```

预设继承容器的 `CC=gcc-14` / `CXX=g++-14`，可直接执行 `./dev.sh exec cmake --preset gcc-debug` 或 `gcc-release`。普通预设不要求 Clang；静态分析使用 `./dev.sh --tools exec cmake --preset clang-tidy`。已有构建目录会缓存编译器，更换工具链时使用新的构建目录（配置时加 `-B` 指定）。

## 使用网络代理时

脚本把已有的 `HTTP_PROXY` / `HTTPS_PROXY` / `NO_PROXY` 及小写形式传给镜像构建。在 Linux 上发现本机回环代理时，构建阶段使用 host 网络；可通过 `FERRUM_BUILD_NETWORK` 显式覆盖。代理地址不写入 Dockerfile。

拉取基础镜像由 Docker 守护进程执行，仍需 Docker 自身能访问镜像仓库。若 `docker pull debian:trixie-slim` 报 DNS 或代理错误，先按 [Docker 代理配置](https://docs.docker.com/engine/daemon/proxy/) 配置守护进程，再重试。

更多选题和结果解释见 [开始练习](../docs/course/getting-started.md) 与 [运行器说明](../labs/README.md)。

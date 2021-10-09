# Matrix Judge System

一个支持 DOMjudge、Sicily Online Judge、Matrix Online Judge 的评测系统。
本评测系统分为两个项目：judge-system、runguard。其中 runguard 子项目的细节请到 runguard 文件夹查看。

## 安装运行

仅在 Debian 11 上测试过。

### 修改内核参数

使用本评测系统需要编辑内核参数，启用 cgroupv1 以及内存控制器。

编辑 `/etc/default/grub` 文件，在 `GRUB_CMDLINE_LINUX_DEFAULT` 和 `GRUB_CMDLINE_LINUX` 添加两个参数：`systemd.unified_cgroup_hierarchy=0 cgroup_enable=memory swapaccount=1` 并保存。

**然后运行 `sudo update-grub && sudo reboot` 以新参数启动系统。**

### 安装运行环境

接下来的命令会解压我们准备好的带有各种语言编译以及运行环境的容器镜像并解压到 `/chroot` 目录下，提供评测编译以及运行环境：

```bash
## 安装 OCI 镜像操作工具
sudo apt install -y skopeo git curl
sudo curl -o /usr/bin/umoci -L https://github.com/opencontainers/umoci/releases/download/v0.4.7/umoci.amd64
sudo chmod +x /usr/bin/umoci

## 展开镜像
skopeo copy docker://vmatrixdev/judge-env-aio oci:/tmp/judge-env-aio:latest
sudo umoci unpack --image /tmp/judge-env-aio:latest /tmp/judge-env-unpack && rm -rf /tmp/judge-env-aio
sudo rm -rf /chroot
sudo mv /tmp/judge-env-unpack/rootfs /chroot && sudo rm -rf /tmp/judge-env-unpack
sudo mkdir -p /chroot/dev/pts /chroot/sys /chroot/proc
```

如果你需要测试构建好的 chroot 环境是否正常，你可以通过以下方式进入 chroot 环境，执行 g++ 等命令来测试。

```bash
cd /opt/judge
sudo bash exec/chroot_run.sh -d /chroot
```

<details>
<summary>运行环境概览</summary>
一个 `chroot` 环境需要包含：

完整的 Debian 或 Ubuntu 环境：以允许 bash 脚本的运行。

以及各个编程语言的编译运行环境：
* curl
* git
* make（允许 Makefile 构建）
* golang
* rustc
* default-jdk-headless
* pypy（Python 2）
* python3
* clang
* ruby
* scala
* libboost-all-dev（支持评测系统、runguard 的运行时环境、允许选手调用 boost 库）
* cmake（支持题目使用 cmake 编译）
* libgtest-dev（支持 gtest 评测）
* gcc, gcc-10, g++, g++-10（允许使用 C++2a）
* gcc-multilib, g++-multilib（评测的 spj 可能是 32 位的）
* fp-compiler
* oclint（支持静态检查）
</details>

### 安装评测本体

你可以下载预编译好的 `.deb` 包并使用 `dpkg -i` 命令安装。会将程序以及辅助脚本安装到 `/opt/judge` 目录下，并创建 `/etc/judge` 默认配置以及配置名为 `judge.service` 的 systemd 服务。你可以按需修改配置和服务。

## 编译运行

目前开发脚本支持 Debian 11 (Bullseye) 快速搭建开发环境，首先安装依赖包以及第三方依赖（可以在最后添加 `-y` 选项跳过确认）：

```bash
sudo hack/install_packages.sh -y
hack/download_3rdparty.sh
```

安装完成后执行编译脚本即可编译 `judge-system` 项目，添加 `-DWITH_ADDRESS_SANITITZER=1` 可以编译出带内存安全检查的二进制（运行效率会比较低）：

```bash
hack/build.sh
```

## 架构

一台机器运行一个 judge-system，并启动一些 worker，worker 可以执行不同的任务：
  * 编译测试：负责执行编译，编译完成后的文件允许各个 worker 评测时读取（通过 aufs 来只读挂载）
  * 标准测试：有设计好的输入输出，由 special judge/diff 来对程序输出进行比较。
  * 随机测试：出题人提供输入数据的随机生成程序，在每次评测时调用该程序生成输入和输出，再进行标准测试。由于每次进行随机测试都要先调用出题人的数据生成器和标准程序，因此执行速度也比较慢。
  * 静态测试：静态测试选手程序是否存在问题，如代码格式不规范。
  * 内存测试：内存测试选手程序是否有内存泄漏情况，通常速度比较慢。
  * 单元测试：出题人提供单元测试对选手程序进行判分。

### check script

评测系统通过 check script 来实现测试任务的可扩展性。每个测试都通过 check script 实现评测机制。
目前有以下的 check script
  1. standard：允许评测普通题目，包括标准测试、随机测试
  2. interactive：允许测试交互题
  3. static: 允许评测静态测试

其中编译测试比较特殊，使用 compile.sh 来完成工作。

check script 通过返回值来确定评分，比如返回 42 表示 AC，43 表示 WA。对于静态测试、内存测试等需要直接返回外部程序的测试结果的（比如 oclint、valgrind 的输出），将这些评测结果经过必要的转换后（由 run script）放到指定文件夹中供评测系统读取并直接返回给评测服务端。
目前的测试中，使用标准测试数据还是随机测试数据是通过评测系统支持的。因此标准测试和随机测试的区别仅在测试数据的来源，都使用 standard 测试脚本。内存测试则可能使用标准测试数据或者随机测试数据，通过测试点依赖的特性来决定使用哪个测试数据（比如内存测试依赖了使用第 2 个标准测试数据的数据点，那么这个内存测试点也使用第 2 个标准测试数据；如果内存测试依赖了某个随机测试点，那么这个内存测试点使用随机测试点一样的测试数据）。

### 评测过程
评测系统从远程服务器上拉取选手的提交，并检查选手提交的测试点依赖关系（测试点依赖关系必须是森林）。并选出不依赖任何测试点的测试点（入度为 0）分发到评测队列中。评测系统开启后会启动 N 个评测客户端，每个评测客户端独立评测测试点。比如对于 OI 赛制的题目，该题的测试点依赖关系通常是：0 分的编译测试，以及 10 个标准测试点。这 10 个标准测试点将依赖编译测试，编译测试失败后这些标准测试点将失败。另一方面，这些标准测试点之间没有依赖关系，因此这 10 个标准测试点可以同时评测。对于有 10 个核心的评测机，我们开启 10 个评测客户端，该题的时限为 1 秒，那么最后的理论总评测时间上限是编译时间 + 1 秒（1 秒内所有核心将测试点都并发评测完成了）。对于 ACM 赛制的题目，这种题的依赖关系通常是之后的测试数据依赖前面的测试数据，一旦某个测试数据失败了，该题评测直接终止并返回结果，因此第 i + 1 个测试点将依赖第 i 个测试点。此时 ACM 赛制的题目将失去并发评测特性（因为链式依赖无法并发评测）。并发评测的优点是在低负载的情况下选手程序可以很快完成。

对于每个测试点，评测客户端将下载/生成相应的测试数据，编译随机测试生成器和标准程序，并保存到 CACHE_DIR 中。这样可以节省编译随机测试生成器、生成随机测试数据的时间。

## 代码
本项目的代码目录树如下：
```
.
├── runguard // runguard 子项目
│   ├── include // 头文件
│   └── src // 源文件
├── test // 测试环境搭建脚本以及数据
├── ext // 依赖项目
├── hack // 开发辅助脚本
├── docker // Docker 镜像创建脚本
├── exec // 评测脚本
│   ├── check   // 评测脚本，用来评测特定类型的测试点
│   ├── compare // 比较脚本，用来比较选手程序输出是否正确
│   ├── compile // 编译脚本，用来编译程序的，按语言分类
│   ├── run     // 运行脚本，用来执行选手程序的
│   └── utils   // 工具脚本，提供给其他脚本使用
├── src // judge-system 项目源文件
│   ├── server // 各个对接评测服务器的源代码
│   │   ├── forth // judge-system 4.0 JSON 格式提交的对接代码
│   │   ├── proto // judge-system 4.0 Protobuf 格式提交的对接代码
│   │   ├── mcourse // judge-system 2.0 的对接代码
│   │   └── sicily // Sicily OJ 的对接代码
│   └── judge // 评测的源代码，比如编程题、选择题的评测逻辑
└── include // judge-system 项目头文件
```

## 运行目录结构
本项目运行时需要提供
* EXEC_DIR: 备用的 executable 本地目录，如果没有服务器能提供 executable，那么从这里根据 id 找可用的
    ```
    EXEC_DIR
    ├── check // 测试执行的脚本
    │   ├── standard // 标准和随机测试的辅助测试脚本
    │   ├── static // 静态测试脚本
    │   └── memory // 内存检查（输入数据标准或随机）的帮助脚本
    ├── compare // 比较脚本
    │   ├── diff-all // 精确比较，如果有空白字符差异则返回 PE
    │   └── diff-ign-space // 忽略行末空格和文末空行的比较脚本，不会有 PE
    ├── compile // 编译脚本
    │   ├── c // C 语言程序编译脚本
    │   ├── cpp // C++ 语言程序编译脚本
    │   ├── make // Makefile 编译脚本
    │   ├── cmake // CMake 编译脚本
    │   └── ...
    └── run // 运行脚本
        └── standard // 标准运行脚本
    ```
* CACHE_DIR： 缓存目录，会缓存所有编译好的随机数据生成器、标准程序、生成的随机数据。
    第 N 组随机测试数据会调用随机数据生成器并传入 N，允许随机测试生成器根据随机测试数据组号生成数据。因此如果我们要缓存随机测试数据，必须根据随机测试数据组号分类，然后复用时在相同组号的随机测试数据中挑选并使用。
    ```
    CACHE_DIR
    ├── sicily // category id
    │   └── 1001 // problem id
    │       ├── standard // 标准程序的缓存目录（代码和可执行文件）
    │       ├── standard_data // 标准输入输出数据的缓存目录
    │       │   ├── 0 // 第 0 组标准测试数据
    │       │   │   ├── input // 当前测试数据组的输入数据文件夹
    │       │   │   └── output // 当前测试数据组的输出数据文件夹
    │       │   ├── 1 // 第 1 组标准测试数据
    │       │   │   ├── input // 当前测试数据组的输入数据文件夹
    │       │   │   └── output // 当前测试数据组的输出数据文件夹
    │       │   └── ...
    │       ├── compare // 比较器
    │       ├── random // 随机数据生成器的缓存目录（代码和可执行文件）
    │       └── random_data // 随机输入输出数据的缓存目录
    │           ├── 0 // 第 0 组随机测试数据，该文件夹内的测试数据可以被随机复用
    │           │   ├── 0 // 该组测试数据生成并缓存的第 0 组测试数据
    │           │   │   ├── input // 当前测试数据组的输入数据文件夹
    │           │   │   └── output // 当前测试数据组的输出数据文件夹
    │           │   ├── 1 // 该组测试数据生成并缓存的第 1 组测试数据
    │           │   │   ├── input // 当前测试数据组的输入数据文件夹
    │           │   │   └── output // 当前测试数据组的输出数据文件夹
    │           │   └── ...
    │           ├── 1 // 第 1 组随机测试数据
    │           │   └── ...
    │           └── ...
    ├── moj
    └── mcourse
    ```
* DATA_DIR：数据缓存目录，如果设置了拷贝数据选项，那么评测系统将在 CACHE_DIR 内存储的数据拷贝到 DATA_DIR 中保存，如果将 DATA_DIR 放进内存盘将可以加速选手程序的 IO 性能，避免 IO 瓶颈
    ```
    DATA_DIR
    ├── ABCDEFG // 随机生成的 uuid，表示一组测试数据，与标准数据和随机数据存储方式一致
    │   ├── input // 当前测试数据组的输入数据文件夹
    │   └── output // 当前测试数据组的输出数据文件夹
    └── ...
    ```
* RUN_DIR: 运行目录，该目录会临时存储所有的选手程序代码、选手程序的编译运行结果，建议将 RUN_DIR 放进内存盘。
    ```
    RUN_DIR
    ├── sicily // category id
    │   └── 1001 // problem id
    │       └── 5100001 // submission id, workdir
    │           ├── compile // 选手程序的代码和编译目录
    │           │   ├── main.cpp // 选手程序的主代码（示例）
    │           │   ├── run // 选手程序的执行入口（可能是生成的运行脚本）
    │           │   ├── compile.out // 编译器的输出
    │           │   ├── compile.meta // 编译器的运行信息
    │           │   └── (Makefile) // 允许 Makefile
    │           ├── run-compile // 编译时使用的运行目录
    │           │   ├── work // 构建的根目录
    │           │   ├── merged // 构建好的 chroot 环境
    │           │   └── runguard_command // runguard 在执行用户程序前需要执行的命令
    │           ├── run-[client_id] // 选手程序的运行目录，包含选手程序输出结果
    │           │   ├── run // 运行路径
    │           │   │   └── testdata.out // 选手程序的 stdout 输出
    │           │   ├── feedback // 比较器结果路径
    │           │   │   ├── judgemessage.txt // 比较器的输出信息
    │           │   │   ├── judgeerror.txt // 比较器的错误信息
    │           │   │   └── score.txt // 如果比较器支持部分分，那么这里将保存部分分数
    │           │   ├── work // 构建的根目录
    │           │   ├── merged // 构建好的 chroot 环境
    │           │   │   ├── judge // 挂载 compile 文件夹、输入数据文件夹、run 文件夹
    │           │   │   ├── testin // 挂载输入数据文件夹
    │           │   │   ├── testout // 挂载输出数据文件夹
    │           │   │   ├── compare // 挂载比较脚本文件夹
    │           │   │   └── run // 挂载运行脚本文件夹
    │           │   ├── program.err // 选手程序的 stderr 输出
    │           │   ├── program.meta // 选手程序的运行信息
    │           │   ├── compare.err // 比较程序的 stderr 输出
    │           │   ├── compare.meta // 比较程序的运行信息
    │           │   └── system.out // 检查脚本的日志
    │           └── run-...
    ├── moj
    └── mcourse
    ```
* CHROOT_DIR: 解压 OCI Image 中的 rootfs 产生的 Linux 子系统环境。
    ```
    CHROOT_DIR
    ├── usr
    ├── bin
    └── ...
    ```


## 调试

### 环境准备

1. 照着部署方案前四步做。
2. 打开 VSCode，安装 C/C++ Extension 和 CMake Tools（注意是微软出的那个）。
3. CMake Tools 会弹出对话框进行 Configure，完成之后点击 VSCode 底栏的 Build 即可完成构建。
4. 点击 Run 栏目，选择一项运行调试。或者使用 gdb attach 的方式附加到运行中的进程进行调试。

### 配置调试环境

在 `test/2.0` 文件夹中有评测 2.0 API 的测试环境，通过
```bash
python test.py [config.json] [detail.json]
```

命令向本地数据库添加提交。接下来你只需要打开评测系统即可执行测试提交

### vscode 调试方法

建议是使用 gdb attach 的方法将调试器附加到进程上进行调试。


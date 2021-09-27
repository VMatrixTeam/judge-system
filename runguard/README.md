# Runguard

Runguard 程序用来监控受控程序的时间及内存的使用状况，并限制系统访问权限。

## 运行前执行脚本

`runguard` 支持运行用户程序前预先执行指定文件，并确保指定文件运行在新的挂载点命名空间内。比如，我们有个文件 `runguard_command` 如下：
```bash
#!/bin/bash
mount --bind -o ro "$CHROOTDIR" run
```
这个脚本执行了 `mount` 命令，由于 runguard 会先创建新的挂载点命名空间，因此一旦用户程序退出后 `runguard` 也将退出，此时我们特别创建的挂载点将被操作系统自动删除，避免了我们手工解除挂载点的麻烦以及可能发送的错误。
此外，如果我们需要还原 runguard 的运行现场时，可以直接运行 runguard 命令，从而省去了手动挂载和解挂载的麻烦。（比如某个提交运行失败，我们需要还原提交的运行现场，如果不使用该功能，我们必须先将需要执行的 mount 命令人工计算出来并执行，之后还需要人工解挂载。但如果使用该功能，我们可以直接使用已经计算好的 mount 脚本，并且 runguard 测试完成之后会自动解挂载）

## 重定向输入输出

`runguard` 目前支持通过 `--standard-input-file`、`--standard-output-file`、`--standard-error-file` 重定向标准文件。需要注意的是，`runguard` 不会将用户程序的输入输出重定向到 `runguard` 自己，也就是说你不能在不通过 `runguard` 提供的命令重定向标准文件的情况下从 `runguard` 的标准输出读取用户程序输出。

## 资源限制

`runguard` 使用 `cgroup` 和 `rlimit` 来限制程序运行资源。`cgroup` 负责限制程序可以使用的 CPU 核心数和内存；`rlimit` 负责限制程序的文件读写量、进程数，并将栈空间设置为无穷大。

### 时间限制

`runguard` 目前支持通过 `--wall-time` 和 `--cpu-time` 来限制用户程序运行时间，但需要注意的是 `runguard` 会在仅指定 `--cpu-time` 的情况下自动指定 3 倍的 wall-time 时间限制。原因是仅限制 cpu-time 时若选手程序执行 sleep 将导致 runguard 无法结束。强制添加 wall-time 将避免这个问题。

## 环境变量

`runguard` 默认将清空用户程序的环境变量，仅提供 `PATH` 和 `HOME`，一般情况下都不需要更改此设置。你可以通过 `-V` 命令添加用户程序的环境变量。

## 权限限制

`runguard` 提供了 `seccomp` 和 `unshare` 两种限制程序运行权限的方法。
* `seccomp` 法将通过限制程序的系统调用来限制程序运行权限，这种方法仅适用于 C/C++/Rust 等系统编程语言，因为其他语言的系统调用无法预测。该方法的优势在于快速，`seccomp` 没有冷启动的时间。
* `unshare` 法将通过 Linux 命名空间技术（容器技术）隔离程序，比如隔离程序的网络命名空间、IPC 命名空间来避免程序间通信和访问网络。`unshare` 法速度比较慢，尤其是创建网络命名空间需要数百毫秒的代价。

上面两种方法并不会限制程序的文件读写行为，`runguard` 允许通过 `chroot` 法来限制程序的文件读写权限，允许程序在 `chroot` 内任意读写。

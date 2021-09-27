#!/bin/sh

# 将 CHROOTDIR 文件夹挂载到目标目录下
#    chroot_setup.sh <CHROOTDIR> <DEST> <OP>
#    CHROOTDIR: 已经准备好的 chroot 环境
#    DEST:      要挂载到的路径
#    OP: check/start/stop
#
# 该脚本用于为解释型语言准备基础环境
# 
# 我们需要准备程序运行必要的最小环境，而环境内的所有数据
# 从 chroot_make.sh 来构建

set -e

CHROOTDIR="$1"
DEST="$2"

# $1: unmount mountpoint
force_umount() {
    set +e
    if ! $GAINROOT umount "$1" < /dev/null; then
        >&2 echo "umount '$1' did not succeed, trying harder"
        if ! $GAINROOT umount -f -vvv "$1" < /dev/null; then
            >&2 echo "umount '$1' failed"
#           >&2 $GAINROOT lsof +c 15 +D "$1"
            exit 1
        fi
    fi
    set -e
}

chroot_check() {
    local CHROOTDIR

    CHROOTDIR="$1"
    
    if [ ! -d "$CHROOTDIR" ]; then
        >&2 echo "chroot dir '$CHROOTDIR' does not exist"
        exit 2
    fi

    # /debootstrap 会在 debootstrap 完成 chroot 构建之后删除
    # 如果文件夹仍然存在，说明 debootstrap 未能正常完成 chroot 构建
    # 则不能进行绑定
    if [ -d "$CHROOTDIR/debootstrap" ]; then
        >&2 echo "chroot dir '$CHROOTDIR' incomplete, rerun chroot_make.sh"
        exit 2
    fi
}

chroot_start() {
    local CHROOTDIR DEST

    CHROOTDIR="$1"
    DEST="$2"

    # mount proc filesystem (needed by Java for /proc/self/stat)
    mkdir -p "$DEST/proc"
    $GAINROOT mount -n --bind /proc "$DEST/proc" < /dev/null

    # 在 Docker 内 mount 会出现字符设备无法访问的问题
    # c????????? ? ?    ?       ?            ? full
    # c????????? ? ?    ?       ?            ? null
    # c????????? ? ?    ?       ?            ? ptmx
    # drwxr-xr-x 2 root root 4096 Sep  1 10:35 pts
    # c????????? ? ?    ?       ?            ? random
    # drwxr-xr-x 2 root root 4096 Sep  1 10:35 shm
    # lrwxrwxrwx 1 root root   15 Sep  1 10:35 stderr -> /proc/self/fd/2
    # lrwxrwxrwx 1 root root   15 Sep  1 10:35 stdin -> /proc/self/fd/0
    # lrwxrwxrwx 1 root root   15 Sep  1 10:35 stdout -> /proc/self/fd/1
    # c????????? ? ?    ?       ?            ? tty
    # c????????? ? ?    ?       ?            ? urandom
    # c????????? ? ?    ?       ?            ? zero
    # 因此这里直接重新创建字符设备
    mkdir -p "$DEST/dev"
    rm -f "$DEST/dev/full" && mknod -m 666 "$DEST/dev/full" c 1 7
    rm -f "$DEST/dev/null" && mknod -m 666 "$DEST/dev/null" c 1 3
    rm -f "$DEST/dev/ptmx" && mknod -m 666 "$DEST/dev/ptmx" c 5 2
    rm -f "$DEST/dev/random" && mknod -m 666 "$DEST/dev/random" c 1 8
    rm -f "$DEST/dev/tty" && mknod -m 666 "$DEST/dev/tty" c 5 0
    rm -f "$DEST/dev/urandom" && mknod -m 666 "$DEST/dev/urandom" c 1 9
}

chroot_stop() {
    local CHROOTDIR DEST

    CHROOTDIR="$1"
    DEST="$2"

    force_umount "$DEST/proc"

    rm -f "$DEST/dev/full"
    rm -f "$DEST/dev/null"
    rm -f "$DEST/dev/ptmx"
    rm -f "$DEST/dev/random"
    rm -f "$DEST/dev/tty"
    rm -f "$DEST/dev/urandom"
}

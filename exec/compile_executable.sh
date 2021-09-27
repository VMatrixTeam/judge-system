#!/bin/sh
#
# 编译 executable 的脚本
#
# 用法：$0 <workdir> <chrootdir>
#
# <cpuset_opt> 编译使用的 CPU 集合
# <workdir> executable 的文件夹，比如 /tmp/cache/executable/cpp
#                   编译生成的可执行文件在该文件夹中，编译器输出也在该文件夹中
# <chrootdir> executable 编译时的运行环境
#
# 本脚本直接调用 executable 的 build 脚本进行编译
#
# 环境变量：
#   $RUNGUARD
#   $RUNUSER
#   $RUNGROUP
#   $SCRIPTMEMLIMIT 编译脚本运行内存限制
#   $SCRIPTTIMELIMIT 编译脚本执行时间
#   $SCRIPTFILELIMIT 编译脚本输出限制
#   $E_COMPILER_ERROR 编译失败返回码
#   $E_INTERNAL_ERROR 内部错误返回码

set -e
trap error EXIT

cleanexit ()
{
    trap - EXIT

    logmsg $LOG_DEBUG "exiting, code = '$1'"
    exit $1
}

. "$JUDGE_UTILS/utils.sh" # runcheck
. "$JUDGE_UTILS/logging.sh" # logmsg, error
. "$JUDGE_UTILS/chroot_setup.sh" # chroot_setup
. "$JUDGE_UTILS/runguard.sh" # read_metadata

CPUSET=""
CPUSET_OPT=""
OPTIND=1
while getopts "n:" opt; do
    case $opt in
        n)
            CPUSET="$OPTARG"
            ;;
        :)
            >&2 echo "Option -$OPTARG requires an argument."
            ;;
    esac
done

shift $((OPTIND-1))
[ "$1" == "--" ] && shift

if [ -n "$CPUSET" ]; then
    CPUSET_OPT="-P $CPUSET"
fi

[ $# -ge 1 ] || error "Not enough arguments."
WORKDIR="$1"; shift
CHROOTDIR="$1"; shift

if [ ! -d "$WORKDIR" ] || [ ! -w "$WORKDIR" ] || [ ! -x "$WORKDIR" ]; then
    error "Work directory is not found or not writable: $WORKDIR"
fi

[ -x "$RUNGUARD" ] || error "runguard not found or not executable: $RUNGUARD"

cd "$WORKDIR"
mkdir -p "$WORKDIR/compile"
chmod a+rwx "$WORKDIR/compile"
cd "$WORKDIR/compile"
touch compile.meta

if [ -n "$VERBOSE" ]; then
    exec > >(tee compile.out) 2>&1
else
    exec >>compile.out 2>&1
fi

RUNDIR="$WORKDIR/run-compile"
mkdir -p "$RUNDIR"
chmod a+rwx "$RUNDIR"

mkdir -m 0755 -p "$RUNDIR/work"
mkdir -m 0777 -p "$RUNDIR/work/judge"
mkdir -m 0777 -p "$RUNDIR/ofs"
mkdir -m 0777 -p "$RUNDIR/ofs/merged"
mkdir -m 0755 -p "$RUNDIR/merged"

cat > "$RUNDIR/runguard_command" << EOF
#!/bin/bash
. "$JUDGE_UTILS/chroot_setup.sh"

mount -t overlay overlay -olowerdir="$CHROOTDIR",upperdir="$RUNDIR/work",workdir="$RUNDIR/ofs/merged" "$RUNDIR/merged"
mount --bind "$WORKDIR/compile" "$RUNDIR/merged/judge"

chroot_start "$CHROOTDIR" merged
EOF
chmod +x "$RUNDIR/runguard_command"

# 调用 runguard 来执行编译命令
runcheck $GAINROOT "$RUNGUARD" ${DEBUG:+-v} $CPUSET_OPT -c \
        --preexecute "$RUNDIR/runguard_command" \
        --root "$RUNDIR/merged" \
        --work /judge \
        --user "$RUNUSER" \
        --group "$RUNGROUP" \
        --memory-limit "$SCRIPTMEMLIMIT" \
        --file-limit "$SCRIPTFILELIMIT" \
        --wall-time "$SCRIPTTIMELIMIT" \
        --standard-error-file compile.tmp \
        --out-meta compile.meta \
        -- \
        "./build"

# 删除挂载点，因为我们已经确保有用的数据在 $WORKDIR/compile 中，因此删除挂载点即可。
rm -rf "$RUNDIR"

$GAINROOT chown -R "$(id -un):" "$WORKDIR/compile"
chmod -R go-w "$WORKDIR/compile"

# 检查是否编译超时，time-result 可能为空、soft-timelimit、hard-timelimit，空表示没有超时
if grep '^time-result: .*timelimit' compile.meta >/dev/null 2>&1; then
    echo "Compilation aborted after $SCRIPTTIMELIMIT seconds."
    cat compile.tmp
    cleanexit ${E_COMPILER_ERROR:--1}
fi

# 检查是否编译器出错/runguard 崩溃
if [ $exitcode -ne 0 ]; then
    echo "Compilation failed with exitcode $exitcode."
    cat compile.tmp
    if [ ! -s compile.meta ]; then
        printf "\n****************runguard crash*****************\n"
    fi
    cleanexit ${E_INTERNAL_ERROR:--1}
fi

# 检查是否成功编译出程序
if [ ! -f run ] || [ ! -x run ]; then
    echo "Compilation failed: executable is not created."
    cat compile.tmp
    cleanexit ${E_COMPILER_ERROR:--1}
fi

cat compile.tmp
cleanexit 0

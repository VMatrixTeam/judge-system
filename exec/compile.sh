#!/bin/bash
#
# 编译脚本
#
# 用法：$0 <compile_script> <chrootdir> <workdir> <source_files> <extra compile args...>
#
# <compile_script>  编译的脚本
# <chrootdir>       chroot directory
# <workdir>         本次评测的工作文件夹，比如 /tmp/judger0/judging_5322222/
#                   编译生成的可执行文件在该文件夹中，编译器输出也在该文件夹中
# <source_file>     需要参与编译的源文件，用冒号隔开。注意：assist file 不会传递给编译脚本
# <extra compile args...> 额外传递给编译脚本的编译参数
#
# 评测系统通过调用不同的编译脚本来实现多语言支持。
#
# 编译脚本的调用参数格式为：
#
#   <compile_script> <dest> <source_file> <extra compile args...>
#
#   <dest>        可执行文件的文件名，如 main, main.jar
#   <source_file> 需要参与编译的源文件，用冒号隔开。
#   <extra compile args...> 额外传递给编译器的编译参数
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

    #chmod go= "$WORKDIR/compile"
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
    CPUSET_OPT="--cpuset $CPUSET"
fi

[ $# -ge 3 ] || error "Not enough arguments."
COMPILE_SCRIPT="$1"; shift
CHROOTDIR="$1"; shift
WORKDIR="$1"; shift
SOURCE_FILES="$1"; shift
IFS=':' read -ra SOURCE_FILES_SPLITTED <<< "$SOURCE_FILES"

if [ ! -d "$WORKDIR" ] || [ ! -w "$WORKDIR" ] || [ ! -x "$WORKDIR" ]; then
    error "Work directory is not found or not writable: $WORKDIR"
fi

[ -x "$COMPILE_SCRIPT" ] || error "Compile script not found or not executable: $COMPILE_SCRIPT"
[ -x "$RUNGUARD" ] || error "runguard not found or not executable: $RUNGUARD"

RUNNETNS_OPT=""
[ ! -z "$RUNNETNS" ] && RUNNETNS_OPT="--netns=$RUNNETNS"

cd "$WORKDIR"
RUNDIR="$WORKDIR/run-compile"

mkdir -p "$WORKDIR/compile"
cd "$WORKDIR/compile"

if [ -n "$VERBOSE" ]; then
    exec > >(tee compile.out) 2>&1
else
    exec >>compile.out 2>&1
fi

$GAINROOT chmod -R 773 "$WORKDIR/compile"
$GAINROOT chown -R "$RUNUSER" "$WORKDIR/compile"
touch compile.meta

for src in "${SOURCE_FILES_SPLITTED[@]}"; do
    [ -r "$WORKDIR/compile/$src" ] || error "source file not found: $src"
done

if [ ! -z "$ENTRY_POINT" ]; then
    ENVIRONMENT_VARS="-V ENTRY_POINT=$ENTRY_POINT"
fi

mkdir -p "$RUNDIR"; chmod 773 "$RUNDIR"

chmod -R +x "$COMPILE_SCRIPT"

mkdir -p "$RUNDIR/work"; chmod 773 "$RUNDIR/work"
mkdir -p "$RUNDIR/work/judge"; chmod 773 "$RUNDIR/work/judge"
mkdir -p "$RUNDIR/work/compile"; chmod 773 "$RUNDIR/work/compile"
mkdir -p "$RUNDIR/merged"; chmod 773 "$RUNDIR/merged"
mkdir -p "$RUNDIR/ofs"; chmod 773 "$RUNDIR/ofs"

cat > "$RUNDIR/runguard_command" << EOF
#!/bin/bash
. "$JUDGE_UTILS/chroot_setup.sh"
. "$JUDGE_UTILS/logging.sh"

mount -t overlay overlay -olowerdir="$CHROOTDIR",upperdir="$RUNDIR/work",workdir="$RUNDIR/ofs" "$RUNDIR/merged"
mount --bind "$WORKDIR/compile" "$RUNDIR/merged/judge"
mount --bind -o ro "$COMPILE_SCRIPT" "$RUNDIR/merged/compile"

chroot_start "$CHROOTDIR" "$RUNDIR/merged"
EOF
chmod +x "$RUNDIR/runguard_command"

logmsg $LOG_DEBUG "Compiling $(pwd) with compile script $COMPILE_SCRIPT"

# 调用 runguard 来执行编译命令
runcheck $GAINROOT "$RUNGUARD" ${DEBUG:+-v} $CPUSET_OPT $RUNNETNS_OPT -c \
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
        $ENVIRONMENT_VARS -- \
        "/compile/run" run "$SOURCE_FILES" "$@"

logmsg $LOG_DEBUG "runguard exited with exitcode $exitcode"

$GAINROOT chown -R "$(id -un):" "$WORKDIR/compile"
chmod -R go-w+x "$WORKDIR/compile"

# 检查是否编译器出错/runguard 崩溃
if [ ! -s compile.meta ]; then
    echo "Runguard exited with code $exitcode and 'compile.meta' is empty, it likely crashed."
    cat compile.tmp
    cleanexit ${E_INTERNAL_ERROR:--1}
fi

if grep -E '^internal-error: .+$' compile.meta >/dev/null 2>&1; then
    echo "Internal Error"
    cat compile.tmp
    cleanexit ${E_INTERNAL_ERROR:-1}
fi

# Check if the compile script auto-detected the entry point, and if
# so, store it in the compile.meta for later reuse, e.g. in a replay.
grep '[Dd]etected entry_point: ' compile.tmp | sed 's/^.*etected //' >>compile.meta

logmsg $LOG_DEBUG "checking compilation exit-status"
read_metadata compile.meta

# 检查是否编译超时，time-result 可能为空、soft-timelimit、hard-timelimit，空表示没有超时
if grep '^time-result: .*timelimit' compile.meta >/dev/null 2>&1; then
    echo "Compilation aborted after $SCRIPTTIMELIMIT seconds."
    cat compile.tmp
    cleanexit ${E_COMPILER_ERROR:--1}
fi

if [ $progexit -ne 0 ]; then
    echo "Compilation failed with exitcode $progexit."
    cat compile.tmp
    cleanexit ${E_COMPILER_ERROR:--1}
fi

# 检查是否成功编译出程序
if [ ! -f run ] || [ ! -x run ]; then
    echo "Compilation failed: executable is not created."
    cat compile.tmp
    cleanexit ${E_COMPILER_ERROR:--1}
fi

cat compile.tmp
cleanexit 0

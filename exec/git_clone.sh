#!/bin/bash
#
# Git clone 脚本
#
# 用法：$0 <url> <commit> <workdir>
#
# <url>     Git 仓库地址，可能包含用户名和密码
# <commit>  Git 仓库 commit id
# <workdir> 代码工作目录，git 仓库将被克隆到 <workdir>/compile 下
#
# 环境变量：
#   $SCRIPTTIMELIMIT git clone 执行时间
#   $GIT_USERNAME    git clone 用户名
#   $GIT_PASSWORD    git clone 密码

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

LOGLEVEL=$LOG_DEBUG
PROGNAME="$(basename "$0")"

if [ -n "$DEBUG" ]; then
    export VERBOSE=$LOG_DEBUG
else
    export VERBOSE=$LOG_ERR
fi

[ $# -ge 3 ] || error "Not enough arguments."
URL="$1"; shift
COMMIT="$1"; shift
WORKDIR="$1"; shift

if [ ! -d "$WORKDIR" ] || [ ! -w "$WORKDIR" ] || [ ! -x "$WORKDIR" ]; then
    error "Work directory is not found or not writable: $WORKDIR"
fi

cd "$WORKDIR"

mkdir -p "$WORKDIR/compile"
$GAINROOT chmod -R 777 "$WORKDIR/compile"
$GAINROOT chown -R "$RUNUSER" "$WORKDIR/compile"

cd "$WORKDIR/compile"

git init
git remote add origin "$URL"

if [ -n "$VERBOSE" ]; then
    exec > >(tee compile.out) 2>&1
else
    exec >>compile.out 2>&1
fi

cat > "$WORKDIR/pass.sh" << EOF
#!/bin/bash
echo username=$GIT_USERNAME
echo password=$GIT_PASSWORD
EOF

git config credential.helper "/bin/bash '$(realpath "$WORKDIR/pass.sh")'"
timeout --signal=HUP "$SCRIPTTIMELIMIT" git pull origin master
git reset --hard "$COMMIT"
# timeout --signal=HUP "$SCRIPTTIMELIMIT" git fetch origin "$COMMIT" --depth=1
# git checkout FETCH_HEAD

rm -f "$WORKDIR/pass.sh"

cleanexit 0

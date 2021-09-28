#!/bin/bash
#
# 在裸机上的运行脚本
# 必须以特权模式运行

set -e

DIR="$(realpath "$(dirname "$0")")"

MOJ_OPT=""
if [ -n "$MOJ_CONF" ]; then
    MOJ_OPT="--enable-3 $MOJ_CONF"
fi

MCOURSE_OPT=""
if [ -n "$MCOURSE_CONF" ]; then
    MCOURSE_OPT="--enable-2 $MCOURSE_CONF"
fi

FORTH_OPT=""
if [ -n "$FORTH_CONF" ]; then
    FORTH_OPT="--enable $FORTH_CONF"
fi

SICILY_OPT=""
if [ -n "$SICILY_CONF" ]; then
    SICILY_OPT="--enable-sicily $SICILY_CONF"
fi

if [ -z "$WORKDIR" ]; then
    WORKDIR="/tmp/judge"
fi

mkdir -p /var/log/judge
mkdir -p "$WORKDIR/cache"
mkdir -p "$WORKDIR/run"

bash "$DIR/exec/create_cgroups.sh" judge
bash "$DIR/exec/create_net_namespace.sh" judge

ENABLE_OPT=""
if [ -n "$MODE" ]; then
    ENABLE_OPT="--enable=$DIR/config/$MODE"
fi

if [ -z "$CORES" ]; then
    export CORES=$(python3 $DIR/script/core_filter.py)
fi

export GLOG_log_dir=/var/log/judge
export GLOG_alsologtostderr=1
export GLOG_colorlogtostderr=1
export BOOST_log_dir=/var/log/judge
export ELASTIC_APM_TRANSPORT_CLASS="elasticapm.transport.http.Transport"
export CACHEDIR="$WORKDIR/cache"
export RUNDIR="$WORKDIR/run"
export CHROOTDIR="/chroot"
export CACHERANDOMDATA=2
export RUNUSER=judge
export RUNGROUP=judge
export RUNNETNS=judge
export SCRIPTTIMELIMIT=30
echo "Running on $CORES"
echo "Command = $DIR/bin/judge-system $ENABLE_OPT $MOJ_OPT $MCOURSE_OPT $FORTH_OPT $SICILY_OPT $@"
"$DIR/bin/judge-system" "$ENABLE_OPT" $MOJ_OPT $MCOURSE_OPT $FORTH_OPT $SICILY_OPT "$@"


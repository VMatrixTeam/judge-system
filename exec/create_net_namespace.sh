#!/bin/bash
#
# 本脚本用于创建网络命名空间
# 该脚本必须在每次开机后执行一次，你可以把本脚本设置为启动脚本
# 用法： $0 <network namespace name>
# 注意，不要在网络命名空间内执行 ip link set lo up，否则命名空间内将可以走 loopback 通信

error() { echo "$*" 1>&2; exit 1; }

[ $# -ge 1 ] || error "network namespace name required"

NETNSNAME=$1; shift
if ! ip netns list | grep -E "^$NETNSNAME\$" >/dev/null 2>&1; then
    ip netns add "$NETNSNAME"
fi

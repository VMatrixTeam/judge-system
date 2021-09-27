#!/bin/bash

# 特权模式下运行

EXEC_DIR=./exec

useradd -d /nonexistent -U -M -s /bin/false judge

bash $EXEC_DIR/create_cgroups.sh judge


import sys
import os
import re
from functools import reduce

def extract(cores):
    if '-' in cores:
        s = cores.split('-')
        return list(range(int(s[0]), int(s[1]) + 1))
    else:
        return [int(cores)]

cgroup_cores = ""
with open('/proc/self/status', 'r') as fin:
    for line in fin:
        line = re.findall(r'Cpus_allowed_list\:\s+(.*)', line)
        if line:
            cgroup_cores = line[0]

cgroup_cores = reduce(list.__add__, [extract(x) for x in cgroup_cores.split(",")])

cpumap = dict()
with open('/proc/cpuinfo', 'r') as fin:
    processor = None
    physical_id = None
    core_id = None
    for line in fin:
        pair = [x.strip() for x in line.split(':')]
        if pair[0] == 'processor':
            processor = int(pair[1])
        elif pair[0] == 'physical id':
            physical_id = pair[1]
        elif pair[0] == 'core id':
            core_id = pair[1]
            cpumap[processor] = f"{physical_id}, {core_id}"
            processor = None
            physical_id = None
            core_id = None

met = set()
cores = []
for c in cgroup_cores:
    if c in cpumap and cpumap[c] not in met:
        met.add(cpumap[c])
        cores += [str(c)]

print(','.join(cores))

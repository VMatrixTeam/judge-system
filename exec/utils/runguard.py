import sys, argparse, os

class Range(object):
    def __init__(self, s):
        super.__init__(self)

        splitted = s.split(':')
        left = 0
        right = 0
        if len(splitted) >= 1:
            left = int(splitted[0])
        if len(splitted) >= 2:
            right = int(splitted[1])
        
        if left > right or left < 0 or right < 0:
            raise ValueError('must be in form <soft:positive number>:<hard:positive number>')
        
        self.soft = left
        self.hard = right

def main():
    parser = argparse.ArgumentParser(prog='runguard', description='Converts command line arguments into runc config.json')
    parser.add_argument('-r', '--rootless', action='store_true', help='start container ')
    parser.add_argument('-r', '--root', action='store', type=str, help='rootfs', required=True)
    parser.add_argument('-u', '--user', action='store', type=int, help='specifies user id', required=True)
    parser.add_argument('-g', '--group', action='store', type=int, help='specifies group id', required=True)
    parser.add_argument('--netns', action='store', type=str, help='specifies network namespace file name to inherit namespace from')
    parser.add_argument('-d', '--work-dir', action='store', type=str, help='run command in specified network namespace, if not specified, runguard will unshare network namespace by default')
    parser.add_argument('-T', '--wall-time', action='store', type=Range, help='kill command after wall time clock seconds (floating point is accpeted)')
    parser.add_argument('-t', '--cpu-time', action='store', type=int, help='kill command after CPU time clock seconds (floating point is accepted)', default='0')
    parser.add_argument('-m', '--memory-limit', action='store', type=int, help='maximum kilo-bytes that all processes in container may consume')
    parser.add_argument('-f', '--file-limit', action='store', type=int, help='maximum kilo-bytes that all processes in container can write', default='0')
    parser.add_argument('-p', '--nproc', action='store', type=int, help='maximum number of processes may run in container', default='0')
    parser.add_argument('-P', '--cpuset', action='store', type=str, help='processor IDs that processes in container may run on')
    parser.add_argument('--allowed-syscall', action='store', type=str, help='whitelisted syscalls that processes in container may invoke')
    parser.add_argument('-c', '--no-core-dumps', action='store_true', help='disable core dumps in container')
    parser.add_argument('-E', '--environment', action='store_true', help='pass through exisitng environment variables into container (exluding PATH)')
    parser.add_argument('-V', '--variable', action='append', type=str, help='set additional environment variable (eg. -Vkey1=value1 -Vkey2=value2)')
    parser.add_argument('commands', metavar='N', type=str, nargs='+', help='command line of program to run in container')
    args = parser.parse_args()

    env = []
    if args.environment:
        env += os.environ
    if args.variable:
        print(args.variable)
    env = [x for x in env if not x.startsWith('PATH=')]
    env += ['PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin']

    config = {
        'ociVersion': '1.0.1-dev',
        'process': {
            'terminal': False,
            'args': args.commands,
            'env': env,
            'cwd': args.work_dir or '/',
            'capabilities': [

            ],
            'rlimits': ([
                {
                    'type': 'RLIMIT_AS',
                    'hard': args.memory_limit * 1024,
                    'soft': args.memory_limit * 1024,
                },
                {
                    'type': 'RLIMIT_DATA',
                    'hard': args.memory_limit * 1024,
                    'soft': args.memory_limit * 1024,
                }
            ] if args.memory_limit > 0 else []) + ([
                {
                    'type': 'RLIMIT_FSIZE',
                    'hard': args.file_limit,
                    'soft': args.file_limit,
                }
            ] if args.file_limit > 0 else []) + ([
                {
                    'type': 'RLIMIT_CPU',
                    'hard': args.cpu_limit,
                    'soft': args.cpu_limit,
                }
            ] if args.cpu_limit > 0 else []) + ([
                {
                    'type': 'RLIMIT_NPROC',
                    'hard': args.nproc,
                    'soft': args.nproc
                }
            ] if args.nproc > 0 else []) + ([
                {
                    'type': 'RLIMIT_CORE',
                    'hard': 0,
                    'soft': 0
                }
            ] if args.no_core_dumps else []),
            'noNewPrivileges': True
        },
        'root': {
            'path': args.root,
            'readonly': True
        },
        'hostname': 'judge',
        'mounts': [
            {
                'destination': '/proc',
                'type': 'proc',
                'source': 'proc'
            },
            {
                'destination': '/dev',
                'type': 'tmpfs',
                'source': 'tmpfs',
                'options': [
                    'nosuid',
                    'strictatime',
                    'mode=755',
                    'size=65536k'
                ]
            },
            {
                'destination': '/dev/pts',
                'type': 'devpts',
                'source': 'devpts',
                'options': [
                    'nosuid',
                    'noexec',
                    'newinstance',
                    'ptmxmode=0666',
                    'mode=0620'
                ]
            },
            {
                'destination': '/dev/shm',
                'type': 'tmpfs',
                'source': 'shm',
                'options': [
                    'nosuid',
                    'noexec',
                    'nodev',
                    'mode=1777',
                    'size=65536k'
                ]
            },
            {
                'destination': '/dev/mqueue',
                'type': 'mqueue',
                'source': 'mqueue',
                'options': [
                    'nosuid',
                    'noexec',
                    'nodev'
                ]
            },
            {
                'destination': '/sys',
                'type': 'none',
                'source': '/sys',
                'options': [
                    'rbind',
                    'nosuid',
                    'noexec',
                    'nodev',
                    'ro'
                ]
            }
        ],
        'linux': {
            "uidMappings": [
                {
                    "containerID": 1000,
                    "hostID": args.user,
                    "size": 1
                }
            ],
            "gidMappings": [
                {
                    "containerID": 1000,
                    "hostID": args.group,
                    "size": 1
                }
            ],
            "namespaces": [
                {
                    "type": "pid"
                },
                {
                    "type": "ipc"
                },
                {
                    "type": "uts"
                },
                {
                    "type": "mount"
                },
                {
                    "type": "user"
                }
            ],
        }
    }


if __name__ == '__main__':
    main()
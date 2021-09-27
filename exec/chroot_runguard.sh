#!/bin/sh
#
# Script to run a command or shell inside a chroot environment

# Abort when a single command fails:
set -e

cleanup() {
    # Unmount things on cleanup
    umount -f "$CHROOTDIR/proc" >/dev/null 2>&1  || /bin/true
    umount -f "$CHROOTDIR/sys" >/dev/null 2>&1  || /bin/true
    umount -f "$CHROOTDIR/dev/pts" >/dev/null 2>&1  || /bin/true
}
trap cleanup EXIT

# Default directory where the chroot tree lives:
CHROOTDIR="/chroot"
PREEXECUTE=""

usage()
{
	cat <<EOF
Usage: $0 [options]... [command]
Runs <command> inside the prebuilt chroot environment. If no command is
given, then an interactive shell inside the chroot is started.

Options:
  -p <pre>    Preexecuted command
  -d <dir>    Directory of the prebuilt chroot (if not default).
  -u <user>   User to run the command
  -g <group>  Group to run the command
  -h          Display this help.

This script must be run as root. It mounts proc, sysfs and devpts
before starting the chroot, and unmounts those afterwards. This script
can be used to test judge environment.
EOF
}

error()
{
    echo "Error: $*"
    echo
    usage
    exit 1
}

# Read command-line parameters:
while getopts 'p:d:u:g:h' OPT ; do
	case $OPT in
    p) PREEXECUTE=$OPTARG ;;
		d) CHROOTDIR=$OPTARG ;;
    u) USER=$OPTARG ;;
    g) GROUP=$OPTARG ;;
		h) SHOWHELP=1 ;;
		\?) error "Could not parse options." ;;
	esac
done
shift $((OPTIND-1))

if [ -n "$SHOWHELP" ]; then
	usage
	exit 0
fi

if [ $# -eq 0 ]; then
	INTERACTIVE=1
fi

PREEXECUTE_OPT=""
if [ -n "$PREEXECUTE" ]; then
  PREEXECUTE_OPT="--preexecute $PREEXECUTE"
fi

if [ "$(id -u)" != 0 ]; then
    echo "Warning: you probably need to run this program as root."
fi

DIR="$(realpath "$(dirname "$0")")/.."

[ -z "$CHROOTDIR" ] && error "No chroot directory given nor default known."
[ -d "$CHROOTDIR" ] || error "Chrootdir '$CHROOTDIR' does not exist."

#rm -f "$CHROOTDIR/etc/resolv.conf"
#cp /etc/resolv.conf /etc/hosts /etc/hostname "$CHROOTDIR/etc" || true
#cp /etc/ssl/certs/ca-certificates.crt "$CHROOTDIR/etc/ssl/certs/" || true
#cp -r /usr/share/ca-certificates/* "$CHROOTDIR/usr/share/ca-certificates" || true
#cp -r /usr/local/share/ca-certificates/* "$CHROOTDIR/usr/local/share/ca-certificates" || true

mount -t proc proc "$CHROOTDIR/proc"
mount -t sysfs sysfs "$CHROOTDIR/sys"

# Required for some warning messages about writing to log files
mount --bind /dev/pts "$CHROOTDIR/dev/pts"

# Prevent perl locale warnings in the chroot:
export LC_ALL=C

echo "Entering chroot in '$CHROOTDIR'."

echo "User is $USER, group is $GROUP."

GLOG_alsologtostderr="1" "$DIR/runguard/bin/runguard" $PREEXECUTE_OPT --user "$USER" --group "$GROUP" --work / --root "$CHROOTDIR" --out-meta program.meta -- "$*"

echo "Finished." # debug

chroot "$CHROOTDIR" /bin/sh -c "apt-get clean"
chroot "$CHROOTDIR" /bin/sh -c "rm -rf /tmp/*"
chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/lib/apt/lists/*"
chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/lib/dpkg/lock*"
chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/lib/dpkg/triggers/Lock"
chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/log/*"
chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/cache/*"

umount "$CHROOTDIR/dev/pts"
umount "$CHROOTDIR/sys"
umount "$CHROOTDIR/proc"

[ -n "$INTERACTIVE" ] && echo "Exited chroot in '$CHROOTDIR'."
exit 0

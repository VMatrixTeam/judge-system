#!/bin/sh
# @configure_input@
#
# Script to generate a basic chroot environment programs to run in a chroot.
#
# This script downloads and installs a Debian or Ubuntu base system.
# Minimum requirements: a Linux system with glibc >= 2.3, wget, ar and
# a POSIX shell in /bin/sh. About 335/610 MB disk space is needed. It
# must be run as root and will install the debootstrap package.
#
# Part of the DOMjudge Programming Contest Jury System and licensed
# under the GNU GPL. See README and COPYING for details.

# Abort when a single command fails:
set -e

echo "This script is deprecated. Please refer to https://gitlab.matrix.moe/Matrix/judge-env-aio to directly export a chroot environment."
exit 1

# cleanup() {
#     # Unmount things on cleanup
#     umount -f "$CHROOTDIR/proc" >/dev/null 2>&1  || /bin/true
#     umount -f "$CHROOTDIR/sys" >/dev/null 2>&1  || /bin/true
#     umount -f "$CHROOTDIR/dev/pts" >/dev/null 2>&1  || /bin/true
# }
# trap cleanup EXIT

# # Default directory where to build the chroot tree:
# CHROOTDIR="/chroot"

# # Fallback Debian and release (codename) to bootstrap (note: overriden right below):
# DISTRO="Ubuntu"
# RELEASE="focal"

# # List of possible architectures to install chroot for:
# DEBIAN_ARCHLIST="amd64,arm64,armel,armhf,i386,mips,mips64el,mipsel,ppc64el,s390x"
# UBUNTU_ARCHLIST="amd64,arm64,armhf,i386,powerpc,ppc64el,s390x"

# # If host system is Debian or Ubuntu, use its release and architecture by default
# if [ "x$(lsb_release -i -s || true)" = 'xDebian' ] || \
#    [ "x$(lsb_release -i -s || true)" = 'xUbuntu' ]; then
# 	DISTRO="$(lsb_release -i -s)"
# 	RELEASE="$(lsb_release -c -s)"
# 	if [ "x$(lsb_release -i -s)" = 'xDebian' ]; then
# 		if [ "x$(lsb_release -c -s)" = "xsid" ]; then
# 			RELEASE='unstable'
# 		fi
# 		if [ "x$(lsb_release -r -s)" = 'xtesting' ]; then
# 			RELEASE='testing'
# 		fi
# 	fi
# 	if [ -z "$ARCH" ]; then
# 		ARCH="$(dpkg --print-architecture)"
# 	fi
# fi

# usage()
# {
# 	cat <<EOF
# Usage: $0 [options]...
# Creates a chroot environment using the Debian or Ubuntu GNU/Linux distribution.

# Options:
#   -a <arch>    Machine archicture to use.
#   -d <dir>     Directory where to build the chroot environment.
#   -D <distro>  Linux distribution to use: 'Debian' or 'Ubuntu'.
#   -R <release> Release for the selected Linux distribution
#   -M <mirror>  Mirror to use.
#   -i <debs>    List of extra package names to install (comma separated).
#   -r <debs>    List of extra package names to remove (comma separated).
#   -l <debs>    List of local package files to install (comma separated).
#   -y           Force overwriting the chroot dir and downloading debootstrap.
#   -h           Display this help.

# This script must be run as root, <chrootdir> is the non-existing
# target location of the chroot (unless '-y' is specified). If the host
# runs Debian/Ubuntu, the host architecture and release are used. The
# default chrootdir is '/chroot'.

# Available architectures:
# Debian: $DEBIAN_ARCHLIST
# Ubuntu: $UBUNTU_ARCHLIST

# EOF
# }

# error()
# {
#     echo "Error: $*"
#     echo
#     usage
#     exit 1
# }

# # Read command-line parameters:
# while getopts 'a:d:D:R:M:i:r:l:yh' OPT ; do
# 	case $OPT in
# 		a) ARCH=$OPTARG ;;
# 		d) CHROOTDIR=$OPTARG ;;
# 		D) DISTRO=$OPTARG ;;
# 		R) RELEASE=$OPTARG ;;
#         M) DEBMIRROR=$OPTARG ;;
# 		i) INSTALLDEBS_EXTRA=$OPTARG ;;
# 		r) REMOVEDEBS_EXTRA=$OPTARG ;;
# 		l) LOCALDEBS=$OPTARG ;;
# 		y) FORCEYES=1 ;;
# 		h) SHOWHELP=1 ;;
# 		\?) error "Could not parse options." ;;
# 	esac
# done
# shift $((OPTIND-1))
# if [ $# -gt 0 ]; then
# 	error "Additional arguments specified."
# fi

# if [ -n "$SHOWHELP" ]; then
# 	usage
# 	exit 0
# fi

# if [ "$DISTRO" != 'Debian' ] && [ "$DISTRO" != 'Ubuntu' ]; then
# 	error "Invalid distribution specified, only 'Debian' and 'Ubuntu' are supported."
# fi

# if [ "$(id -u)" != 0 ]; then
#     echo "Warning: you probably need to run this program as root."
# fi

# [ -z "$CHROOTDIR" ] && error "No chroot directory given or default known."
# [ -z "$ARCH" ]      && error "No architecture given or detected."

# # Various settings that can be tweaked, specific per distribution:
# if [ "$DISTRO" = 'Debian' ]; then

# # Packages to include during bootstrap process (comma separated):
# INCLUDEDEBS="ca-certificates"

# # Packages to install after upgrade (space separated):
# INSTALLDEBS="gcc g++ default-jdk-headless default-jre-headless pypy pypy3 python3 locales"
# # For C# support add: mono-runtime libmono-system2.0-cil
# # However running mono within chroot still gives errors...

# # Packages to remove after upgrade (space separated):
# REMOVEDEBS=""

# # Which debootstrap package to install on non-Debian systems:
# DEBOOTDEB="debootstrap_1.0.114_all.deb"

# # The Debian mirror/proxy below can be passed as environment
# # variables; if none are given the following defaults are used.

# # Debian mirror. deb.debian.org will pick the 'closest'.
# [ -z "$DEBMIRROR" ] && DEBMIRROR="http://deb.debian.org/debian"

# # A local caching proxy to use for apt-get,
# # (typically an install of aptcacher-ng), for example:
# #DEBPROXY="http://aptcacher-ng.example.com:3142/"
# [ -z "$DEBPROXY" ] && DEBPROXY=""

# fi

# if [ "$DISTRO" = 'Ubuntu' ]; then

# # Packages to include during bootstrap process (comma separated):
# INCLUDEDEBS=""

# # Packages to install after upgrade (space separated):
# INSTALLDEBS="software-properties-common cabal-install ghc curl make librabbitmq-dev libcurl4-openssl-dev golang golint nodejs lua5.3 rustc xz-utils default-jdk-headless pypy pypy3 pylint python3 python3-pip pylint3 clang libclang-dev ruby scala libboost-all-dev cmake libgtest-dev gcc gcc-9 g++ g++-9 gfortran gnat gcc-multilib g++-multilib libsqlite3-dev libmysqlclient-dev libpq-dev fp-compiler valgrind locales libseccomp-dev libcgroup-dev"
# # INSTALLDEBS="gcc g++ default-jdk-headless default-jre-headless pypy pypy3 python3 locales"
# # For C# support add: mono-mcs mono-devel
# # However running mono within chroot still gives errors...

# # Packages to remove after upgrade (space separated):
# REMOVEDEBS=""

# # Which debootstrap package to install on non-Ubuntu systems:
# # This is only used when the default distro changed from Debian to
# # Ubuntu. The version below corresponds to Ubuntu 20.04 Focal.
# DEBOOTDEB="debootstrap_1.0.118ubuntu1_all.deb"

# # The Debian mirror/proxy below can be passed as environment
# # variables; if none are given the following defaults are used.

# # Ubuntu mirror, modify to match closest mirror
# [ -z "$DEBMIRROR" ] && DEBMIRROR="https://mirrors.ustc.edu.cn/ubuntu/"
# # "http://us.archive.ubuntu.com./ubuntu/"

# # A local caching proxy to use for apt-get,
# # (typically an install of aptcacher-ng), for example:
# #DEBPROXY="http://aptcacher-ng.example.com:3142/"
# [ -z "$DEBPROXY" ] && DEBPROXY=""

# fi

# INSTALLDEBS="$INSTALLDEBS $(echo $INSTALLDEBS_EXTRA | tr , ' ')"
# REMOVEDEBS=" $REMOVEDEBS  $(echo $REMOVEDEBS_EXTRA  | tr , ' ')"

# # To prevent (libc6) upgrade questions:
# export DEBIAN_FRONTEND=noninteractive

# if [ -e "$CHROOTDIR" ]; then
# 	if [ ! "$FORCEYES" ]; then
# 		printf "'%s' already exists. Remove? (y/N) " "$CHROOTDIR"
# 		read -r yn
# 		if [ "$yn" != "y" ] && [ "$yn" != "Y" ]; then
# 			error "Chrootdir already exists, exiting."
# 		fi
# 	fi
# 	cleanup
# 	rm -rf "$CHROOTDIR"
# fi

# mkdir -p "$CHROOTDIR"
# cd "$CHROOTDIR"
# CHROOTDIR="$PWD"

# if [ ! -x /usr/sbin/debootstrap ]; then

# 	echo "This script will install debootstrap on your system."
# 	if [ ! "$FORCEYES" ]; then
# 		printf "Continue? (y/N) "
# 		read -r yn
# 		if [ "$yn" != "y" ] && [ "$yn" != "Y" ]; then
# 			exit 1;
# 		fi
# 	fi

# 	if [ -f /etc/debian_version ]; then

# 		cd /
# 		apt-get install debootstrap

# 	else
# 		mkdir "$CHROOTDIR/debootstrap"
# 		cd "$CHROOTDIR/debootstrap"

# 		wget "$DEBMIRROR/pool/main/d/debootstrap/${DEBOOTDEB}"

# 		ar -x "$DEBOOTDEB"
# 		cd /
# 		zcat "$CHROOTDIR/debootstrap/data.tar.gz" | tar xv

# 		rm -rf "$CHROOTDIR/debootstrap"
# 	fi
# fi

# INCLUDEOPT=""
# if [ -n "$INCLUDEDEBS" ]; then
# 	INCLUDEOPT="--include=$INCLUDEDEBS"
# fi
# EXCLUDEOPT=""
# # shellcheck disable=SC2153
# if [ -n "$EXCLUDEDEBS" ]; then
# 	EXCLUDEOPT="--exclude=$EXCLUDEDEBS"
# fi

# BOOTSTRAP_COMMAND="/usr/sbin/debootstrap"
# if [ -n "$DEBPROXY" ]; then
#     BOOTSTRAP_COMMAND="http_proxy=\"$DEBPROXY\" $BOOTSTRAP_COMMAND"
# fi

# echo "Running debootstrap to install base system, this may take a while..."

# # Add extra paths that are not set by default on Redhat-like systems.
# # shellcheck disable=SC2086
# eval PATH="$PATH:/bin:/sbin:/usr/sbin" $BOOTSTRAP_COMMAND $INCLUDEOPT $EXCLUDEOPT \
# 	--variant=minbase --arch "$ARCH" "$RELEASE" "$CHROOTDIR" "$DEBMIRROR"

# rm -f "$CHROOTDIR/etc/resolv.conf"
# cp /etc/resolv.conf /etc/hosts /etc/hostname "$CHROOTDIR/etc" || true
# cp /etc/ssl/certs/ca-certificates.crt "$CHROOTDIR/etc/ssl/certs/" || true
# cp -r /usr/share/ca-certificates/* "$CHROOTDIR/usr/share/ca-certificates" || true
# cp -r /usr/local/share/ca-certificates/* "$CHROOTDIR/usr/local/share/ca-certificates" || true

# if [ "$DISTRO" = 'Debian' ]; then
# 	if [ "x$RELEASE" = 'xtesting' -o "x$RELEASE" = 'xunstable' ]; then
# 		DISABLE_SECURITY='#'
# 	fi
# cat > "$CHROOTDIR/etc/apt/sources.list" <<EOF
# # Release as specified or taken from host (incl. optional security repository):

# # $RELEASE
# deb $DEBMIRROR			$RELEASE		main
# ${DISABLE_SECURITY}deb http://security.debian.org	$RELEASE/updates	main

# EOF
# fi
# if [ "$DISTRO" = 'Ubuntu' ]; then
# cat > "$CHROOTDIR/etc/apt/sources.list" <<EOF
# deb $DEBMIRROR $RELEASE main
# deb $DEBMIRROR $RELEASE universe
# deb $DEBMIRROR $RELEASE-updates main
# deb $DEBMIRROR $RELEASE-updates universe
# deb $DEBMIRROR $RELEASE-security main
# deb $DEBMIRROR $RELEASE-security universe
# EOF
# fi

# cat > "$CHROOTDIR/etc/apt/apt.conf" <<EOF
# APT::Get::Assume-Yes "true";
# APT::Get::Force-Yes "false";
# APT::Get::Purge "true";
# APT::Install-Recommends "false";
# Acquire::Retries "3";
# Acquire::PDiffs "false";
# EOF

# # Add apt proxy settings if desired
# if [ -n "$DEBPROXY" ]; then
#     echo "Acquire::http::Proxy \"$DEBPROXY\";" >> "$CHROOTDIR/etc/apt/apt.conf"
# fi

# mount -t proc proc "$CHROOTDIR/proc"
# mount -t sysfs sysfs "$CHROOTDIR/sys"

# # Required for some warning messages about writing to log files
# mount --bind /dev/pts "$CHROOTDIR/dev/pts"

# # Prevent perl locale warnings in the chroot:
# export LC_ALL=C

# userdel -r judge || /bin/true
# useradd -d /nonexistent -U -M -s /bin/false judge
# chroot "$CHROOTDIR" useradd -U -m -s /bin/sh judge

# chroot "$CHROOTDIR" /bin/sh -c debconf-set-selections <<EOF
# passwd	passwd/root-password-crypted	password
# passwd	passwd/user-password-crypted	password
# passwd	passwd/root-password			password
# passwd	passwd/root-password-again		password
# passwd	passwd/user-password-again		password
# passwd	passwd/user-password			password
# passwd	passwd/shadow					boolean	true
# passwd	passwd/username-bad				note
# passwd	passwd/password-mismatch		note
# passwd	passwd/username					string
# passwd	passwd/make-user				boolean	true
# passwd	passwd/md5						boolean	false
# passwd	passwd/user-fullname			string
# passwd	passwd/user-uid					string
# passwd	passwd/password-empty			note
# debconf	debconf/priority				select	high
# debconf	debconf/frontend				select	Noninteractive
# locales	locales/locales_to_be_generated		multiselect
# locales	locales/default_environment_locale	select	None
# EOF

# if [ "$DISTRO" = 'Ubuntu' ]; then
# # Disable upstart init scripts(so upgrades work), we don't need to actually run
# # any services in the chroot, so this is fine.
# # Refer to: http://ubuntuforums.org/showthread.php?t=1326721
# chroot "$CHROOTDIR" /bin/sh -c "dpkg-divert --local --rename --add /sbin/initctl"
# chroot "$CHROOTDIR" /bin/sh -c "ln -s /bin/true /sbin/initctl"
# fi

# # Upgrade the system, and install/remove packages as desired
# chroot "$CHROOTDIR" /bin/sh -c "apt-get update && apt-get dist-upgrade"
# chroot "$CHROOTDIR" /bin/sh -c "apt-get clean"
# chroot "$CHROOTDIR" /bin/sh -c "apt-get install $INSTALLDEBS"
# if [ -n "$LOCALDEBS" ]; then
# 	DIR=$(mktemp -d -p "$CHROOTDIR/tmp" 'chroot_debs_XXXXXX')
# 	# shellcheck disable=SC2046
# 	cp $(echo "$LOCALDEBS" | tr , ' ') "$DIR"
# 	chroot "$CHROOTDIR" /bin/sh -c "dpkg -i /${DIR#$CHROOTDIR}/*.deb"
# fi

# # Do some cleanup of the chroot
# chroot "$CHROOTDIR" /bin/sh -c "apt-get remove --purge $REMOVEDEBS"
# chroot "$CHROOTDIR" /bin/sh -c "apt-get autoremove --purge"
# chroot "$CHROOTDIR" /bin/sh -c "apt-get clean"

# # Install required python packages
# chroot "$CHROOTDIR" /bin/sh -c "pip3 install xmltodict"

# # Install GTest
# chroot "$CHROOTDIR" /bin/sh -c "cd /usr/src/gtest && cmake . && make && cp ./lib/*.a /usr/lib"

# # Install oclint
# chroot "$CHROOTDIR" /bin/sh -c "apt-get install wget"

# wget "https://github.com/oclint/oclint/releases/download/v20.11/oclint-20.11-llvm-11.0.0-x86_64-linux-ubuntu-20.04.tar.gz" -P "$CHROOTDIR/tmp"
# tar -C "$CHROOTDIR/tmp/" -xzf "$CHROOTDIR/tmp/oclint-20.11-llvm-11.0.0-x86_64-linux-ubuntu-20.04.tar.gz"
# cp -p $(find "$CHROOTDIR/tmp/oclint-20.11/bin" -name "oclint*") "$CHROOTDIR/usr/local/bin/"
# cp -rp "$CHROOTDIR/tmp/oclint-20.11/lib/." "$CHROOTDIR/usr/local/lib/"

# chroot "$CHROOTDIR" /bin/sh -c "apt-get remove wget"

# # Clean temp directory
# chroot "$CHROOTDIR" /bin/sh -c "rm -rf /tmp/*"
# chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/lib/apt/lists/*"
# chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/lib/dpkg/lock*"
# chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/lib/dpkg/triggers/Lock"
# chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/log/*"
# chroot "$CHROOTDIR" /bin/sh -c "rm -rf /var/cache/*"

# # Remove unnecessary setuid bits
# chroot "$CHROOTDIR" /bin/sh -c "chmod a-s /usr/bin/wall /usr/bin/newgrp \
# 	/usr/bin/chage /usr/bin/chfn /usr/bin/chsh /usr/bin/expiry \
# 	/usr/bin/gpasswd /usr/bin/passwd \
# 	/bin/su /bin/mount /bin/umount /sbin/unix_chkpwd" || true

# # Disable root account
# sed -i "s/^root::/root:*:/" "$CHROOTDIR/etc/shadow"

# # Create a dummy file to test that in the judging environment no
# # access to group root readable files is available:
# echo "This file should not be readable inside the judging environment!" \
# 	> "$CHROOTDIR/etc/root-permission-test.txt"
# chmod 0640 "$CHROOTDIR/etc/root-permission-test.txt"

# umount "$CHROOTDIR/dev/pts"
# umount "$CHROOTDIR/sys"
# umount "$CHROOTDIR/proc"

# echo "Done building chroot in $CHROOTDIR"
# exit 0

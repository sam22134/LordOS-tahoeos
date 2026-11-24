#!/bin/bash
# devices.sh — "no camera" and "no USB" for Linux apps: lord-netperm's cgroup/dev program
# (docs/FIREWALL.md, "Linux apps"). Needs a video device (v4l2loopback is enough) and
# v4l2-ctl. Applies immediately: the next open() of an app that is already running.
#
#   bash server/tests/fwlinux/devices.sh [/dev/videoN]
. "$(dirname "$0")/lib.sh"
VID=${1:-$(ls /dev/video* 2>/dev/null | head -1)}
USB=$(find /dev/bus/usb -type c 2>/dev/null | head -1)
[ -c "$VID" ] || { echo "no video device"; exit 2; }
[ -S /run/lordos/netperm.sock ] || { echo "lord-netperm is not running"; exit 2; }
APP=desktop:fwdev
SC=$(scope fwdev)
OPEN='
import os, sys, errno
modes = {"ro": os.O_RDONLY, "wo": os.O_WRONLY, "rw": os.O_RDWR}
try:
    mode = modes[sys.argv[2]] if len(sys.argv) > 2 else os.O_RDONLY
    fd = os.open(sys.argv[1], mode | os.O_NONBLOCK); os.close(fd); print("ok")
except OSError as e:
    print(errno.errorcode.get(e.errno, str(e.errno)))
'
op() { in_scope "$SC" python3 -c "$OPEN" "$1" "${2:-ro}" 2>&1 | tail -1; }
expect() { local want=$1 got=$2 what=$3; case "$got" in $want*) ok "$what → $got";; *) bad "$what → $got (expected $want)";; esac; }

echo "1. not restricted: the camera and USB open"
restrict $APP ""
expect ok "$(op "$VID")" "open $VID"
got=$(in_scope "$SC" v4l2-ctl -d "$VID" --info 2>&1 | grep -c "Driver name")
[ "$got" = 1 ] && ok "v4l2-ctl --info works" || bad "v4l2-ctl --info failed"
[ -n "$USB" ] && expect ok "$(op "$USB")" "open $USB"

echo "2. no-camera: the video device is refused, USB still opens"
restrict $APP no-camera
expect EPERM "$(op "$VID" ro)" "open $VID read-only"
expect EPERM "$(op "$VID" wo)" "open $VID write-only"
expect EPERM "$(op "$VID" rw)" "open $VID read-write"
got=$(in_scope "$SC" v4l2-ctl -d "$VID" --info 2>&1 | tail -1)
case "$got" in *"ermission denied"*|*"not permitted"*) ok "v4l2-ctl --info refused ($got)";; *) bad "v4l2-ctl --info → $got";; esac
[ -n "$USB" ] && expect ok "$(op "$USB")" "open $USB"
expect ok "$(op /dev/null)" "open /dev/null (other devices stay allowed)"
expect ok "$(op /dev/urandom)" "open /dev/urandom"
got=$(python3 -c "$OPEN" "$VID" | tail -1)
expect ok "$got" "the same open outside the app's scope"

echo "3. no-usb: raw USB is refused, the camera opens"
restrict $APP no-usb
if [ -n "$USB" ]; then
    expect EPERM "$(op "$USB" ro)" "open $USB read-only"
    expect EPERM "$(op "$USB" wo)" "open $USB write-only"
    expect EPERM "$(op "$USB" rw)" "open $USB read-write"
fi
expect ok "$(op "$VID")" "open $VID"

echo "4. lifted again: works at once (no restart)"
restrict $APP ""
expect ok "$(op "$VID")" "open $VID"

echo "5. escape with systemd-run --user while the restricted app runs"
restrict $APP no-camera
in_scope "$SC" sleep 6 &
sleep 1.5                               # lord-netperm sees the scope and builds the union
got=$(systemd-run --user --quiet --wait --pipe python3 -c "$OPEN" "$VID" 2>&1 | tail -1)
expect EPERM "$got" "systemd-run --user (user@ union)"
PWCG=/sys/fs/cgroup/user.slice/user-$U.slice/user@$U.service/session.slice/pipewire.service
if [ -d "$PWCG" ]; then             # no exemption a process could join: an escapee that moves itself
    # into the user's pipewire.service (the user owns user@'s subtree, so it can) is still refused
    got=$(systemd-run --user --scope --quiet bash -c 'echo $$ > "$0/cgroup.procs" && exec python3 -c "$1" "$2"' "$PWCG" "$OPEN" "$VID" 2>&1 | tail -1)
    expect EPERM "$got" "an escapee that moved itself into pipewire.service"
fi
wait
fwlinux_done

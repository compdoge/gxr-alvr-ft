#!/system/bin/sh
# KernelSU Manager "Action" button: (re)start the supervised relay immediately,
# with no reboot. Use it after changing pc/port/devopts, after a Wi-Fi change, or
# after a dev-mode binary push ($MODDIR/dev + /data/local/tmp/gxr_alvr).
#
# It stops the previous supervisor and relay first, so this doubles as the
# "restart the relay now" button. See supervise.sh.
MODDIR=${0%/*}

sh "$MODDIR/supervise.sh"

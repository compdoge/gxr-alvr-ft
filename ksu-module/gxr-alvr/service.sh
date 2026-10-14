#!/system/bin/sh
# KernelSU module service script: start the supervised relay as root at boot
# (and in late-load mode). MODDIR = the module dir.
#
# All config reading lives in supervise.sh, which also supervises the relay
# afterwards (see that file and README.md).
MODDIR=${0%/*}

# late_start can be very early; give the system/network a moment.
sleep 8

sh "$MODDIR/supervise.sh"

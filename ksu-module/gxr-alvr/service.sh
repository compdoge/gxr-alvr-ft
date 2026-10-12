#!/system/bin/sh
# KernelSU module service script: start the Galaxy XR ALVR relay as root.
# Runs in late_start service (and in late-load mode). MODDIR = the module dir.
MODDIR=${0%/*}

# Target. With no "pc" file the relay auto-connects by sending to the Wi-Fi
# LAN broadcast address, which the stock VRCFT-ALVR module (bound 0.0.0.0:41463)
# receives with no PC-side setup -- so "install -> reboot -> done" works for a
# new user. To pin a specific PC instead, put its IP in "$MODDIR/pc"; put a
# custom port in "$MODDIR/port" (default 41463). See this module's README.md.
PC=$(cat "$MODDIR/pc" 2>/dev/null)
PORT=$(cat "$MODDIR/port" 2>/dev/null)
[ -z "$PORT" ] && PORT=41463

# Shipped binary. Dev opt-in only: create "$MODDIR/dev" to prefer a binary
# pushed to /data/local/tmp/gxr_alvr. (An unconditional /data/local/tmp
# override was removed: a stale file there would silently shadow the module
# binary for shipped users.)
BIN="$MODDIR/gxr_alvr"
if [ -f "$MODDIR/dev" ] && [ -x /data/local/tmp/gxr_alvr ]; then
  BIN=/data/local/tmp/gxr_alvr
fi

# late_start can be very early; give the system/network a moment.
sleep 8

# Stop any previous instance, then start fresh.
pkill -x gxr_alvr 2>/dev/null
sleep 1
if [ -n "$PC" ]; then
  setsid "$BIN" --pc "$PC" --port "$PORT" </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
else
  setsid "$BIN" --port "$PORT" </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
fi
echo "gxr-alvr: started relay ($BIN pc=${PC:-auto} port=$PORT pid=$(pidof gxr_alvr))" >>/data/local/tmp/gxr_alvr.log

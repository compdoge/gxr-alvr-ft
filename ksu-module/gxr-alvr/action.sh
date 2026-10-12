#!/system/bin/sh
# KernelSU Manager "Action" button: (re)start the relay immediately, without a
# reboot. Use it after changing pc/port, after Wi-Fi changes, or after a
# dev-mode binary push ($MODDIR/dev + /data/local/tmp/gxr_alvr).
MODDIR=${0%/*}

# Same target resolution as service.sh (see that file / README.md).
PC=$(cat "$MODDIR/pc" 2>/dev/null)
PORT=$(cat "$MODDIR/port" 2>/dev/null)
[ -z "$PORT" ] && PORT=41463

BIN="$MODDIR/gxr_alvr"
if [ -f "$MODDIR/dev" ] && [ -x /data/local/tmp/gxr_alvr ]; then
  BIN=/data/local/tmp/gxr_alvr
fi

pkill -x gxr_alvr 2>/dev/null
sleep 1
if [ -n "$PC" ]; then
  setsid "$BIN" --pc "$PC" --port "$PORT" </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
else
  setsid "$BIN" --port "$PORT" </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
fi
sleep 1
echo "gxr-alvr: relay=$BIN pid=$(pidof gxr_alvr) -> ${PC:-auto(broadcast)}:$PORT"

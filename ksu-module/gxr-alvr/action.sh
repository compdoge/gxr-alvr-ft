#!/system/bin/sh
# KernelSU Manager "Action" button: (re)start the relay immediately, without a
# reboot. Use it after changing pc/port, after Wi-Fi changes, or after a
# dev-mode binary push ($MODDIR/dev + /data/local/tmp/gxr_alvr).
MODDIR=${0%/*}

# Same target resolution as service.sh (see that file / README.md).
PC=$(cat "$MODDIR/pc" 2>/dev/null)
PORT=$(cat "$MODDIR/port" 2>/dev/null)
[ -z "$PORT" ] && PORT=41463

# Same optional extras as service.sh (see README.md).
MIRROR=$(cat "$MODDIR/mirror" 2>/dev/null)
TONGUE=$(cat "$MODDIR/tongue" 2>/dev/null)
EXTRA=""
[ -n "$MIRROR" ] && EXTRA="$EXTRA --mirror-port $MIRROR"
[ "$TONGUE" = "0" ] && EXTRA="$EXTRA --tongue-extras 0"
[ -f "$MODDIR/verbose" ] && EXTRA="$EXTRA --verbose"
if [ -f "$MODDIR/devopts" ]; then
  if grep -qvE '^(--[a-z0-9-]+( [A-Za-z0-9._,:/+-]+)?)?$' "$MODDIR/devopts"; then
    echo "gxr-alvr: ignoring devopts (unexpected content)" >>/data/local/tmp/gxr_alvr.log
  else
    EXTRA="$EXTRA $(tr '\n' ' ' < "$MODDIR/devopts")"
  fi
fi

BIN="$MODDIR/gxr_alvr"
if [ -f "$MODDIR/dev" ] && [ -x /data/local/tmp/gxr_alvr ]; then
  BIN=/data/local/tmp/gxr_alvr
fi

pkill -x gxr_alvr 2>/dev/null
sleep 1
pidof gxr_alvr >/dev/null 2>&1 && { pkill -9 -x gxr_alvr 2>/dev/null; sleep 1; }
if [ -n "$PC" ]; then
  setsid "$BIN" --pc "$PC" --port "$PORT" $EXTRA </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
else
  setsid "$BIN" --port "$PORT" $EXTRA </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
fi
sleep 1
echo "gxr-alvr: relay=$BIN pid=$(pidof gxr_alvr) -> ${PC:-auto(broadcast)}:$PORT mirror=${MIRROR:-none}"

#!/system/bin/sh
# KernelSU module service script: start the Galaxy XR face-tracking relay as root.
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

# Optional extras (files in the module dir):
#   mirror  = also send a copy to <target>:<this port> (e.g. a monitor tool)
#   tongue  = 0 disables the TongueRoll/Shape/Twist extras (A/B testing)
#   verbose = present enables the 1 Hz trace
#   devopts = extra sender flags from the WebUI "Developer options", one
#             "--flag value" per line. Data-only: reject shell metacharacters.
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

# Stop any previous instance (any module version, or a manual dev copy), then
# start fresh. Hard-kill as a fallback so a wedged/stale peer can't linger and
# shadow the new binary.
pkill -x gxr_alvr 2>/dev/null
sleep 1
pidof gxr_alvr >/dev/null 2>&1 && { pkill -9 -x gxr_alvr 2>/dev/null; sleep 1; }
if [ -n "$PC" ]; then
  setsid "$BIN" --pc "$PC" --port "$PORT" $EXTRA </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
else
  setsid "$BIN" --port "$PORT" $EXTRA </dev/null >>/data/local/tmp/gxr_alvr.log 2>&1 &
fi
echo "gxr-alvr: started relay ($BIN pc=${PC:-auto} port=$PORT mirror=${MIRROR:-none} pid=$(pidof gxr_alvr))" >>/data/local/tmp/gxr_alvr.log

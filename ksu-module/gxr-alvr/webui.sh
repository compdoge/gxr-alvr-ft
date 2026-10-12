#!/system/bin/sh
# Helper for the module WebUI (webroot/index.html). The KernelSU Manager invokes
# this as root. It validates its arguments, reads/writes the module's option
# files, and can restart the relay. Output is machine-readable: one "key=value"
# per line (the page parses it).
MODDIR=${0%/*}
[ -f "$MODDIR/module.prop" ] || MODDIR=/data/adb/modules/gxr-alvr
LOG=/data/local/tmp/gxr_alvr.log

readval() { [ -f "$MODDIR/$1" ] && cat "$MODDIR/$1" 2>/dev/null; }

case "$1" in
  status)
    echo "pc=$(readval pc)"
    echo "port=$(readval port)"
    echo "mirror=$(readval mirror)"
    echo "tongue=$(readval tongue)"
    echo "verbose=$([ -f "$MODDIR/verbose" ] && echo 1 || echo 0)"
    echo "pid=$(pidof gxr_alvr 2>/dev/null)"
    echo "ip=$(ip -4 addr show 2>/dev/null | grep -o 'inet [0-9.]*' | awk '{print $2}' | tr '\n' ',')"
    echo "armed=$(grep -a 'gxr_alvr: v' "$LOG" 2>/dev/null | tail -n1)"
    echo "tail=$(tail -n 12 "$LOG" 2>/dev/null | tr '\n' '~')"
    ;;
  set)
    # set <pc|-|auto> <port> <tongue 0|1> <verbose 0|1>
    PC="$2"; PORT="$3"; TONGUE="$4"; VERB="$5"
    case "$PC" in
      ""|-|auto) rm -f "$MODDIR/pc" 2>/dev/null ;;
      *[!0-9.]*) echo "error=invalid PC address"; exit 2 ;;
      *) printf '%s\n' "$PC" > "$MODDIR/pc" ;;
    esac
    case "$PORT" in
      ""|*[!0-9]*) echo "error=invalid port"; exit 2 ;;
      *) if [ "$PORT" -ge 1 ] && [ "$PORT" -le 65535 ]; then
           printf '%s\n' "$PORT" > "$MODDIR/port"
         else echo "error=invalid port"; exit 2; fi ;;
    esac
    if [ "$TONGUE" = "1" ]; then rm -f "$MODDIR/tongue" 2>/dev/null; else printf '0\n' > "$MODDIR/tongue"; fi
    if [ "$VERB" = "1" ]; then : > "$MODDIR/verbose"; else rm -f "$MODDIR/verbose" 2>/dev/null; fi
    sh "$MODDIR/action.sh" >/dev/null 2>&1
    sleep 1
    echo "ok=1"
    echo "pid=$(pidof gxr_alvr 2>/dev/null)"
    ;;
  test)
    # test <ip>  -> best-effort reachability check from the headset
    IP="$2"
    case "$IP" in *[!0-9.]*|"") echo "ping=bad"; exit 2 ;; esac
    if command -v timeout >/dev/null 2>&1; then
      timeout 2 ping -c 1 "$IP" >/dev/null 2>&1 && echo "ping=ok" || echo "ping=fail"
    else
      ping -c 1 -W 1 "$IP" >/dev/null 2>&1 && echo "ping=ok" || echo "ping=fail"
    fi
    ;;
  *)
    echo "error=usage: status | set <pc|-|auto> <port> <tongue> <verbose> | test <ip>"
    exit 2
    ;;
esac

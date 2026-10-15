#!/system/bin/sh
# Helper for the module WebUI (webroot/index.html). The KernelSU Manager invokes
# this as root. It validates its arguments, reads/writes the module's option
# files, restarts / pauses / resumes the relay, and exports the logs. Output is
# machine-readable: one "key=value" per line (the page parses it).
MODDIR=${0%/*}
[ -f "$MODDIR/module.prop" ] || MODDIR=/data/adb/modules/gxr-alvr
LOG=/data/local/tmp/gxr_alvr.log
PAUSED="$MODDIR/paused"
TRACE=/sys/kernel/tracing

readval() { [ -f "$MODDIR/$1" ] && cat "$MODDIR/$1" 2>/dev/null; }

case "$1" in
  status)
    echo "pc=$(readval pc)"
    echo "port=$(readval port)"
    echo "mirror=$(readval mirror)"
    echo "tongue=$(readval tongue)"
    echo "verbose=$([ -f "$MODDIR/verbose" ] && echo 1 || echo 0)"
    echo "paused=$([ -f "$PAUSED" ] && echo 1 || echo 0)"
    dv=""
    if [ -f "$MODDIR/devopts" ]; then dv=$(sed 's/ /=/' "$MODDIR/devopts" | tr '\n' ';'); dv=${dv%;}; fi
    echo "devopts=$dv"
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
  setdev)
    # setdev <--flag=value;--flag=value | ->  -> write devopts (validated) + restart
    SPEC="$2"
    if [ -z "$SPEC" ] || [ "$SPEC" = "-" ]; then
      rm -f "$MODDIR/devopts" 2>/dev/null
    else
      if ! printf '%s' "$SPEC" | grep -qE '^[A-Za-z0-9._,:/+;=-]+$'; then
        echo "error=invalid characters in dev options"; exit 2
      fi
      BAD=$(printf '%s' "$SPEC" | tr ';' '\n' | grep -vE '^--[a-z0-9-]+=[A-Za-z0-9._,:/+-]+$')
      if [ -n "$BAD" ]; then echo "error=bad dev option: $BAD"; exit 2; fi
      printf '%s\n' "$SPEC" | tr ';' '\n' | sed 's/=/ /' > "$MODDIR/devopts"
    fi
    sh "$MODDIR/action.sh" >/dev/null 2>&1
    sleep 1
    echo "ok=1"
    echo "pid=$(pidof gxr_alvr 2>/dev/null)"
    ;;
  pause)
    # Stop the supervisor first (killing the relay alone would just make it
    # respawn), then let the relay exit on its own: SIGTERM makes it disable its
    # probes and clear uprobe_events on the way out, so a paused module is
    # genuinely not tracing anything. This is the honest A/B switch for
    # "is the module making my headset hot": one tap, no reboot, no zip swap.
    pkill -f "$MODDIR/supervise.sh --loop" 2>/dev/null
    sleep 1
    pkill -x gxr_alvr 2>/dev/null
    sleep 1
    if pidof gxr_alvr >/dev/null 2>&1; then
      echo "error=relay did not stop (pid $(pidof gxr_alvr))"; exit 1
    fi
    # Cover the case where the relay had been killed from outside earlier, so
    # nothing disarmed its probes: turn our tracing off explicitly.
    echo 0 > "$TRACE/tracing_on" 2>/dev/null
    echo 0 > "$TRACE/events/uprobes/pten/enable" 2>/dev/null
    echo 0 > "$TRACE/events/uprobes/dir/enable" 2>/dev/null
    echo 0 > "$TRACE/events/uprobes/bs/enable" 2>/dev/null
    echo 0 > "$TRACE/events/uprobes/jaw/enable" 2>/dev/null
    echo 0 > "$TRACE/events/uprobes/tongue/enable" 2>/dev/null
    : > "$PAUSED"
    echo "ok=1"
    echo "paused=1"
    ;;
  resume)
    rm -f "$PAUSED" 2>/dev/null
    sh "$MODDIR/action.sh" >/dev/null 2>&1
    sleep 1
    echo "ok=1"
    echo "paused=0"
    echo "pid=$(pidof gxr_alvr 2>/dev/null)"
    ;;
  exportlog)
    # Copy the logs where the user can reach them (Downloads on internal storage),
    # overwriting the previous copy instead of piling up files. Uses `cat >` so it
    # does not depend on cp being allowed to write to /sdcard.
    DST=/sdcard/Download
    mkdir -p "$DST" 2>/dev/null
    n=0
    for pair in "gxr-alvr-log.txt:$LOG" "gxr-alvr-log-previous.txt:$LOG.1" \
                "gxr-alvr-install-log.txt:/data/local/tmp/gxr_alvr_install.log"; do
      out="${pair%%:*}"; src="${pair#*:}"
      if [ -f "$src" ]; then cat "$src" > "$DST/$out" 2>/dev/null && n=$((n + 1)); fi
    done
    echo "ok=$n"
    echo "dir=$DST"
    echo "files=$(ls -l "$DST"/gxr-alvr-log.txt "$DST"/gxr-alvr-log-previous.txt 2>/dev/null | awk '{print $5}' | tr '\n' ',')"
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
    echo "error=usage: status | set <pc|-|auto> <port> <tongue> <verbose> | setdev <spec|-> | pause | resume | exportlog | test <ip>"
    exit 2
    ;;
esac

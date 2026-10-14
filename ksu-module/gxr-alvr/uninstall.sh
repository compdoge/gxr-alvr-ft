#!/system/bin/sh
# KernelSU module uninstall: stop the supervisor first (otherwise it would keep
# restarting the relay), then the relay itself.
pkill -f "supervise.sh --loop" 2>/dev/null
sleep 1
pkill -x gxr_alvr 2>/dev/null

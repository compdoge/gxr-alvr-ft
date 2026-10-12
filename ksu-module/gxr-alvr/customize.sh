#!/system/bin/sh
# KernelSU module installer customization (sourced by the installer after extraction).
set_perm $MODPATH/gxr_alvr    0 0 0755
set_perm $MODPATH/service.sh  0 0 0755
set_perm $MODPATH/action.sh   0 0 0755
set_perm $MODPATH/webui.sh    0 0 0755
set_perm $MODPATH/uninstall.sh 0 0 0755
# Do NOT set permissions on $MODPATH/webroot -- KernelSU sets them itself.
# Kill any relay still running from an OLDER module version (or a manual dev
# copy) so a stale binary can't keep streaming after an in-place update. The
# installer has a minimal PATH, so try several tools and, as a last resort, kill
# by pid. Logged so it can be inspected at /data/local/tmp/gxr_alvr_install.log.
# (The new relay starts on reboot -- KernelSU modules are not active until then.)
KLOG=/data/local/tmp/gxr_alvr_install.log
{
  echo "install: killing old gxr_alvr (before: $(pidof gxr_alvr 2>/dev/null))"
  for p in $(pidof gxr_alvr 2>/dev/null); do kill -9 "$p" 2>/dev/null; done
  /system/bin/pkill -x gxr_alvr 2>/dev/null
  pkill -x gxr_alvr 2>/dev/null
  killall gxr_alvr 2>/dev/null
  sleep 1
  echo "install: after kill (pidof: $(pidof gxr_alvr 2>/dev/null))"
} >> "$KLOG" 2>&1
ui_print "- gxr-alvr: Galaxy XR ALVR relay installed"
ui_print "  reboot to activate this version (KernelSU modules load at boot)"
ui_print "  target: auto (LAN broadcast) -> VRCFT-ALVR module on UDP 41463"
ui_print "  set the PC's IP from the Manager: tap WebUI on this module"

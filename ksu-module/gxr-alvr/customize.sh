#!/system/bin/sh
# KernelSU module installer customization (sourced by the installer after extraction).
set_perm $MODPATH/gxr_alvr    0 0 0755
set_perm $MODPATH/service.sh  0 0 0755
set_perm $MODPATH/action.sh   0 0 0755
set_perm $MODPATH/uninstall.sh 0 0 0755
ui_print "- gxr-alvr: Galaxy XR ALVR relay installed"
ui_print "  target: auto (Wi-Fi broadcast) -> VRCFT-ALVR module on UDP 41463"
ui_print "  no PC setup needed; pin a PC with $MODPATH/pc and $MODPATH/port"
ui_print "  Manager Action button = restart the relay now"

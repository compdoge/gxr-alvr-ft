# `gxr-alvr` — Galaxy XR ALVR relay (KernelSU module)

Runs the headset-side sender `gxr_alvr` as root at boot. It reads the Galaxy XR
perception host through read-only ftrace uprobes and streams the full
face/eye parameter set to the PC as ALVR `UniExpr` records on **UDP 41463**, for
the **VRCFT-ALVR-UE** VRCFT module.

## Install (normal user)

1. Install the module zip in the KernelSU Manager and reboot.
2. Done — **no PC-side setup and no IP to type.** With no `pc` file the relay
   auto-connects by sending each datagram to the **Wi-Fi LAN broadcast address**
   (computed from the interface netmask). The stock VRCFT-ALVR module binds
   `0.0.0.0:41463`, so it receives broadcast datagrams too.

Requirements: the headset and PC on the same LAN; the VRCFT-ALVR module running
on the PC. Windows Firewall must allow inbound UDP 41463 for VRCFT (the same
rule the module already needs for unicast).

## Pin a specific PC (override)

Only needed if the PC is on a different subnet, or to avoid broadcasting to the
whole LAN. Create `pc` (and optionally `port`) in the module directory, then
tap the Manager **Action** button (or reboot):

```sh
# as root (e.g. via `adb shell su` or a terminal):
echo 192.168.1.50 > /data/adb/modules/gxr-alvr/pc
echo 41463       > /data/adb/modules/gxr-alvr/port   # optional
```

Remove `pc` to go back to auto/broadcast. `port` defaults to `41463`.

## Action button

The Manager **Action** button runs `action.sh`: it stops the current relay
(incl. any stale copy from an older module version) and starts it again
immediately (no reboot). Use it after changing `pc`/`port`, or after a dev
binary push. **A module update does not restart the relay by itself** — tap
**Action** (or reboot) after installing a new version.

## Monitor / A–B (optional)

To watch the values on the PC while VRCFT keeps 41463, create a `mirror` file
with the extra port (the relay then sends a copy to `<target>:<port>`):

```sh
echo 41464 > /data/adb/modules/gxr-alvr/mirror
```

Set `tongue` to `0` to disable the extra `TongueRoll` parameter (A–B test).
Both are read at start — tap the **Action** button after changing them.

## Dev mode (opt-in)

To iterate on the sender without reinstalling the module, create an empty `dev`
file and push a binary:

```sh
touch /data/adb/modules/gxr-alvr/dev
adb push gxr_alvr /data/local/tmp/gxr_alvr && adb shell chmod 755 /data/local/tmp/gxr_alvr
```

Now `service.sh`/`action.sh` prefer `/data/local/tmp/gxr_alvr`. Delete `dev`
to return to the bundled binary. (The dev override is opt-in because a stale
`/data/local/tmp/gxr_alvr` silently shadowing the module binary is a footgun
for shipped users.)

## Files

| file | purpose |
|---|---|
| `gxr_alvr` | the sender (built from `headset/gxr_alvr.c`) |
| `service.sh` | boot / late-load start |
| `action.sh` | Manager Action button = restart now |
| `customize.sh` | installer permissions + ui_print |
| `uninstall.sh` | stop the relay on uninstall |
| `sepolicy.rule` | allow the KernelSU root domain to use tracefs (Enforcing) |
| `module.prop` | module metadata |
| `pc`, `port`, `mirror`, `tongue`, `verbose`, `dev` | optional overrides (not shipped; create as needed) |

## Logs

`/data/local/tmp/gxr_alvr.log` (append). The relay also logs `armed …` /
`auto target …` lines at startup. Pass `--verbose` (dev) for the 1 Hz gaze trace.

## Notes

- Firmware-specific uprobe offsets (firmware `I610UEU2AZF3`); the sender fails
  soft per probe if the firmware changes.
- The relay refuses to start a second copy (flock at
  `/data/local/tmp/gxr_alvr.lock`) and self-heals if tracefs is cleared under it.
- Wire format is fixed: `"UniExpr\0"` + 88 `float32` UE weights + an 11-float
  eye tail (99 floats, 396 B payload, 404 B datagram). Do not change it without
  updating the module.

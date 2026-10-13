# `gxr-alvr` — Galaxy XR ALVR relay (KernelSU module)

Runs the headset-side sender `gxr_alvr` as root at boot. It reads the Galaxy XR
perception host through read-only ftrace uprobes and streams the full
face/eye parameter set to the PC as ALVR `UniExpr` records on **UDP 41463**, for
the **VRCFT-ALVR-UE** VRCFT module.

## Install (normal user)

1. Install the module zip in the KernelSU Manager and reboot.
2. Done — **no PC-side setup and no IP to type.** With no `pc` file the relay
   auto-connects by broadcasting each datagram to **every up interface's LAN
   broadcast address plus `255.255.255.255`**. The stock VRCFT-ALVR module binds
   `0.0.0.0:41463`, so it receives broadcast datagrams too.

Requirements: the headset and PC on the same LAN; the VRCFT-ALVR module running
on the PC. Windows Firewall must allow inbound UDP 41463 for VRCFT (the same
rule the module already needs for unicast).

## Set the PC's IP (Manager WebUI) — use this if auto-connect is flaky

Wi-Fi access points frequently filter broadcast or isolate clients, so broadcast
does not always reach the PC. The reliable fix is to send **unicast** to the PC.
Open **KernelSU Manager → Modules → this module → WebUI** and use the page:

- **PC IP address** — the PC's IPv4 (the machine running VRCFaceTracking), then
  tap **Save & restart relay**. Leave it empty for auto/broadcast.
- **Test** — ping the PC from the headset to check reachability.
- **UDP port** — defaults to 41463.
- **TongueRoll / Verbose** — toggle the extra tongue parameter and the fast tuning log.

The page writes the module's option files (`pc`, `port`, `tongue`, `verbose`) and
restarts the relay — no adb, no terminal. (If your Manager build has no WebUI
button, use the by-hand method below; it writes the same files.)

### Or do it by hand (root)

Only needed if the PC is on a different subnet, or to avoid broadcasting to the
whole LAN. Create `pc` (and optionally `port`) in the module directory, then
tap the Manager **Action** button (or reboot):

```sh
# as root (e.g. via a terminal):
echo 192.168.1.50 > /data/adb/modules/gxr-alvr/pc
echo 41463       > /data/adb/modules/gxr-alvr/port   # optional
```

Remove `pc` to go back to auto/broadcast. `port` defaults to `41463`.

## If Steam Link (or another stream) stutters or pings spike

The relay sends a steady **60 datagrams/s**, and in auto/broadcast mode it fans
each one out to every interface's directed broadcast address *plus*
`255.255.255.255` — i.e. roughly **120 broadcast packets/s (~48 KB/s)**. The
bandwidth is trivial next to streamed video, but broadcast is expensive airtime:
Wi-Fi must send it at a low basic rate so every station and the access point
receives it, it is neither aggregated nor acknowledged, and it keeps the radio
out of power save. On a latency-sensitive stream such as **Steam Link** that can
surface as regular ping spikes.

**Fix: set the PC IP (section above).** The relay then unicasts 60 packets/s to
that one host and the broadcasts stop. There is no downside once you know the
PC's address — broadcast exists only so the first connection works without typing
an IP.

To confirm it is the relay and not your Wi-Fi: watch `ping <headset-ip>` (or the
Steam Link overlay) with the relay running, set the PC IP and press **Save &
restart relay**, then compare. If the spikes remain, disable the module in the
Manager and reboot — that separates "broadcast traffic" from "the ftrace probes"
(the relay holds system-wide uprobes on the perception pipeline).

## Action button

The Manager **Action** button runs `action.sh`: it stops the current relay
(incl. any stale copy from an older module version) and starts it again
immediately (no reboot). Use it after changing option files, or after a dev
binary push. **A module update does not restart the relay by itself** — tap
**Action** (or reboot) after installing a new version. The WebUI's *Save &
restart* already does that for you.

## Monitor / A–B (optional)

To watch the values on the PC while VRCFT keeps 41463, create a `mirror` file
with the extra port (the relay then sends a copy to `<target>:<port>`):

```sh
echo 41464 > /data/adb/modules/gxr-alvr/mirror
```

Set `tongue` to `0` to disable the extra `TongueRoll` parameter (A–B test).
Both are read at start — tap the **Action** button after changing them.

## Developer options (WebUI)

The WebUI's **Developer options** card exposes the sender's tuning knobs, each
with a one-line description — the mirror port, TongueRoll gain, the dilation keys
(`--pupil-dilation-gain`, `--pupil-smooth-ms`, `--pupil-min-conf`) and the gaze
keys. The gaze keys decide when an eye's direction is *trustworthy*: a
pitch-guarded blink test plus a pupil test relative to that eye's own recent size
(`--gaze-lid-low`, `--gaze-pupil-ratio`), a smoothing-only band for a narrowed
lid that the pupil test cannot see (`--gaze-soft-open`, `--gaze-soft-fmin`), and
the base filter speed (`--gaze-smooth-fmin`). Change a field and tap **Save &
restart relay**; a field left at its default is omitted, so future default changes
still reach you.

A nested **Advanced — firmware probes** section holds the firmware-specific
uprobe offsets and plugin paths. They exist so the module can track a firmware
change without a rebuild, but a wrong value stops face/eye tracking entirely —
leave them alone unless you know your firmware differs.

It writes the `devopts` file (one `--flag value` per line), which
`service.sh`/`action.sh` append to the launch command. The file is a **pure data
file**: values are restricted to `A–Za–z0–9 . _ , : / + - =`, so nothing can
escape into the shell. **Reset to defaults** clears it.

PC IP, UDP port, TongueRoll and Verbose have their own controls in the
**Connection** card above.

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
| `webui.sh` | helper for the Manager WebUI page (status / set / setdev / test) |
| `webroot/index.html` | the Manager WebUI page (PC IP, status, log, Developer options) |
| `customize.sh` | installer permissions + ui_print |
| `uninstall.sh` | stop the relay on uninstall |
| `sepolicy.rule` | allow the KernelSU root domain to use tracefs (Enforcing) |
| `module.prop` | module metadata |
| `pc`, `port`, `mirror`, `tongue`, `verbose`, `devopts`, `dev` | optional overrides (not shipped; create as needed). **These are preserved across module updates** — the installer copies them into the new version. `devopts` holds the WebUI's Developer-options flags. |

## Logs

`/data/local/tmp/gxr_alvr.log` (append). The relay also logs `armed …` /
`auto target …` lines at startup. Pass `--verbose` (dev) for the tuning trace
(~20 Hz gaze, 4 Hz pupil).

## Notes

- Firmware-specific uprobe offsets (firmware `I610UEU2AZF3`); the sender fails
  soft per probe if the firmware changes.
- The relay refuses to start a second copy (flock at
  `/data/local/tmp/gxr_alvr.lock`) and self-heals if tracefs is cleared under it.
- Wire format is fixed: `"UniExpr\0"` + 88 `float32` UE weights + an 11-float
  eye tail (99 floats, 396 B payload, 404 B datagram). Do not change it without
  updating the module.

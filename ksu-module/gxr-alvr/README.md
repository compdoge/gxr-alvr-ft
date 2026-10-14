# `gxr-alvr` — Galaxy XR face-tracking relay (KernelSU module)

Ships the headset-side sender `gxr_alvr` and starts it as root at boot. It reads
the Galaxy XR perception host through read-only ftrace uprobes and streams the
full face/eye parameter set to the PC on **UDP 41463**, for the VRCFaceTracking
module [`compdoge/VRCFT-ALVR-UE`](https://github.com/compdoge/VRCFT-ALVR-UE).

> **ALVR is NOT required.** Neither this module nor the PC side needs the ALVR
> streamer, the ALVR app, or any ALVR setup. "ALVR" is in the module id only
> because we reuse ALVR's `UniExpr` packet *format*.

**Install:** the zip goes into the KernelSU Manager, then **reboot** (installing
alone does not restart the relay). The PC-side module, the full walkthrough and
the troubleshooting notes — nothing arrives, auto-connect flaky, Wi-Fi
trackers/streaming stutter — are in the
[repo README](https://github.com/compdoge/gxr-root-ft#readme).

The rest of this file is reference.


## Option files

Every setting is an optional file in the module directory. All of them are
**preserved across module updates**. The WebUI writes them for you; the Manager
**Action** button (or a reboot) applies a change.

| file | purpose |
|---|---|
| `pc` | PC IPv4 address, or a comma-separated list of them. Absent = auto/broadcast. |
| `port` | UDP port, default `41463`. |
| `mirror` | extra port: also send a copy to `<target>:<port>` (for a monitor tool). |
| `tongue` | `0` disables the extra `TongueRoll` parameter. |
| `verbose` | enables the fast tuning log (see Logs). |
| `devopts` | the WebUI's Developer-options flags, one `--flag value` per line. A **pure data file**: values are restricted to `A-Za-z0-9 . _ , : / + - =`, so nothing can escape into the shell. |
| `dev` | opt-in dev override: prefer a binary at `/data/local/tmp/gxr_alvr` (see Dev mode). |

By hand (root):

```sh
echo 192.168.1.50 > /data/adb/modules/gxr-alvr/pc
echo 41463        > /data/adb/modules/gxr-alvr/port   # optional
```

## Action button

The Manager **Action** button runs `action.sh`: it stops the current relay
(including any stale copy from an older module version) and starts it again
immediately, with no reboot. Use it after changing option files or after a dev
binary push. **A module update does not restart the relay by itself** — tap
**Action** (or reboot) after installing a new version. The WebUI's *Save &
restart* does that for you.

## Developer options (WebUI)

The WebUI's **Developer options** card exposes the sender's tuning knobs, each
with a one-line description — the datagram rate (`--tx-hz`), the mirror port,
TongueRoll gain, the dilation keys (`--pupil-dilation-gain`, `--pupil-smooth-ms`,
`--pupil-min-conf`) and the gaze keys. The gaze keys decide when an eye's
direction is *trustworthy*: a pitch-guarded blink test plus a pupil test relative
to that eye's own recent size (`--gaze-lid-low`, `--gaze-pupil-ratio`), a
smoothing-only band for a narrowed lid that the pupil test cannot see
(`--gaze-soft-open`, `--gaze-soft-fmin`), and the base filter speed
(`--gaze-smooth-fmin`).

A nested **Advanced — firmware probes** section holds the firmware-specific uprobe
offsets and plugin paths. They exist so the module can track a firmware change
without a rebuild, but a wrong value stops face/eye tracking entirely — leave them
alone unless you know your firmware differs.

A field left at its default is omitted, so future default changes still reach you;
**Reset to defaults** clears the file. PC IP, UDP port, TongueRoll and Verbose have
their own controls in the **Connection** card above.

## Monitor / A–B (optional)

To watch the values on the PC while VRCFT keeps 41463, create a `mirror` file with
the extra port — the relay then also sends a copy to `<target>:<port>`:

```sh
echo 41464 > /data/adb/modules/gxr-alvr/mirror
```

Set `tongue` to `0` to disable the extra `TongueRoll` parameter (A–B test). Both
are read at start — tap the **Action** button after changing them.

## Dev mode (opt-in)

To iterate on the sender without reinstalling the module, create an empty `dev`
file and push a binary:

```sh
touch /data/adb/modules/gxr-alvr/dev
adb push gxr_alvr /data/local/tmp/gxr_alvr && adb shell chmod 755 /data/local/tmp/gxr_alvr
```

Now `service.sh`/`action.sh` prefer `/data/local/tmp/gxr_alvr`. Delete `dev` to
return to the bundled binary. (The dev override is opt-in because a stale
`/data/local/tmp/gxr_alvr` silently shadowing the module binary is a footgun for
shipped users.)

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
| `pc`, `port`, `mirror`, `tongue`, `verbose`, `devopts`, `dev` | optional overrides (not shipped — see Option files) |

## Logs

`/data/local/tmp/gxr_alvr.log` (append). The relay also logs `armed …` /
`auto target …` lines at startup. `verbose` gives the tuning trace (~20 Hz gaze,
4 Hz pupil).

## Notes

- Uprobe offsets are **firmware-specific** (firmware `I610UEU2AZF3`); the sender
  fails soft per probe if the firmware changes.
- The relay refuses to start a second copy (flock at
  `/data/local/tmp/gxr_alvr.lock`) and self-heals if tracefs is cleared under it.
- Wire format is fixed: `"UniExpr\0"` + 88 `float32` UE weights + an 11-float eye
  tail (99 floats, 396 B payload, 404 B datagram). Do not change it without
  updating the PC-side module.

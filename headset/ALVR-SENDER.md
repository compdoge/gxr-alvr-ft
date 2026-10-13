# gxr_alvr, headset sender for the VRCFT-ALVR-UE module

Uses the **ALVR VRCFT wire format**, so the PC receiver is the ALVR VRCFT module (`github.com/compdoge/VRCFT-ALVR-UE`) using its `UniExpr` record.

## Wire format (as parsed by `ALVRModule.ALVRModule.cs`)

- One **UDP datagram to port 41463** (`0xA1F7`), default.
- Datagram = concatenated records. Each record = **8-byte ASCII prefix** (NUL-padded) + little-endian `float32`s.
- `gxr_alvr` sends a single record:

| field | value |
|---|---|
| prefix | `"UniExpr\0"` (8 bytes) |
| payload | 88 `float` UE weights + 11 `float` eye tail = 99 floats (396 B) |
| total | 404 bytes |

The 88 weights are indexed by `VRCFaceTracking.Core.Params.Expressions.UnifiedExpressions` (`Max = 88`), in enum order. The 11 tail slots, in order (`UnifiedExprTracking.EyeTail`):

```
0 Version(=1)  1 LOpen  2 ROpen  3 LPupil  4 RPupil  5 MinDil  6 MaxDil  7 LPitch  8 LYaw  9 RPitch  10 RYaw
```

(Module maps `Gaze.x <- Pitch`, `Gaze.y <- Yaw`.)

## Destination: auto-broadcast (default) or pinned unicast

- **Auto (default):** with no `--pc`, the sender sends each datagram to the **directed broadcast of every up, broadcast-capable IPv4 interface** (Wi-Fi preferred, but not required) **plus the limited broadcast `255.255.255.255`**. The stock VRCFT-ALVR module binds `0.0.0.0:41463`, so it receives broadcast datagrams. **Zero configuration, no PC-side change.** The set is re-resolved while idle so a subnet change recovers.
- **Pinned:** `--pc <ip>` (or a comma-separated list of IPs) sends unicast to those PCs. The module scripts read a single IP from the module's `pc` file, which the **Manager WebUI** writes (see the module README).

Caveats: broadcast reaches every host on the LAN (a second PC running the module would also get the packets), and **many Wi-Fi access points filter broadcast or isolate clients** — that is the usual reason auto-connect "doesn't work sometimes". The reliable fix is to pin the PC's IP (via the module's Manager WebUI). If the PC is on a different subnet, you must pin it.

## What it computes (all from read-only host uprobes)

- **68 → UE** (`FACE68_TO_UE`, 66 pairs) — mirrors `RootedLinkFTModule.Face68Map`.
- **Un-clamped jaw** — `ue[JawOpen]` overridden from the pre-clamp jaw while the vendor tongue-clamp is active (ported from `RootedLinkFTModule.UpdateJaw`).
- **Openness** — `1 - clamp(EyesClosed + EyesClosed*Squint)` per eye.
- **Pupil** — per eye, smoothed, with a drift-based fallback. The probe's 4 detections are **idx 0/1 = left, 2/3 = right** (confirmed on-device by winking each eye: the closed lid collapses that eye's detections to ~1 mm while the other stays ~4 mm, and the closed eye's detections keep arriving). Each eye's largest candidate is taken (a lid occludes → a smaller fit) and low-pass filtered (`PUPIL_SMOOTH_MS = 350`, because one eye alone is noisier than the two-eye average). A wink is spotted by the eyes disagreeing (hysteresis + a shut lid confirming a collapse); the **open eye then drives the value as its drift from the last both-eyes-open pair average**, so dropping one lid neither *steps* the dilation nor injects that eye's noise. Only a full blink holds (last good value, never 0). The dilation envelope (min/max pupil) is what VRCFT normalizes against — the ALVR module feeds it our window every frame — so it grows **only** from frames where both eyes read cleanly and **both lids are clearly open** (`PUPIL_WINDOW_OPEN = 0.8`), and a new extreme must persist `PUPIL_WINDOW_DWELL_MS (300)` before it is committed. A squint, a blink or bad data (headset briefly off) can therefore no longer widen the window and flatten the whole dilation range. The emitted window is forced to strictly contain the emitted pupil. Tuning: `--pupil-dilation-gain` (raise toward 1.0 for the full range), `--pupil-smooth-ms`, `--pupil-max-jump` (per-eye jump that counts as occlusion), `--pupil-relock-ms` (forget an eye after this long with no reading), `--pupil-open-gate` (per-eye lid threshold), `--pupil-min-conf`.
- **Gaze** — per-eye `yaw = atan2(x,z)`, `pitch = atan2(-yDown,z)` from the host DNN direction (`0x4be378`).
- **Tongue** — the 5 basic DOFs come from the 68-vector (face 63–67). `TongueRoll` is derived from the 11-D `predicted_local_tongue_expression` (uprobe `0x358a34`) via the exact 11→5 mapper plus a DOF-leak-free calibration axis (`--tongue-extras`, default on). Shape (Flat/Squish) and Twist were dropped after live testing — the 11-D model can't separate them reliably.

Offsets (firmware `I610UEU2AZF3`): face `0x3589f8`, jaw `0xf09aa4`, eyedir `0x4be378`, pupil `0x4bef60`, tongue `0x358a34`.

## Build / run

```sh
tools/build_alvr.sh                      # -> tools/gxr_alvr
# on device, as root (module does this automatically at boot):
/data/local/tmp/gxr_alvr                       # auto-connect (broadcast)
/data/local/tmp/gxr_alvr --pc <pc-ip>          # pin the PC
/data/local/tmp/gxr_alvr --selftest            # one synthetic packet, no root
```

## Flags

`--pc <ip[,ip..]>`, `--port`, `--verbose` (1 Hz gaze trace + a 4 Hz per-detection pupil dump `idx:d<mm>/c<conf>/x<centre>/a<age-ms>`), `--mirror-port`, `--tongue-extras`, `--tongue-gain-roll`, `--pupil-dilation-gain`, `--pupil-min-conf`, `--pupil-smooth-ms`, `--pupil-open-gate`, `--pupil-max-jump`, `--pupil-relock-ms`.

Dev-only flags (not exposed in the WebUI; the axis mapping is fixed and verified): `--gaze-swap`, `--gaze-invert-x`, `--gaze-invert-y`, `--tongue-gate`, `--selftest`, and the firmware probes `--pupil-offset`, `--eye-dir-offset`, `--face-bs-offset`, `--jaw-offset`, `--tongue-offset`, `--eye-plugin`, `--face-plugin`. There is **no `--pupil-mm-scale`** any more — the ellipse→mm factor is a fixed constant, because VRCFT normalizes the sent diameter against the window we also send, so the scale cancels out.
Run `gxr_alvr --help` for the list.

The old runtime `/data/local/tmp/gxr_alvr.conf` (`swap`/`invert_x`/`invert_y`) is **no longer polled**; the now-verified gaze mapping is the compile-time default, and the same knobs are the `--gaze-*` flags above.
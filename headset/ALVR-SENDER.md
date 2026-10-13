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
- **Openness** — `1 - clamp(EyesClosed, 0, 1)` per eye. Face indices 12/13 are the **raw** eyes-closed value: the device folds the lid tightener into it later, in its own `EyeWinkPostprocessor`, and our uprobe reads it before that (RE: `results/eye-postprocessing-re.md`). Folding `EyesClosed*Squint` in here as well saturated openness at `closed ≈ 0.8–0.9` and made the lid snap shut over the last ~20% — so it is emitted raw, and the squint travels separately (face 28/29 → `UE_EyeSquintLeft/Right`).
- **Pupil** — per eye, smoothed, with a drift-based fallback. The probe's 4 detections are **idx 0/1 = left, 2/3 = right** (confirmed on-device by winking each eye: the closed lid collapses that eye's detections to ~1 mm while the other stays ~4 mm, and the closed eye's detections keep arriving). Each eye's largest candidate is taken (a lid occludes → a smaller fit) and low-pass filtered (`PUPIL_SMOOTH_MS = 350`, because one eye alone is noisier than the two-eye average). A wink is spotted by the eyes disagreeing (hysteresis + a shut lid confirming a collapse); the **open eye then drives the value as its drift from the last both-eyes-open pair average**, so dropping one lid neither *steps* the dilation nor injects that eye's noise. Only a full blink holds (last good value, never 0). The dilation envelope (min/max pupil) is what VRCFT normalizes against — the ALVR module feeds it our window every frame — so it grows **only** from frames where both eyes read cleanly and **both lids are clearly open** (`PUPIL_WINDOW_OPEN = 0.8`), and a new extreme must persist `PUPIL_WINDOW_DWELL_MS (300)` before it is committed. A squint, a blink or bad data (headset briefly off) can therefore no longer widen the window and flatten the whole dilation range. The emitted window is forced to strictly contain the emitted pupil. Tuning: `--pupil-dilation-gain` (raise toward 1.0 for the full range), `--pupil-smooth-ms`, `--pupil-max-jump` (per-eye jump that counts as occlusion), `--pupil-relock-ms` (forget an eye after this long with no reading), `--pupil-open-gate` (per-eye lid threshold), `--pupil-min-conf`.
- **Gaze** — per-eye `yaw = atan2(x,z)`, `pitch = atan2(-yDown,z)` from the host DNN direction (`0x4be378`), then conditioned with a per-eye One Euro filter (`GAZE_BETA = 0.01`, `GAZE_DCUT = 1.0 Hz`).

  **Whether a direction is trusted** is decided per eye from the lid *and* the pupil, because neither alone works. The device's face model collapses `eyes_closed` to 1.0 as a function of gaze **pitch** — looking down only ~11–23° already reports openness `0.00` — so an openness-only gate froze clean gaze on 48–84% of look-down frames (the "tracking freezes when I look down" report). Pupil size alone fails the other way: a bright scene legitimately constricts it. Combined, with the pupil test **relative to that eye's own recent size** so brightness cancels:

  - `open < GAZE_BLINK_SHUT (0.15)` **and** `pitch > GAZE_PITCH_GUARD (-20°)` → a real blink → **frozen**;
  - `open < GAZE_LID_LOW (0.70)` **and** `pupil < GAZE_PUPIL_RATIO (0.55) ×` that eye's P90 pupil over the last 60 s (seeded only from frames whose lid is clearly open, so a squint cannot poison it) → the pupil is occluded → **frozen**;
  - `open < GAZE_SOFT_OPEN (0.75)` **and** `pitch > guard` → the lid is narrowed but the pupil still looks normal, which the pupil test cannot see: **smooth much harder** (`GAZE_SOFT_FMIN = 0.35 Hz`) rather than freeze, so a droopy or badly-fitted baseline cannot lock the gaze up;
  - otherwise → normal cutoff `GAZE_FMIN = 1.5 Hz`.

  The pitch guard keeps deep look-down (where openness reads `0.00`) out of both the blink test and the soft band, so the gaze stays responsive while looking down; and because the pupil test is a ratio, brightness cancels. Validated offline on real captures: look-down false positives 48–84% → 0–9%, bright scene 0%, blink recall 100% at 0 frames latency (`results/gaze-reliability-method.md`); a field capture then measured the worst *untouched* jitter at openness 0.65–0.75 (raw p90 ~14°/frame) with the pupil still at 0.94 of baseline — which is what the smoothing band above was added for. The device does none of this — the RE (`results/eye-postprocessing-re.md`) found its own gaze filter reports `FILTER_TYPE_NONE` and there is no lid→gaze gate anywhere — so a partly closed eyelid's corrupted direction used to be forwarded as-is (the "gaze jitters when I squint" report). Tuning: `--gaze-lid-low`, `--gaze-pupil-ratio`, `--gaze-soft-open` (must sit just below the resting openness, or the gaze is permanently laggy), `--gaze-soft-fmin`, `--gaze-smooth-fmin`, `--gaze-smooth-beta`, `--gaze-smooth-dcut`. `--gaze-hold-open` is accepted and ignored (the freeze threshold was removed in v1.3.4) so a `devopts` file carried across an update cannot brick the relay.
- **Tongue** — the 5 basic DOFs come from the 68-vector (face 63–67). `TongueRoll` is derived from the 11-D `predicted_local_tongue_expression` (uprobe `0x358a34`) via the exact 11→5 mapper plus a DOF-leak-free calibration axis (`--tongue-extras`, default on). Shape (Flat/Squish) and Twist were dropped after live testing — the 11-D model can't separate them reliably.

Offsets (firmware `I610UEU2AZF3`): face `0x3589f8`, jaw `0xf09aa4`, eyedir `0x4be378`, pupil `0x4bef60`, tongue `0x358a34`.

## Transmit / link robustness

The sender transmits on **its own clock** (`TX_HZ = 60`), not only when the device
calls the probed function. Replaying the last computed frame keeps the link alive
while the perception pipeline idles (headset off, streamed app unfocused, display
asleep), so the PC/VRCFT never sees the module vanish; after `TX_HOLD_MS = 2000` of
silence it relaxes the face to neutral and centres the gaze rather than replaying a
frozen expression. Health/self-heal checks run on their own 3 s cadence.

## Build / run

```sh
tools/build_alvr.sh                      # -> tools/gxr_alvr
# on device, as root (module does this automatically at boot):
/data/local/tmp/gxr_alvr                       # auto-connect (broadcast)
/data/local/tmp/gxr_alvr --pc <pc-ip>          # pin the PC
/data/local/tmp/gxr_alvr --selftest            # one synthetic packet, no root
```

## Flags

`--pc <ip[,ip..]>`, `--port`, `--verbose` (a 20 Hz gaze trace `t/ok/held/open/pu/Ldir/Rdir/Lg/Rg` + a 4 Hz per-detection pupil dump `idx:d<mm>/c<conf>/x<centre>/a<age-ms>`), `--mirror-port`, `--tongue-extras`, `--tongue-gain-roll`, `--pupil-dilation-gain`, `--pupil-min-conf`, `--pupil-smooth-ms`, `--pupil-open-gate`, `--pupil-max-jump`, `--pupil-relock-ms`, `--gaze-lid-low`, `--gaze-pupil-ratio`, `--gaze-soft-open`, `--gaze-soft-fmin`, `--gaze-smooth-fmin`.

Dev-only flags (not exposed in the WebUI; the axis mapping is fixed and verified): `--gaze-swap`, `--gaze-invert-x`, `--gaze-invert-y`, `--tongue-gate`, `--gaze-blink-shut`, `--gaze-pitch-guard`, `--gaze-smooth-beta`, `--gaze-smooth-dcut`, `--selftest`, and the firmware probes `--pupil-offset`, `--eye-dir-offset`, `--face-bs-offset`, `--jaw-offset`, `--tongue-offset`, `--eye-plugin`, `--face-plugin`. There is **no `--pupil-mm-scale`** any more — the ellipse→mm factor is a fixed constant, because VRCFT normalizes the sent diameter against the window we also send, so the scale cancels out.
Run `gxr_alvr --help` for the list.

The old runtime `/data/local/tmp/gxr_alvr.conf` (`swap`/`invert_x`/`invert_y`) is **no longer polled**; the now-verified gaze mapping is the compile-time default, and the same knobs are the `--gaze-*` flags above.
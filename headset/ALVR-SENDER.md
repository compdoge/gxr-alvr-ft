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

## Destination: auto-connect (default) or a pinned PC

- **Auto (default):** with no `--pc`, the sender find's the active interface's **directed broadcast address** (from the Wi-Fi IPv4 netmask) and sends there. The stock VRCFT-ALVR module binds `0.0.0.0:41463`, so it receives broadcast datagrams. **Zero configuration, no PC-side change**. The address is re-resolved when idle so a subnet change recovers.
- **Pinned:** `--pc <ip>` sends unicast to that PC. The module scripts read it from the module's `pc` file (see the module README).

Caveats: broadcast reaches every host on the LAN (a second PC running the module would also get the packets); if the PC is on a different subnet, pick it with `--pc`/`pc`.

## What it computes (all from read-only host uprobes)

- **68 → UE** (`FACE68_TO_UE`, 66 pairs) — mirrors `RootedLinkFTModule.Face68Map`.
- **Un-clamped jaw** — `ue[JawOpen]` overridden from the pre-clamp jaw while the vendor tongue-clamp is active (ported from `RootedLinkFTModule.UpdateJaw`).
- **Openness** — `1 - clamp(EyesClosed + EyesClosed*Squint)` per eye.
- **Pupil** — `2*max(ellipse axes) * PUPIL_MM_SCALE (24)`, with a tracked min/max dilation window (`PUPIL_DILATION_GAIN = 0.75`).
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

`--pc`, `--port`, `--verbose` (1 Hz gaze trace), `--gaze-swap`, `--gaze-invert-x`, `--gaze-invert-y`, `--selftest`, `--pupil-mm-scale`, `--pupil-dilation-gain`, `--pupil-offset`, `--eye-dir-offset`, `--face-bs-offset`, `--jaw-offset`, `--eye-plugin`, `--face-plugin`. 
Run `gxr_alvr --help` for the list.

The old runtime `/data/local/tmp/gxr_alvr.conf` (`swap`/`invert_x`/`invert_y`) is **no longer polled**; the now-verified gaze mapping is the compile-time default, and the same knobs are the `--gaze-*` flags above.
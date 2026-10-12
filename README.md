# gxr-alvr-ft

Galaxy XR to PC **full face/eye tracking** link. The headset runs a root sender that reads the Galaxy XR perception host through **read-only ftrace uprobes** and streams the complete face/eye parameter set to a PC running **VRCFaceTracking**, so a VRChat avatar's face and eyes follow yours.

This repo is the **headset side**:

- `headset/`: the C sender `gxr_alvr` (+ build script, wire-format doc, tools).
- `ksu-module/gxr-alvr/`: the **KernelSU module** that starts the sender as
  root at boot, for normal users (no adb at runtime).

The **PC side** is the VRCFaceTracking module [`compdoge/VRCFT-ALVR-UE`](https://github.com/compdoge/VRCFT-ALVR-UE). This is where this sender sends its `UniExpr` wire format.

Requires **root** (KernelSU/DFRoot) on the headset. The sender only *reads*; it never patches or injects into vendor code, and it carries numeric signals only (no camera imagery).

## Install (normal user)

1. Install the **KernelSU module** in the Manager and reboot. Pack `ksu-module/gxr-alvr/` as a zip, or grab a release asset.
2. Install/enable the **VRCFT-ALVR-UE** module in VRCFaceTracking on the PC.
3. Done. **No PC IP to type.** With no `pc` file the sender auto-connects by sending each datagram to the **Wi-Fi LAN broadcast address**; the VRCFT-ALVR module binds `0.0.0.0:41463`, so it receives broadcast too.

Requirements: headset and PC on the same LAN; Windows Firewall must allow inbound **UDP 41463** for VRCFaceTracking.

## Benefits

Using this sender over a generic OpenXR route currently gives you:

1. Eye dilation
2. Individual eye gaze
3. Jaw open not being clamped to >0.5 when tongue out
   
With hopefully more to come!

### Pin a specific PC (override)

Only needed if the PC is on another subnet, or to avoid broadcasting:

```sh
echo 192.168.1.50 > /data/adb/modules/gxr-alvr/pc     # as root
echo 41463        > /data/adb/modules/gxr-alvr/port    # optional
# then tap the module's Action button in the KernelSU Manager (or reboot)
```

Remove `pc` to return to auto/broadcast.

## Build the sender

Requires an Android NDK (r29 tested):

```sh
NDK=/path/to/android-ndk-r29 headset/build_alvr.sh   # -> headset/gxr_alvr
```

## Build the module zip

The module has a prebuilt `gxr_alvr`. To rebuild it from source, copy the new built binary in first, then zip the module dir (with `module.prop` at the zip root):

```sh
cp headset/gxr_alvr ksu-module/gxr-alvr/gxr_alvr
( cd ksu-module/gxr-alvr && zip -r ../gxr-alvr.zip . )
```

The bundled binary is committed so users can install without a toolchain; if you change `headset/gxr_alvr.c`, rebuild and re-copy it.

## Wire format

One UDP datagram to port **41463** (`0xA1F7`) = `"UniExpr\0"` + 88 `float32` Unified-Expression weights + an 11-float eye tail (openness, pupil, per-eye pitch/yaw) → **404 bytes**. Read `headset/ALVR-SENDER.md` and the VRCFT-ALVR-UE module for details.

The 88 weights are indexed by VRCFaceTracking's `UnifiedExpressions` (`Max = 88`) and the eye tail (in order): `Version, LOpen, ROpen, LPupil, RPupil, MinDil, MaxDil, LPitch, LYaw, RPitch, RYaw`.

## Notes

- Uprobe offsets are **firmware-specific** (developed on Galaxy XR `SM-I610`, firmware `I610UEU2AZF3`); the sender fails soft per probe if the firmware changes.
- The sender refuses to start a second copy (`flock`) and self-heals if tracefs is cleared under it.
- Eyes can be slightly jittery on the current host DNN output.

## License

MIT: see [`LICENSE`](LICENSE).

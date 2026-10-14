# gxr-root-ft

Galaxy XR -> PC **full face/eye tracking** for VRChat. A rooted headset streams its
complete face/eye parameter set to a PC running **VRCFaceTracking**, so your
avatar's face and eyes follow yours.

This repo is the **headset side** - the sender and the KernelSU module that runs
it at boot. The PC side is a separate VRCFaceTracking module (installed in step 2
below).

> ### ALVR is NOT required
> You do **not** need the ALVR streamer, the ALVR app, or any ALVR setup.
> "ALVR" appears in these names only because we reuse ALVR's `UniExpr` packet
> *format*. No ALVR software is involved.

## What you need

- **Headset:** Galaxy XR (`SM-I610`) with **root** (KernelSU or DFRoot) and the
  **KernelSU Manager** installed.
- **PC:** [VRCFaceTracking](https://github.com/benaclejames/VRCFaceTracking)
  installed and working.
- Headset and PC on the **same Wi-Fi / LAN**.

No ALVR. No adb. No terminal.


## Install (normal user)

Two installs, one per machine. Nothing to configure afterwards.

1. **Headset - install the KernelSU module.** Download `gxr-alvr-vX.Y.Z.zip` from
   the **Releases** page, install it in the KernelSU Manager, then **reboot**.
   The sender starts by itself at boot. (Installing on its own does not restart
   the relay - the reboot is what starts it.)
2. **PC - install the VRCFaceTracking module.** Download `ALVRModule.zip` from the
   [VRCFT module's latest release](https://github.com/compdoge/VRCFT-ALVR-UE/releases/latest),
   unzip it, and copy **`ALVRModule.dll`** into
   `%APPDATA%\VRCFaceTracking\CustomLibs` (paste that path into the Explorer
   address bar). Restart VRCFaceTracking and check that **ALVR Module** is listed
   and enabled. It shows up as *ALVR Module* - that is the face-tracking module,
   **not** the ALVR streamer.
3. **Play.** Launch VRChat with face tracking enabled. There is **no PC IP to
   type**: the headset finds the PC by broadcasting.


## If it doesn't work

- **Nothing arrives, or it worked and then stopped.** Check the inbox is open:
  Windows Firewall must allow inbound **UDP 41463** for VRCFaceTracking, and the
  headset and PC must be on the same network.
- **Auto-connect is flaky.** Some access points filter broadcast, and a PC on
  another subnet will never hear it. Pin the PC's IP to switch to unicast ->
  [Set a custom PC IP](#set-a-custom-pc-ip).
- **Wi-Fi trackers or a stream (e.g. Steam Link) stutter.** The relay sends 60
  broadcast datagrams/s, continuously (also while nothing is tracking), and
  broadcast is the expensive kind of Wi-Fi traffic.
  Either lower the datagram rate in the module's **Developer options -> `--tx-hz`**,
  or pin the PC IP to switch to unicast and keep the full rate.


## Set a custom PC IP

**Auto-connect is the default** - with no config the sender broadcasts to the LAN
broadcast address, and the PC module (bound `0.0.0.0:41463`) receives that. Set a
PC explicitly if auto-connect is flaky, if the PC is on another subnet, or to
switch the stream to unicast.

**Easiest: the module's WebUI.** KernelSU Manager -> **Modules** -> *Galaxy XR face
tracking relay* -> tap the **Open** button (or tap the card itself). Enter the PC's
IPv4 address and tap **Save & restart relay**. Leave the field empty and Save to
go back to auto/broadcast. The page also shows live status (target, pid, headset
IP, log tail), a **Test** button that pings the PC from the headset, and
**Refresh** to re-read status.

**Or by hand (root):**

```sh
echo 192.168.1.50 > /data/adb/modules/gxr-alvr/pc     # as root
echo 41463        > /data/adb/modules/gxr-alvr/port    # optional
# then tap the module's Action button in the KernelSU Manager (or reboot)
```

Delete `pc` to return to auto/broadcast. `port` defaults to `41463`. These option
files (`pc`, `port`, `mirror`, `tongue`, `verbose`, `dev`) are **preserved across
module updates**.


## Benefits

Using this sender over a generic OpenXR route currently gives you:

1. Eye dilation
2. Individual eye gaze
3. Jaw open not being clamped to >0.5 when tongue out
4. Tongue roll

With hopefully more to come!


## For developers

Everything below is about building and modifying the sender. If you just want to
use it, stop here.

### How it works

The headset runs `gxr_alvr` as **root**. It reads the Galaxy XR perception host
through **read-only ftrace uprobes** - it only *reads*; it never patches or
injects into vendor code, and it carries numeric signals only (no camera imagery).

Repo layout:

- `headset/` - the C sender `gxr_alvr` (+ build script, wire-format doc, tools).
- `ksu-module/gxr-alvr/` - the KernelSU module that starts the sender at boot.

### Wire format

One UDP datagram to port **41463** (`0xA1F7`) = `"UniExpr\0"` + 88 `float32`
Unified-Expression weights + an 11-float eye tail -> **404 bytes**.

The 88 weights are indexed by VRCFaceTracking's `UnifiedExpressions` (`Max = 88`).
The eye tail, in order:
`Version, LOpen, ROpen, LPupil, RPupil, MinDil, MaxDil, LPitch, LYaw, RPitch, RYaw`.

Read `headset/ALVR-SENDER.md` for the signal notes, and the VRCFT-ALVR-UE module
for the receiving side.

### Build the sender

Requires an Android NDK (r29 tested):

```sh
NDK=/path/to/android-ndk-r29 headset/build_alvr.sh   # -> headset/gxr_alvr
```

### Build the module zip

The module has a prebuilt `gxr_alvr`. To rebuild it from source, copy the new
built binary in first, then zip the module dir (with `module.prop` at the zip
root):

```sh
cp headset/gxr_alvr ksu-module/gxr-alvr/gxr_alvr
python tools/make_module_zip.py ksu-module/gxr-alvr gxr-alvr.zip   # any OS
# or, on Linux/macOS:
( cd ksu-module/gxr-alvr && zip -r ../gxr-alvr.zip . )
```

**Entry names must use forward slashes.** KernelSU extracts the zip on Linux, so a
Windows tool that writes `webroot\index.html` (e.g. `ZipFile.CreateFromDirectory`
or `Compress-Archive`) creates a *literal file with a backslash* at the module
root instead of a `webroot/` directory - ksud then reports `web=false` and the
module's WebUI never shows. Use `tools/make_module_zip.py` (writes `/` explicitly)
or POSIX `zip`.

The bundled binary is committed so users can install without a toolchain; if you
change `headset/gxr_alvr.c`, rebuild and re-copy it.

### Notes

- Uprobe offsets are **firmware-specific** (developed on Galaxy XR `SM-I610`,
  firmware `I610UEU2AZF3`); the sender fails soft per probe if the firmware
  changes.
- The sender refuses to start a second copy (`flock`) and self-heals if tracefs is
  cleared under it.
- Eyes can be slightly jittery on the current host DNN output.


## License

MIT: see [`LICENSE`](LICENSE).

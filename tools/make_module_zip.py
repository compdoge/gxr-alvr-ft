#!/usr/bin/env python3
"""Package a KernelSU module directory into a release zip.

WHY THIS EXISTS
---------------
KernelSU extracts the module zip on Android (Linux), so every entry name MUST
use "/" as the path separator. Windows' ZipFile.CreateFromDirectory / Compress-
Archive write "\\", which turns e.g. "webroot/index.html" into a *literal
filename* containing a backslash at the module root. KernelSU then finds no
"webroot" directory, reports web=false, and the module's WebUI never appears
(the card just says "action").

This script walks the tree and writes forward-slash entry names explicitly, so
nested paths install correctly regardless of the host OS.

Usage:
    python make_module_zip.py <module-dir> <out.zip>
"""
import os
import sys
import zipfile


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    root, out = argv[1], argv[2]
    if not os.path.isfile(os.path.join(root, "module.prop")):
        print(f"error: {root} does not look like a module dir (no module.prop)")
        return 2
    n = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames.sort()
            for fn in sorted(filenames):
                full = os.path.join(dirpath, fn)
                arc = os.path.relpath(full, root).replace(os.sep, "/")
                if "\\" in arc:  # belt-and-suspenders
                    raise SystemExit(f"refusing to write backslash entry: {arc}")
                z.write(full, arc)
                n += 1
    print(f"wrote {out}: {n} files, {os.path.getsize(out)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

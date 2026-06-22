#
# gzip_assets.py — pre-uploadfs build hook
#
# The web UI in data/ is served from LittleFS by an offline AP. Static assets
# (HTML/CSS/JS) compress ~65%, cutting both flash-image size and over-WiFi
# transfer. The device firmware (webBackend serveFile) already prefers a
# precompressed `<name>.gz` twin and serves it with `Content-Encoding: gzip`.
#
# This hook keeps data/ as the editable source of truth: on every filesystem
# image build it stages a copy where static assets are replaced by their .gz
# twin, then points the LittleFS image builder at that staging dir. Nothing in
# data/ is mutated, so .gz files can never go stale.
#
# Runtime-mutable files (config.json, plants.json) are copied verbatim — they
# are opened raw by the backend, not through serveFile, and must stay readable.
#
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)
import os
import gzip
import shutil

# Extensions worth compressing. Everything else is copied unchanged.
COMPRESS_EXT = (".html", ".css", ".js")
# Files that must remain raw even if their extension matches above.
KEEP_RAW = {"config.json", "plants.json"}

SRC_DATA = env.subst("$PROJECT_DATA_DIR")
STAGE_DIR = os.path.join(env.subst("$BUILD_DIR"), "data_gz")


def stage_compressed_assets(*args, **kwargs):
    if not os.path.isdir(SRC_DATA):
        print("[gzip_assets] no data dir at %s — nothing to stage" % SRC_DATA)
        return

    if os.path.isdir(STAGE_DIR):
        shutil.rmtree(STAGE_DIR)
    os.makedirs(STAGE_DIR, exist_ok=True)

    compressed, copied = 0, 0
    for root, _dirs, files in os.walk(SRC_DATA):
        rel = os.path.relpath(root, SRC_DATA)
        out_root = STAGE_DIR if rel == "." else os.path.join(STAGE_DIR, rel)
        os.makedirs(out_root, exist_ok=True)
        for name in files:
            src = os.path.join(root, name)
            if name.lower().endswith(COMPRESS_EXT) and name not in KEEP_RAW:
                dst = os.path.join(out_root, name + ".gz")
                with open(src, "rb") as fi, gzip.open(dst, "wb", compresslevel=9) as fo:
                    shutil.copyfileobj(fi, fo)
                compressed += 1
                print("[gzip_assets]  gz  %s -> %.0f%%" % (
                    name,
                    100.0 * os.path.getsize(dst) / max(1, os.path.getsize(src)),
                ))
            else:
                shutil.copy2(src, os.path.join(out_root, name))
                copied += 1
    print("[gzip_assets] staged %d compressed + %d raw -> %s"
          % (compressed, copied, STAGE_DIR))


# Redirect the LittleFS image source to the staging dir, and populate it just
# before the image is assembled. Firmware-only builds (`pio run`) never build
# the fs image, so the hook does no work there.
env.Replace(PROJECT_DATA_DIR=STAGE_DIR)
env.AddPreAction("$BUILD_DIR/littlefs.bin", stage_compressed_assets)

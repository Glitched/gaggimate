#
# PlatformIO pre-build hook (display envs).
#
# The web UI is embedded into the firmware app image (GM-106). The real bundle
# is produced by scripts/build_webui.sh (npm build -> gzip -> embed_webui.py) and
# lands, git-ignored, in src/display/webassets/. That step is the source of truth
# and is run explicitly in CI and locally when the UI changes.
#
# This hook only guarantees the build can compile *without* a prior web build: if
# the generated manifest is missing it drops in an empty stub so a bare
# `pio run -e display` still links (serving an empty UI). It never overwrites a
# real bundle.
#
import os
import subprocess
import sys

Import("env")  # noqa: F821 -- provided by PlatformIO/SCons

project_dir = env["PROJECT_DIR"]  # noqa: F821
out_dir = os.path.join(project_dir, "src", "display", "webassets")
manifest = os.path.join(out_dir, "web_ui_manifest.h")
packer = os.path.join(project_dir, "scripts", "embed_webui.py")

if not os.path.isfile(manifest):
    print("embed_webui_pre: no web bundle found, writing stub (run build_webui.sh for the real UI)")
    subprocess.check_call([sys.executable, packer, "--out", out_dir, "--stub"])

# The bundle reaches the firmware through .incbin, which no dependency scanner
# can see: SCons decides whether to reassemble by hashing the .S file, and that
# file does not change when the blob it pulls in does. embed_webui.py stamps the
# blob's size and digest into the generated src/display/webassets/web_ui_blob.S
# to force a rebuild there, but the simulator uses its own hand-written stub
# (sim/web/web_ui_blob_sim.S) which has no such stamp -- so it silently kept
# serving whatever bundle was current the first time it was assembled.
#
# Drop any blob object older than the bundle it is supposed to contain.
# scripts/check_webui_blob.py is the post-link backstop for the device builds.
blob = os.path.join(out_dir, "web_ui.bin")
build_dir = env.subst("$BUILD_DIR")  # noqa: F821
if os.path.isfile(blob) and os.path.isdir(build_dir):
    blob_mtime = os.path.getmtime(blob)
    for root, _dirs, files in os.walk(build_dir):
        for name in files:
            if not name.startswith("web_ui_blob") or not name.endswith(".o"):
                continue
            obj = os.path.join(root, name)
            if os.path.getmtime(obj) < blob_mtime:
                print("embed_webui_pre: %s predates web_ui.bin, removing so it reassembles" % name)
                os.remove(obj)

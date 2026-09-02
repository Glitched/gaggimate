import datetime
import os
import subprocess

Import("env")  # noqa: F821 -- provided by PlatformIO/SCons

# `git describe` output becomes BUILD_GIT_VERSION verbatim; lib/OTA's
# from_string() splits it on "." to compare against releases, which is why the
# non-semver `nightly` and `db` tags are excluded (see CLAUDE.md, Conventions).
DESCRIBE = ["git", "describe", "--tags", "--dirty", "--exclude", "nightly", "--exclude", "db"]
UNVERSIONED = "0.0.0-unversioned"


def get_firmware_specifier_build_flag():
    ret = subprocess.run(DESCRIBE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    build_version = ret.stdout.strip()
    if ret.returncode != 0 or not build_version:
        reason = ret.stderr.strip() or "empty output"
        if os.environ.get("GM_ALLOW_UNVERSIONED") == "1":
            build_version = UNVERSIONED
            print("auto_firmware_version: git describe failed (%s); GM_ALLOW_UNVERSIONED=1, using %s"
                  % (reason, build_version))
        else:
            print("\n*** auto_firmware_version: `%s` failed (%s) ***" % (" ".join(DESCRIBE), reason))
            print("    BUILD_GIT_VERSION would be empty and the firmware would report no version.")
            print("    Usually this is a shallow clone with no reachable tag: run")
            print("    `git fetch --tags --unshallow` (or check out a tagged history).")
            print("    To build anyway, set GM_ALLOW_UNVERSIONED=1 and the version becomes %s.\n" % UNVERSIONED)
            env.Exit(1)  # noqa: F821
    build_flag = "#define BUILD_GIT_VERSION \"" + build_version + "\""
    print("Build version: " + build_version)
    return build_flag


def get_time_specifier_build_flag():
    build_timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    build_flag = "#define BUILD_TIMESTAMP \"" + build_timestamp + "\""
    print("Build date: " + build_timestamp)
    return build_flag


with open('src/version.h', 'w') as f:
    f.write(
        '#pragma once\n' +
        '#ifndef GIT_VERSION_H\n' +
        '#define GIT_VERSION_H\n' +
        get_firmware_specifier_build_flag() + '\n' +
        get_time_specifier_build_flag() + '\n'
        '#endif\n'
    )

Import("env")
import shutil
import hashlib
import os
import re
import json
from datetime import datetime, timezone

def extract_from_main():
    main_cpp = os.path.join(env.get("PROJECT_DIR"), "src", "main.cpp")
    version = None
    version_url = None
    with open(main_cpp, "r") as f:
        for line in f:
            if version is None:
                m = re.search(r'const char\s*\*\s*CURRENT_VERSION\s*=\s*"([^"]+)"', line)
                if m:
                    version = m.group(1)
            if version_url is None:
                m = re.search(r'const char\s*\*\s*VERSION_URL\s*=\s*"([^"]+)"', line)
                if m:
                    version_url = m.group(1)
            if version and version_url:
                break
    if not version:
        raise RuntimeError("Could not find CURRENT_VERSION in main.cpp!")
    if not version_url:
        raise RuntimeError("Could not find VERSION_URL in main.cpp!")
    return version, version_url

def after_build(source, target, env):
    firmware_path = str(target[0])
    project_dir   = env.get("PROJECT_DIR")
    firmware_dir  = os.path.join(project_dir, "firmware")

    # Wipe the firmware directory so no stale binaries linger
    if os.path.exists(firmware_dir):
        shutil.rmtree(firmware_dir)
    os.makedirs(firmware_dir)
    print("[OTA] Cleared firmware/")

    version, version_url = extract_from_main()
    print(f"[OTA] Version : {version}")
    print(f"[OTA] Base URL: {version_url}")

    # Versioned binary name, e.g. firmware_1.0.3.bin
    bin_name    = f"firmware_{version}.bin"
    firmware_out = os.path.join(firmware_dir, bin_name)
    shutil.copy2(firmware_path, firmware_out)
    print(f"[OTA] Copied → firmware/{bin_name}")

    with open(firmware_out, "rb") as f:
        sha256 = hashlib.sha256(f.read()).hexdigest()
    print(f"[OTA] SHA-256 : {sha256}")

    # Derive the firmware download URL from VERSION_URL
    # e.g. .../firmware/version.json → .../firmware/firmware_1.0.3.bin
    firmware_url = version_url.rsplit("/", 1)[0] + "/" + bin_name

    build_date = datetime.now(timezone.utc).strftime("%d.%m.%Y @ %H:%M")

    version_json = {
        "version":      version,
        "firmware_url": firmware_url,
        "build_date":   build_date,
        "sha256":       sha256,
    }
    json_path = os.path.join(firmware_dir, "version.json")
    with open(json_path, "w") as f:
        json.dump(version_json, f, indent=2)
        f.write("\n")
    print(f"[OTA] version.json written:")
    print(f"      version      : {version}")
    print(f"      firmware_url : {firmware_url}")
    print(f"      build_date   : {build_date}")
    print(f"      sha256       : {sha256}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)

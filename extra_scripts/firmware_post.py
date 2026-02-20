Import("env")
import shutil
import hashlib
import os
import re
import json

def extract_version_from_main():
    main_cpp = os.path.join(env.get("PROJECT_DIR"), "src", "main.cpp")
    version = None
    with open(main_cpp, "r") as f:
        for line in f:
            m = re.search(r'const char\s*\*\s*CURRENT_VERSION\s*=\s*"([^"]+)"', line)
            if m:
                version = m.group(1)
                break
    if not version:
        raise RuntimeError("Could not find CURRENT_VERSION in main.cpp!")
    return version

def after_build(source, target, env):
    firmware_path = str(target[0])
    project_dir = env.get("PROJECT_DIR")
    firmware_dir = os.path.join(project_dir, "firmware")
    os.makedirs(firmware_dir, exist_ok=True)
    firmware_out = os.path.join(firmware_dir, "firmware.bin")
    shutil.copy2(firmware_path, firmware_out)
    print(f"[OTA] Copied firmware.bin to {firmware_out}")
    with open(firmware_out, "rb") as f:
        sha256 = hashlib.sha256(f.read()).hexdigest()
    print("[OTA] SHA-256 for firmware.bin:", sha256)
    version = extract_version_from_main()
    print("[OTA] Version from main.cpp:", version)
    # Generate version.json (firmware_url can be derived on ESP32, so leave as empty or placeholder)
    version_json = {
        "version": version,
        "sha256": sha256
    }
    with open(os.path.join(firmware_dir, "version.json"), "w") as outf:
        json.dump(version_json, outf, indent=2)
        outf.write("\n")
    print("[OTA] version.json generated.")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)

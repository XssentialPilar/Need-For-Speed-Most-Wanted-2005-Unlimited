"""Stage the rendering component and the four authored shader profiles for PC 1.3."""

import argparse
import hashlib
from pathlib import Path
import shutil


SOURCE = "70171ad75d5ccdf6032bed161fbcdad6a0799b2f7c717af6d977e3571dd185bf"
PROFILES = (
    ("ModData/Files/FX/COMPILED", "U360", 18, "9ec8d53e97179f198fb9585da344d73ee7e840c16ba6765b855033f0a829e1a0"),
    ("AlternativeShaders/LowPerf", "U36L", 10, "e4ecf4f50a7430eada898d226a9c3fe5dcbd5153e0136e9ff5ee30ebb42b4a61"),
    ("AlternativeShaders/SharpTrees", "U36T", 1, "7aa8815e796bb0e1927234c65f1d31887eaf2a7796036b5b77d4c93b54601b2f"),
    ("AlternativeShaders/SoftShadows", "U36S", 7, "08ea66179b40c594315ff586a9edcaf93dc4af201c7ccb10cd3dbbdf62995acb"),
)


def stage(package, output):
    scripts = package / "ModData/Files/scripts"
    data = bytearray((scripts / "X360Stuff.asi").read_bytes())
    if hashlib.sha256(data).hexdigest() != SOURCE:
        raise ValueError("Unsupported Xbox 360 Stuff rendering component")
    # The wrapper initializes rendering explicitly. Keep the CRT and config path
    # setup, and skip the standalone mod's automatic second entry into GameMain.
    if data[0x2ad70:0x2ad73] != bytes.fromhex("51 8b c4"):
        raise ValueError("Unsupported module initializer")
    data[0x2ad70:0x2ad72] = bytes((0xeb, 0x26))
    for filename in (b"%s.o", b"hdr.fx.o"):
        original = b"fx\\compiled\\" + filename + b"\0"
        private = b"FX\\U360\\" + filename + b"\0"
        if data.count(original) != 1:
            raise ValueError("Unsupported shader file lookup")
        data = data.replace(original, private.ljust(len(original), b"\0"))
    output.mkdir(parents=True, exist_ok=True)
    (output / "World360.dll").write_bytes(data)
    settings = (scripts / "X360Stuff.ini").read_bytes()
    cap = b"CapFEFramerate = 1"
    if settings.count(cap) != 1:
        raise ValueError("Unsupported frontend frame limiter setting")
    (output / "World360.ini").write_bytes(settings.replace(cap, b"CapFEFramerate = 0"))
    shutil.copyfile(package / "LICENSE", output / "Xbox360Stuff-LICENSE.txt")
    baseline = {}
    for folder, name, count, expected in PROFILES:
        files = {f.name: f for f in sorted((package / folder).rglob("*.o"))}
        digest = hashlib.sha256()
        for filename, path in files.items():
            digest.update(filename.encode("ascii") + b"\0" + hashlib.sha256(path.read_bytes()).digest())
        if len(files) != count or digest.hexdigest() != expected:
            raise ValueError(f"Unsupported shader profile: {folder}")
        if not baseline:
            baseline = files
        if files.keys() - baseline.keys():
            raise ValueError(f"Unknown replacement in {folder}")
        target = output / "FX" / name
        target.mkdir(parents=True, exist_ok=True)
        for filename, path in (baseline | files).items():
            shutil.copyfile(path, target / filename)
    print("Console renderer: 18 base effects and 18 authored alternatives staged in four profiles")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    stage(args.package, args.output)

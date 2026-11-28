"""Compile the Xbox 360 visual settings for the verified PC 1.3 vault."""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


RETAIL = {
    "attributes.bin": "3d0362f2c052b2f93576852f504cb7806b14eefbf83b8531d45dc1d430ec6337",
    "fe_attrib.bin": "b4f78df4a83f3d36e961b43cfd635ed1750666a051260e27620cc2f254b9bcca",
    "gameplay.bin": "6eb317c578ab44176fa0bdd2d1b530bf4dfbae0f648980ee3af72212ae391314",
}
SCRIPTS = {
    "ModData/reset_tod_filter.nfsms": "40f6656b78fb5cd3ca75382880e69fe2982504582223669439b33a013d5e44d5",
    "Install-Part-1.nfsms": "af41032ee9d294b1e3eee58145f3bcc89a826c709a26140e5d62094080d37b2e",
}
EXPECTED_VAULT = "cf3088dfcffd78d888e49d14a4748925fc3da70c7de15d8e3d1403d834365d03"
EXPECTED_ALTERNATIVE_VAULT = "d509f6beb1b0edcde4478d8d417e772948e1017eef46ebe7df77bfef694dab05"
ALTERNATIVE_MATERIALS = {
    "lightmaterials.txt": "3830e4ac7d49d295c72d3dcc6d828c62e7cf51d447ad5e2ca3d48b7328169fbf",
    "lightmaterials.yml": "2daadf0732760daa6a674a89adcba502f33366dba9e01fa7ed0b278a4aa7c601",
    "lightshaders.txt": "ac6eb552ecc8dbb755e47da934c5fc3a58257442ab9e9d58d3fb0ffa5447447e",
    "lightshaders.yml": "c66168dd6550d37833f211d0e0e2bfd4201905d1795193d03f8b20505fbd26f9",
}

# Field keys as stored by the PC executable.
CURVES = {
    "BlackBloomCurve": "0xAAACCDF3",
    "ColourBloomCurve": "0x1D521C97",
    "DetailMapCurve": "0x51376819",
}
# Attribulator exports these existing retail records by key rather than label.
RETAIL_KEYS = {
    ("visualrgbtweaker", "default"): ("0xAF1837CA", "default"),
    ("timeofdaylighting", "no_vt_dusk_overcast"): ("timeofdaylighting", "0x919B6689"),
    ("timeofdaylighting", "no_vt_overcast"): ("timeofdaylighting", "0x94ABDFF0"),
    ("light_flares_cg", "brakelight_traffic_inner"): ("light_flares_cg", "0xD364E8AC"),
    ("light_flares_cg", "catseye_orange"): ("light_flares_cg", "0x96147A22"),
}
XBOX_EMITTER_COUNTS = {
    "fxtd_sk_asphalt": 5,
    "fxtd_sk_asphalt_no_leaves": 4,
    "fxtd_sl_asphalt": 5,
    "fxtd_dr_asphalt_leaves": 2,
}


def checked_bytes(path, expected):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f"Unexpected vault input or output: {path}")
    return data


def compile_commands(package):
    # The PC emittergroup layout already has capacity for six emitter references.
    commands = [f"resize_field emittergroup {name} Emitters {count}"
                for name, count in XBOX_EMITTER_COUNTS.items()]
    resolved_keys = 0
    for relative, checksum in SCRIPTS.items():
        for line in checked_bytes(package / relative, checksum).decode("utf-8-sig").splitlines():
            line = line.strip()
            parts = line.split()
            if not parts or parts[0] != "update_field":
                continue
            _, group, name, field, *_ = parts
            if (group, name) in RETAIL_KEYS:
                parts[1:3] = RETAIL_KEYS[group, name]
                line = " ".join(parts)
                resolved_keys += 1
            if group == "visuallook" and field in CURVES:
                line = line.replace(f" {field} ", f" {CURVES[field]} ", 1)
            commands.append(line)
    if len(commands) != 1075 or resolved_keys != 77:
        raise ValueError(f"Unexpected PC field inventory: {len(commands)} operations, {resolved_keys} keyed")
    return commands


def stage_vault(path, output, checksum):
    data = path.read_bytes()
    # Attribulator writes a build timestamp in the vault's provenance string.
    stamp = re.search(rb"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}", data[:256])
    if not stamp:
        raise ValueError("Attribulator's vault format differs")
    data = data[:stamp.start()] + b"2005-01-01T00:00:00" + data[stamp.end():]
    if hashlib.sha256(data).hexdigest() != checksum:
        raise ValueError("The compiled vault differs from the verified PC build")
    output.write_bytes(data)


def stage_alternative(package, command, main, workspace, output):
    authored = package / "AlternativeVLT"
    source = workspace / "alternative-yaml"
    packed = workspace / "alternative-packed"
    subprocess.run(command + [
        "unpack", "-i", str(main), "-o", str(source), "-p", "MOST_WANTED", "-f", "yml",
    ], check=True)
    info = source / "info.yml"
    schema = info.read_text(encoding="utf-8-sig")
    if not schema.startswith("Classes:\n") or "\nTypes:" not in schema:
        raise ValueError("Unrecognized PC vault schema")
    declarations = []
    for name in ("lightmaterials", "lightshaders"):
        declaration = name + ".txt"
        definition = checked_bytes(authored / declaration, ALTERNATIVE_MATERIALS[declaration])
        block = definition.decode("utf-8-sig").replace("\r\n", "\n").strip()
        if not block.startswith("- Name: " + name + "\n") or "- Name: " + name + "\n" in schema:
            raise ValueError(f"Unexpected class declaration: {name}")
        declarations.append(block)
        filename = name + ".yml"
        data = checked_bytes(authored / filename, ALTERNATIVE_MATERIALS[filename])
        (source / "main" / "attributes" / "db" / filename).write_bytes(data)
    info.write_text(schema.replace("\nTypes:", "\n" + "\n".join(declarations) + "\nTypes:", 1),
                    encoding="utf-8")
    subprocess.run(command + ["pack", "-i", str(source), "-o", str(packed), "-p", "MOST_WANTED"],
                   check=True)
    stage_vault(packed / "main" / "attributes.bin", output, EXPECTED_ALTERNATIVE_VAULT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("stock_global", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--attribulator", required=True, type=Path)
    parser.add_argument("--dotnet", type=Path)
    parser.add_argument("--alternative-output", type=Path)
    args = parser.parse_args()

    for name, checksum in RETAIL.items():
        checked_bytes(args.stock_global / name, checksum)
    commands = compile_commands(args.package)
    command = [str(args.attribulator.resolve())]
    if args.dotnet:
        command.insert(0, str(args.dotnet.resolve()))

    with tempfile.TemporaryDirectory(prefix="unlimited-console-vault-") as temp:
        workspace = Path(temp)
        script = workspace / "visuals.nfsms"
        script.write_text("\n".join(commands) + "\n", encoding="utf-8")
        subprocess.run(command + [
            "apply-script-bin", "-i", str(args.stock_global.resolve()), "-o", str(workspace),
            "-p", "MOST_WANTED", "-s", str(script),
        ], check=True)
        main_vault = workspace / "main"
        stage_vault(main_vault / "attributes.bin", args.output, EXPECTED_VAULT)
        if args.alternative_output:
            stage_alternative(args.package, command, main_vault, workspace, args.alternative_output)
    print(f"Console visual vault: {len(commands)} native PC operations")


if __name__ == "__main__":
    main()

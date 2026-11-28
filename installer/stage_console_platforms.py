"""Stage the five PC-format platform archives from Xbox 360 Stuff Pack 4.2."""

import argparse
import hashlib
from pathlib import Path
import struct
import zlib


PLATFORMS = {
    "CAR_LOT.BIN": 0x61F5E9A3,
    "CAREER_SAFEHOUSE.BIN": 0x892CF806,
    "CUSTOMIZATION_SHOP.BIN": 0x2C8E9C49,
    "CUSTOMIZATION_SHOP_BACKROOM.BIN": 0x21F963BC,
    "PlatformCrib.BIN": 0x388FFD6,
}
CHUNK = struct.Struct("<II")
PLATFORM_BLOCKS = {0x80134000, 0xB3300000, 0x0003BC00}
RIG_SIZE = 160
# Xbox 360 preset for five showroom rigs and the in-game car rig.
XBOX_RIGS = (0x1004B4C0, 0x1004B2E0, 0x1004B4C0, 0x1004B4C0, 0x1004B4C0, 0x1004B240)
PC_RIGS = (0x8F2B50, 0x8F2BF0, 0x8F2C90, 0x8F2D30, 0x8F2DD0, 0x8F2E70)


def validate_blocks(data, path):
    offset = 0
    blocks = set()
    while offset + CHUNK.size <= len(data):
        kind, count = CHUNK.unpack_from(data, offset)
        blocks.add(kind)
        offset += CHUNK.size + count
    if offset != len(data) or not PLATFORM_BLOCKS.issubset(blocks):
        raise ValueError(f"Invalid PC platform archive: {path}")


def read_pe_rigs(data, addresses):
    pe, = struct.unpack_from("<I", data, 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Expected a Win32 PE image")
    machine, section_count = struct.unpack_from("<HH", data, pe + 4)
    header_size, = struct.unpack_from("<H", data, pe + 20)
    optional = pe + 24
    magic, = struct.unpack_from("<H", data, optional)
    if machine != 0x14C or magic != 0x10B:
        raise ValueError("Expected a 32-bit PE image")
    image_base, = struct.unpack_from("<I", data, optional + 28)
    sections = [struct.unpack_from("<III", data, optional + header_size + 40 * i + 12)
                for i in range(section_count)]
    result = []
    for address in addresses:
        rva = address - image_base
        for base, length, start in sections:
            if base <= rva and rva + RIG_SIZE <= base + length:
                offset = start + rva - base
                result.append(data[offset:offset + RIG_SIZE])
                break
        else:
            raise ValueError(f"Platform light rig {address:#x} is outside the image")
    if any(len(rig) != RIG_SIZE for rig in result):
        raise ValueError("Truncated platform light rigs")
    return result


def compile_lighting(package, speed, output):
    source = (package / "ModData" / "Files" / "scripts" / "X360Stuff.asi").read_bytes()
    # The offsets are specific to this exact distribution of the asset data.
    if hashlib.sha256(source).hexdigest() != "70171ad75d5ccdf6032bed161fbcdad6a0799b2f7c717af6d977e3571dd185bf":
        raise ValueError("Unrecognized Xbox 360 Stuff Pack lighting layout")
    stock = read_pe_rigs(speed.read_bytes(), PC_RIGS)
    xbox = read_pe_rigs(source, XBOX_RIGS)
    output.write_bytes(b"ULP1" + b"".join(a + b for a, b in zip(stock, xbox)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--lighting", type=Path, metavar="SPEED_EXE")
    parser.add_argument("--lighting-output", type=Path)
    args = parser.parse_args()
    if bool(args.lighting) != bool(args.lighting_output):
        parser.error("--lighting and --lighting-output must be supplied together")
    source = args.package / "ModData" / "Files" / "FRONTEND" / "PLATFORMS"
    args.destination.mkdir(exist_ok=True, parents=True)
    for name, expected in PLATFORMS.items():
        path = source / name
        data = path.read_bytes()
        if zlib.crc32(data) != expected:
            raise ValueError(f"Unexpected Xbox 360 Stuff Pack 4.2 platform checksum: {path}")
        validate_blocks(data, path)
        (args.destination / name).write_bytes(data)
    print(f"Console platforms: {len(PLATFORMS)} verified PC-format archives")
    if args.lighting and args.lighting_output:
        compile_lighting(args.package, args.lighting, args.lighting_output)


if __name__ == "__main__":
    main()

"""Add the console shader inputs to two retail PC texture collections."""

import argparse
from pathlib import Path
import struct
import zlib

from compile_console_materials import unpack_jdlz
from compile_textures import chunks, game_hash, read_dds


def block(kind, payload):
    return struct.pack("<II", kind, len(payload)) + payload


def pad_block(position, alignment):
    if not position % alignment:
        return b""
    return block(0, bytes(-(position + 8) % alignment))


def shader_texture(path):
    asset = read_dds(path)
    name = path.stem
    normal = name in ("DAMAGE0_N", "TREAD_N", "TREADR_N", "UNIVERSAL_TIRE_N")
    opaque = normal or name.startswith("RAINSPLASH") or name in ("DOORLINE_N", "ONSCREENRAINDROPALPHA")
    header = bytearray(124)
    encoded = name.encode("ascii")
    if len(encoded) >= 24:
        raise ValueError(f"Texture name exceeds the native field: {name}")
    header[12:12 + len(encoded)] = encoded
    key = game_hash(name)
    struct.pack_into("<II", header, 36, key, 0x1da6c8a6 if normal else 0x001a93cf)
    pixels = path.read_bytes()[asset.offset:]  # Retail includes the stored tail below the declared mip limit.
    first_mip = (asset.width * asset.height * 4 if asset.format == 21 else
                 max(1, (asset.width+3)//4) * max(1, (asset.height+3)//4) *
                 (8 if asset.format == 0x31545844 else 16))
    struct.pack_into("<5IHH", header, 48, 0, 0xffffffff, len(pixels), 0, first_mip, asset.width, asset.height)
    header[72:75] = bytes((asset.width.bit_length()-1, asset.height.bit_length()-1,
                            {21: 32, 0x31545844: 34, 0x35545844: 38}[asset.format]))
    header[78] = asset.levels
    header[79] = 3 if name.startswith("SKY_NEXGEN_CLOUDS") else 0
    header[81] = 0 if opaque else 5
    if opaque or name == "2DNOISE":
        header[87] = 1
    else:
        header[85:87] = bytes((2, 2 if name == "SUNCOMBINED" else 1))
    header[89] = header[99] = header[101] = 1
    return key, (header, struct.pack("<8I", 0, 0, 1, 5, 6, asset.format, 0, 0), pixels)


def expand_group(source, filename, textures, *, move_sky=False):
    for kind, start, end in chunks(source, 0, len(source)):
        if kind != 0xb3300000:
            continue
        group = {k: (lo, hi) for k, lo, hi in chunks(source, start, end)}
        part = lambda tag: source[slice(*group[tag])]
        if part(0x33310001)[32:96].split(b"\0")[0].lower() == filename:
            break
    else:
        raise ValueError(f"Missing native texture collection {filename!r}")
    origin = start - 8
    if origin % 128 or end % 128:
        raise ValueError("Unaligned native texture collection")
    records = {}
    headers, formats, keys = part(0x33310004), part(0x33310005), part(0x33310002)
    pixel_base = (group[0x33320002][0]+127) & ~127
    pixels = bytearray(source[pixel_base:end])
    for index, (key, companion) in enumerate(struct.iter_unpack("<2I",keys)):
        header = bytearray(headers[index*124:(index+1)*124])
        if struct.unpack_from("<I",header,36)[0] != key or companion:
            raise ValueError("Unexpected native texture key")
        name = header[12:36].split(b"\0")[0]
        if move_sky and name.startswith(b"SKY_NEXGEN_"):
            name = b"_" + name
            key = game_hash(name.decode("ascii"))
            header[12:36] = name.ljust(24, b"\0")
            struct.pack_into("<I", header, 36, key)
        records[key] = header, formats[index*32:(index+1)*32]
    for path in sorted(textures):
        key, (header, fmt, data) = shader_texture(path)
        if key in records:
            raise ValueError(f"Shader texture collides with a stock texture: {path.name}")
        pixels.extend(bytes(-len(pixels) % 128))
        struct.pack_into("<I", header, 48, len(pixels))
        pixels.extend(data)
        records[key] = header, fmt
    ordered = sorted(records.items())
    info = block(0x33310001, part(0x33310001))
    info += block(0x33310002, b"".join(struct.pack("<II", key, 0) for key, _ in ordered))
    info += block(0x33310004, b"".join(value[0] for _, value in ordered))
    info += block(0x33310005, b"".join(value[1] for _, value in ordered))
    contents = pad_block(8,64) + block(0xb3310000,info)
    contents += pad_block(8 + len(contents),128)
    parts = block(0x33320001, part(0x33320001))
    parts += pad_block(8 + len(contents) + 8 + len(parts),128)
    payload = bytes(120) + pixels
    payload += bytes(-(8+len(payload)) % 128)
    packed = block(0xb3300000, contents + block(0xb3320000, parts + block(0x33320002,payload)))
    if len(packed) % 128:
        raise ValueError("Invalid native texture pack alignment")
    return source[:origin] + packed + source[end:]


def build(game, package, output):
    stocks = (("GlobalB.lzc", 0xd97f1754, "GLOB36B.LZC", b"global\\globaltexturespc.tpk", "GLOBALTEXTURESPC", 29),
              ("INGAMEA.BUN", 0x20eba990, "INGAM36A.BUN", b"global\\ingametexturespc.tpk", "INGAMETEXTURESPC", 1))
    for name, crc, target, group, folder, count in stocks:
        source = (game/"GLOBAL"/name).read_bytes()
        if zlib.crc32(source) != crc:
            raise ValueError(f"Unsupported PC source {name}")
        paths = list((package/"ModData/Textures"/folder).glob("*.dds"))
        if len(paths) != count:
            raise ValueError(f"Expected {count} authored textures in {folder}")
        raw = unpack_jdlz(source) if source[:4] == b"JDLZ" else source
        output_data = expand_group(raw, group, paths, move_sky=folder=="INGAMETEXTURESPC")
        list(chunks(output_data,0,len(output_data)))
        (output/target).write_bytes(output_data)
        print(f"{target}: {count} added, {len(output_data)} bytes, CRC32 {zlib.crc32(output_data):08X}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ("game","package","output"):
        parser.add_argument(argument,type=Path)
    args = parser.parse_args()
    build(args.game,args.package,args.output)

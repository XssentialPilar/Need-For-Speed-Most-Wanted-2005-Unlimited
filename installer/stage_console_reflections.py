"""Build the native PC streaming texture pack for the four showroom cubemaps."""

import argparse
from pathlib import Path
import struct
import zlib

from compile_console_materials import unpack_huff, unpack_jdlz
from compile_textures import chunks, game_hash, read_dds, read_pixels


ROW = struct.Struct("<6I")
GROUPS = ("CAREER", "CARLOT", "CSHOP", "QRACE")
FACES = ("XPOS", "XNEG", "YPOS", "YNEG", "ZPOS", "ZNEG")
STOCK_CRC = 0x15F2DE0F


def pack_jdlz(data):
    # MW interleaves token flags with a separate flag stream for match lengths.
    output = bytearray(16)
    recent = {}
    pos = tokens = matches = 0
    need_match_flag = True
    while pos < len(data):
        if tokens % 8 == 0:
            token_flag = len(output)
            output.append(0)
        if need_match_flag:
            match_flag = len(output)
            output.append(0)
            need_match_flag = False
        key = data[pos:pos + 3]
        previous = recent.get(key, -4096)
        recent[key] = pos
        distance = pos - previous
        if distance > 2064 or len(key) < 3:
            output.append(data[pos])
            pos += 1
        else:
            near = distance <= 16
            limit = min(len(data) - pos, 4098 if near else 34)
            length = 3
            while length < limit and data[previous + length] == data[pos + length]:
                length += 1
            output[token_flag] |= 1 << (tokens % 8)
            if near:
                output[match_flag] |= 1 << (matches % 8)
                output.extend((((length - 3) >> 8 << 4) | (distance - 1), (length - 3) & 255))
            else:
                output.extend((((distance - 17) >> 8 << 5) | (length - 3), (distance - 17) & 255))
            pos += length
            matches += 1
            need_match_flag = matches % 8 == 0
        tokens += 1
    # Retail refills either flag register after consuming its eighth bit,
    # before checking the compressed byte count for end of stream.
    if tokens % 8 == 0:
        output.append(0)
    if need_match_flag:
        output.append(0)
    struct.pack_into("<4sIII", output, 0, b"JDLZ", 0x1002, len(data), len(output))
    return output


def build(stock_path, folder, destination):
    stock = stock_path.read_bytes()
    if zlib.crc32(stock) != STOCK_CRC:
        raise ValueError(f"Unsupported PC DYNTEX archive: {stock_path}")
    sections = {kind: (start, end) for kind, start, end in chunks(stock, 0, len(stock))}
    first, last = sections[0x33310003]
    rows = [(entry, *ROW.unpack_from(stock, entry)) for entry in range(first, last, ROW.size)]
    face_names = [f"{group}_{face}" for group in GROUPS for face in FACES]
    names = {game_hash(name): name for name in face_names}
    out = bytearray(stock[:min(row[2] for row in rows)])
    for entry, key, offset, packed_size, expanded_size, _, _ in sorted(rows, key=lambda row: row[2]):
        block = stock[offset:offset + packed_size]
        if key in names:
            name = names.pop(key)
            face = read_dds(folder / (name + ".dds"))
            raw = unpack_huff(block) if block.startswith(b"HUFF") else unpack_jdlz(block)
            header = raw[-156:]
            w, h = struct.unpack_from("<HH", header, 0x44)
            fmt, = struct.unpack_from("<I", header, 124 + 20)
            if (len(raw) != expanded_size or len(raw) != face.size + len(header) or
                    header[12:36].rstrip(b"\0") != name.encode("ascii") or
                    (w, h, header[0x4e], fmt) != (face.width, face.height, face.levels, face.format)):
                raise ValueError(f"{name}: incompatible DDS and native PC texture")
            expected = read_pixels(face) + header
            block = pack_jdlz(expected)
            if unpack_jdlz(block, verify_refills=True) != expected:
                raise ValueError(f"{name}: packed texture failed round-trip verification")
        out.extend(bytes(-len(out) % 64))
        struct.pack_into("<II", out, entry + 4, len(out), len(block))
        out.extend(block)
    if names:
        raise ValueError(f"Missing native cubemap slots: {sorted(names.values())}")
    out.extend(bytes(-len(out) % 128))
    payload_end = len(out)
    for kind in (0xb3300000, 0xb3320000, 0x33320002):
        start = sections[kind][0]
        struct.pack_into("<I", out, start - 4, payload_end - start)
    out.extend(stock[sections[0xb3300000][1]:])
    list(chunks(out, 0, len(out)))
    destination.write_bytes(out)
    print(f"Console showroom reflections: 24 faces, {len(rows) - 24} stock entries, "
          f"{len(out)} bytes, CRC32 {zlib.crc32(out):08X}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock", type=Path)
    parser.add_argument("dds_folder", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.stock, args.dds_folder, args.output)

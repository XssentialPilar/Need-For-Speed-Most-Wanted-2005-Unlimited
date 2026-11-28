"""Compile the named Xbox light-material exports for the PC 1.3 loader."""

import argparse
import math
from pathlib import Path
import struct

from compile_textures import chunks, game_hash


MATERIAL = struct.Struct("<30f")
MATERIAL_CHUNK = 0x00135200


def unpack_jdlz(data, *, verify_refills=False):
    if data[:8] != b"JDLZ\x02\x10\0\0":
        raise ValueError("Invalid JDLZ signature")
    length, packed_length = struct.unpack_from("<II", data, 8)
    if not 16 <= packed_length <= len(data) or not 0 < length <= 32 * 1024 * 1024:
        raise ValueError("Invalid JDLZ block size")
    data = memoryview(data)[:packed_length]
    output = bytearray()
    position = 16
    token_flags = match_flags = 1
    while len(output) < length:
        if token_flags == 1:
            token_flags = data[position] | 256
            position += 1
        if match_flags == 1:
            match_flags = data[position] | 256
            position += 1
        if token_flags & 1:
            first, second = data[position:position + 2]
            position += 2
            if match_flags & 1:
                distance = (first & 15) + 1
                count = (second | ((first & 240) << 4)) + 3
            else:
                distance = (second | ((first & 224) << 3)) + 17
                count = (first & 31) + 3
            if distance > len(output) or count > length - len(output):
                raise ValueError("Invalid JDLZ back-reference")
            for _ in range(count):
                output.append(output[-distance])
            match_flags >>= 1
        else:
            output.append(data[position])
            position += 1
        token_flags >>= 1
    if verify_refills:
        # The retail reader refills both flag bytes before testing for end of stream.
        end = position + int(token_flags == 1) + int(match_flags == 1)
        if end != packed_length:
            raise ValueError("Invalid retail JDLZ flag termination")
    return bytes(output)


def stock_materials(path):
    data = path.read_bytes()
    if data.startswith(b"JDLZ"):
        data = unpack_jdlz(data)
    records = {}
    for tag, first, last in chunks(data, 0, len(data)):
        if tag != MATERIAL_CHUNK or last - first != 0xa8:
            continue
        key, version = struct.unpack_from("<II", data, first + 12)
        name = data[first + 20:first + 48].split(b"\0", 1)[0]
        if version == 3 and key == game_hash(name.decode("ascii")):
            records[key] = data[first + 48:last]
    return records


def unpack_huff(data):
    # The material exports and showroom texture blocks use EA's plain canonical code stream.
    if data[:8] != b"HUFF\x01\x10\0\0":
        raise ValueError("Invalid HUFF signature")
    size, compressed = struct.unpack_from("<II", data, 8)
    if compressed != len(data) - 16 or not 0 < size <= 32 * 1024 * 1024:
        raise ValueError("Invalid HUFF block size")
    position = 16
    buffer = available_bits = 0

    def read(bits):
        nonlocal position, buffer, available_bits
        while available_bits < bits:
            if position == len(data):
                raise ValueError("Truncated HUFF record")
            buffer = buffer << 8 | data[position]
            position += 1
            available_bits += 8
        available_bits -= bits
        value = buffer >> available_bits
        buffer &= (1 << available_bits) - 1
        return value

    def number():
        if read(1):
            return read(2)
        zeroes = 1
        while not read(1):
            zeroes += 1
        bits = zeroes + 2
        return read(bits) + (1 << bits) - 4

    if read(16) != 0x30fb or read(24) != size:
        raise ValueError("Unsupported HUFF material stream")
    escape = read(8)
    codes = []
    first = 0
    for width in range(1, 25):
        count = number()
        if count > 256 - len(codes) or first + count > 1 << width:
            raise ValueError("Invalid HUFF code table")
        codes.extend((width, first + j) for j in range(count))
        if first + count == 1 << width:
            break
        first = (first + count) * 2
    symbols = {}
    used = set()
    previous = 255
    for code in codes:
        available = [i & 255 for i in range(previous + 1, previous + 257) if i & 255 not in used]
        distance = number()
        if distance >= len(available):
            raise ValueError("Invalid HUFF symbol distance")
        previous = available[distance]
        used.add(previous)
        symbols[code] = previous

    output = bytearray()
    while len(output) < size:
        code = 0
        for width in range(1, 25):
            code = code * 2 + read(1)
            if (width, code) in symbols:
                symbol = symbols[width, code]
                break
        else:
            raise ValueError("Unknown HUFF code")
        if symbol != escape:
            output.append(symbol)
        else:
            repeat = number()
            if repeat:
                if repeat > size - len(output):
                    raise ValueError("Invalid HUFF repeat")
                output.extend(bytes([output[-1] if output else 0]) * repeat)
            elif read(1) == 0:
                output.append(read(8))
            else:
                raise ValueError("Premature HUFF end marker")
    return bytes(output)


def export_material(path):
    data = path.read_bytes()
    if len(data) < 36 or data[:4] != b"gMpB":
        raise ValueError(f"Invalid Binarius export: {path.name}")
    size, version, kind, compressed = struct.unpack_from("<IIII", data, 4)
    if size != len(data) - 8 or compressed != len(data) - 20 or version != 2 or kind != 0x5f9bcca1:
        raise ValueError(f"Unsupported Binarius material: {path.name}")
    data = unpack_huff(data[20:]) if data[20:24] == b"HUFF" else unpack_jdlz(data[20:])
    name, terminator, values = data.partition(b"\0")
    if name != path.stem.encode("ascii") or not terminator or len(values) != MATERIAL.size:
        raise ValueError(f"Invalid name or layout: {path.name}")
    if not all(math.isfinite(value) for value in MATERIAL.unpack(values)):
        raise ValueError(f"Invalid material values: {path.name}")
    return game_hash(path.stem), values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exports", type=Path)
    parser.add_argument("stock", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    stock = stock_materials(args.stock)
    records = {}
    for path in sorted(args.exports.glob("*.bin")):
        key, values = export_material(path)
        if key not in stock or key in records:
            raise ValueError(f"Missing or duplicate PC material: {path.stem}")
        records[key] = stock[key] + values
    if not records or len(records) > 128:
        raise ValueError("Unexpected number of console materials")
    with args.output.open("wb") as output:
        output.write(struct.pack("<4sI", b"ULM1", len(records)))
        for key, data in sorted(records.items()):
            output.write(struct.pack("<I", key))
            output.write(data)
    print(f"Console materials: {len(records)} matching PC material slots")


if __name__ == "__main__":
    main()

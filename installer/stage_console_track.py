"""Build the optional PC track variant from the package's verified UPS delta."""

import argparse
import mmap
from pathlib import Path
import shutil
import struct
import zlib


def read_number(patch, position):
    value = 0
    weight = 1
    for _ in range(5):
        byte = patch[position]
        position += 1
        value += (byte & 127) * weight
        if byte & 128:
            return value, position
        weight *= 128
        value += weight
    raise ValueError("Track delta offset exceeds the PC stream size")


def stage(source, output, patch):
    if len(patch) < 18 or patch[:4] != b"UPS1":
        raise ValueError("Invalid UPS track delta")
    original, expected, checksum = struct.unpack_from("<III", patch, len(patch) - 12)
    if zlib.crc32(patch[:-4]) != checksum:
        raise ValueError("Track delta checksum differs")
    body = patch[:-12]
    before, position = read_number(body, 4)
    after, position = read_number(body, position)
    if before != after or source.stat().st_size != before:
        raise ValueError("Track delta requires the unmodified PC stream layout")

    temporary = output.with_suffix(".building")
    try:
        shutil.copyfile(source, temporary)
        with temporary.open("r+b") as file, mmap.mmap(file.fileno(), 0) as image:
            if zlib.crc32(image) != original:
                raise ValueError("Track delta does not match the stock PC stream")
            offset = 0
            while position < len(body):
                distance, position = read_number(body, position)
                offset += distance
                end = body.index(0, position)
                length = end - position
                if not length or offset + length > after:
                    raise ValueError("Track delta range exceeds the stream")
                image[offset:offset + length] = bytes(a ^ b for a, b in
                                                      zip(image[offset:offset + length], body[position:end]))
                offset += length + 1
                position = end + 1
            if zlib.crc32(image) != expected:
                raise ValueError("Track output checksum differs")
            image.flush()
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Console track: {after:,} bytes, verified CRC {expected:08x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock", type=Path)
    parser.add_argument("patch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    stage(args.stock, args.output, args.patch.read_bytes())


if __name__ == "__main__":
    main()

"""Validate and stage the PC-compatible audio from Complex Installation."""

import argparse
import hashlib
from pathlib import Path
import shutil
import struct


MUSIC_FORMAT = (136148, 653261696)


def check_music(console, stock):
    retail = (stock / "MW_Music.mpf").read_bytes()
    index = (console / "MW_Music.mpf").read_bytes()
    stream_path = console / "MW_Music.mus"
    index_size, stream_size = MUSIC_FORMAT
    if (len(retail) != index_size or len(index) != index_size or
            not index.startswith(b"xDFP") or stream_path.stat().st_size != stream_size):
        raise ValueError("Unexpected Pathfinder music bank")
    begin, end = struct.unpack_from("<II", index, 0x34)
    if not 0x3c <= begin < end == len(index) or (end - begin) % 8 or index[:begin] != retail[:begin]:
        raise ValueError("The console music bank has a different Pathfinder event table")
    with stream_path.open("rb") as stream:
        if stream.read(4) != bytes.fromhex("facea58c"):
            raise ValueError("Invalid Pathfinder stream header")
        for position in range(begin, end, 8):
            sector, key = struct.unpack_from("<II", index, position)
            offset = sector * 128
            if offset + 8 > stream_size or key != struct.unpack_from("<I", retail, position + 4)[0]:
                raise ValueError(f"Invalid Pathfinder sector or sound ID at {position:#x}")
            stream.seek(offset)
            signature, size = struct.unpack("<4sI", stream.read(8))
            if signature != b"SCHl" or not 8 <= size <= stream_size - offset:
                raise ValueError(f"Invalid EA audio stream at {offset:#x}")
    print(f"Console music: {(end - begin) // 8} validated stream offsets and unchanged Pathfinder event IDs")


def check_gin(path):
    data = path.read_bytes()
    if len(data) < 32 or data[:4] != b"Gnsu":
        raise ValueError(f"Invalid EA GIN header in {path}")
    pitch, frequency, samples, rate = struct.unpack_from("<4I", data, 0x10)
    # Two RPM lookup tables followed by EA XAS frames of 32 mono samples.
    payload = 32 + 4 * (pitch + frequency + 2) + 19 * ((samples + 31) // 32)
    if not samples or not 8000 <= rate <= 48000 or payload != len(data):
        raise ValueError(f"Invalid EA GIN stream in {path}")
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("stock", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    source = args.source / "ModData" / "Files" / "SOUND"
    stock = args.stock / "SOUND"
    check_music(source / "PFDATA", stock / "PFDATA")

    original = {path.name.casefold(): path for path in (stock / "ENGINE").glob("*.gin")}
    records = []
    for path in sorted((source / "ENGINE").glob("*.gin")):
        before = original.get(path.name.casefold())
        if not before:
            raise ValueError(f"No matching PC sound slot for {path.name}")
        stock_hash = hashlib.sha256(before.read_bytes()).hexdigest()
        new_hash = check_gin(path)
        if new_hash == stock_hash:
            continue
        records.append(f"SOUND\\ENGINE\\{before.name}\t{stock_hash}\t{new_hash}\n")
        shutil.copyfile(path, args.destination / (new_hash + ".xgin"))
    if not records:
        raise ValueError("The package does not contain any engine sound replacements")

    for ext in ("mpf", "mus"):
        shutil.copyfile(source / "PFDATA" / f"MW_Music.{ext}", args.destination / f"X3_Music.{ext}")
    (args.destination / "console-engine-checksums.tsv").write_text("".join(records), encoding="ascii")
    print(f"Console engines: {len(records)} validated PC sound slots")


if __name__ == "__main__":
    main()

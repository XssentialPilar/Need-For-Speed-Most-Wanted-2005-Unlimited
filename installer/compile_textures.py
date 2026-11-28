"""Compile the supplied remaster into a seekable resource for the game's D3D9 loader."""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
from tempfile import NamedTemporaryFile, TemporaryDirectory

HEADER = struct.Struct("<4sIII")
RECORD = struct.Struct("<IIIIHHB3x")
FOURCC = {int.from_bytes(code, "little"): stride for code, stride in
          ((b"DXT1", 8), (b"DXT3", 16), (b"DXT5", 16))}
UNCOMPRESSED = {21, 22}  # A8R8G8B8, X8R8G8B8
MAPPINGS = ("Xbox 360 Look", "Xbox 360 Look Bonus")
MAX_EDGE = 8192


@dataclass(frozen=True)
class Image:
    source: Path
    offset: int
    size: int
    format: int
    width: int
    height: int
    levels: int


def game_hash(name):
    if re.fullmatch(r"0[xX][\da-fA-F]{1,8}", name):
        return int(name, 16)
    value = 0xffffffff
    for byte in name.upper().encode("ascii"):
        value = (value * 33 + byte) & 0xffffffff
    return value


def mip_size(width, height, fmt, levels):
    total = 0
    for _ in range(levels):
        if fmt in FOURCC:
            total += ((width + 3) // 4) * ((height + 3) // 4) * FOURCC[fmt]
        elif fmt in UNCOMPRESSED:
            total += width * height * 4
        else:
            raise ValueError(f"Unsupported format {fmt:#x}")
        width, height = max(1, width // 2), max(1, height // 2)
    return total


def image(path, offset, fmt, width, height, levels):
    if not (0 < width <= MAX_EDGE and 0 < height <= MAX_EDGE
            and 0 < levels <= max(width, height).bit_length()):
        raise ValueError(f"{path}: invalid dimensions or mip count")
    size = mip_size(width, height, fmt, levels)
    if offset + size > path.stat().st_size:
        raise ValueError(f"{path}: truncated pixel data at {offset}")
    return Image(path, offset, size, fmt, width, height, levels)


def read_dds(path):
    with path.open("rb") as stream:
        header = stream.read(128)
    if len(header) != 128 or header[:8] != b"DDS \x7c\x00\x00\x00":
        raise ValueError(f"{path}: invalid DDS header")
    height, width = struct.unpack_from("<2I", header, 12)
    levels = struct.unpack_from("<I", header, 28)[0] or 1
    _, flags, fmt, bits, red, green, blue, alpha = struct.unpack_from("<8I", header, 76)
    caps2 = struct.unpack_from("<I", header, 112)[0]
    if caps2 or struct.unpack_from("<I", header, 24)[0] > 1:
        raise ValueError(f"{path}: only 2D textures are supported")
    if not flags & 4:
        if (bits, red, green, blue) != (32, 0xff0000, 0xff00, 0xff) or alpha not in (0, 0xff000000):
            raise ValueError(f"{path}: unsupported RGB masks")
        fmt = 21 if alpha else 22
    return image(path, 128, fmt, width, height, levels)


def d3d_compatible_dds(asset, work):
    # D3D9 rejects compressed top-level dimensions outside block boundaries.
    # Decode these authored mip levels losslessly instead of padding UVs or
    # silently falling back to the retail world texture.
    if asset.format not in FOURCC or not (asset.width % 4 or asset.height % 4):
        return asset
    from io import BytesIO
    from PIL import Image as PillowImage
    with asset.source.open("rb") as source, NamedTemporaryFile(dir=work, suffix=".pixels", delete=False) as output:
        target = Path(output.name)
        header = bytearray(source.read(128))
        width, height = asset.width, asset.height
        for _ in range(asset.levels):
            size = mip_size(width, height, asset.format, 1)
            blocks = source.read(size)
            if len(blocks) != size:
                raise ValueError(f"{asset.source}: truncated compressed mip")
            struct.pack_into("<5I", header, 12, height, width, size, 0, 1)
            with PillowImage.open(BytesIO(header + blocks)) as mip:
                decoded = mip.convert("RGBA")
            output.write(decoded.tobytes("raw", "BGRA"))
            width, height = max(1, width // 2), max(1, height // 2)
        levels = max(asset.width, asset.height).bit_length()
        for _ in range(asset.levels, levels):
            decoded = decoded.resize((width, height), PillowImage.Resampling.BOX)
            output.write(decoded.tobytes("raw", "BGRA"))
            width, height = max(1, width // 2), max(1, height // 2)
    return image(target, 0, 21, asset.width, asset.height, levels)


def complete_dds_mips(asset, work):
    if max(asset.width, asset.height) <= 256 or asset.levels > 1:
        return asset
    # Keep the authored top level and append mips for smaller texture profiles.
    from io import BytesIO
    from PIL import Image as PillowImage
    with asset.source.open("rb") as source, NamedTemporaryFile(dir=work, suffix=".pixels", delete=False) as output:
        target = Path(output.name)
        source.seek(asset.offset)
        output.write(source.read(asset.size))
        with PillowImage.open(asset.source) as original:
            mip = original.convert("RGBA")
        levels = max(mip.size).bit_length()
        for _ in range(1, levels):
            mip = mip.resize((max(1, mip.width // 2), max(1, mip.height // 2)),
                             PillowImage.Resampling.BOX)
            if asset.format in UNCOMPRESSED:
                output.write(mip.tobytes("raw", "BGRA"))
            else:
                encoded = BytesIO()
                fourcc = asset.format.to_bytes(4, "little").decode("ascii")
                mip.save(encoded, format="DDS", pixel_format=fourcc)
                data = encoded.getbuffer()[128:]
                if len(data) != mip_size(mip.width, mip.height, asset.format, 1):
                    raise ValueError(f"{asset.source}: invalid encoded mip")
                output.write(data)
    return image(target, 0, asset.format, asset.width, asset.height, levels)


def read_png(path, work):
    from PIL import Image as PillowImage
    pixels = work / (path.stem + ".pixels")
    with PillowImage.open(path) as source, pixels.open("wb") as output:
        width, height = source.size
        mip = source.convert("RGBA")
        levels = max(width, height).bit_length()
        for _ in range(levels):
            output.write(mip.tobytes("raw", "BGRA"))
            if mip.size != (1, 1):
                mip = mip.resize((max(1, mip.width // 2), max(1, mip.height // 2)),
                                 PillowImage.Resampling.BOX)
    return image(pixels, 0, 21, width, height, levels)


def chunks(data, start, end):
    while start + 8 <= end:
        kind, length = struct.unpack_from("<II", data, start)
        start += 8
        stop = start + length
        if stop > end:
            raise ValueError(f"Invalid BIN chunk length at {start - 8:#x}")
        yield kind, start, stop
        if kind & 0x80000000:
            yield from chunks(data, start, stop)
        start = stop


def native_images(path, selected=None, *, supported_only=False):
    data = path.read_bytes()
    want = {0x33310004, 0x33310005, 0x33320002}
    for kind, start, end in chunks(data, 0, len(data)):
        if kind != 0xb3300000:
            continue
        sections = {tag: (lo, hi) for tag, lo, hi in chunks(data, start, end) if tag in want}
        if sections.keys() >= want:
            yield from native_table(path, data, sections, selected, supported_only)


def native_table(path, data, sections, selected, supported_only):
    info, formats, pixels = (sections[tag] for tag in (0x33310004, 0x33310005, 0x33320002))
    count, remainder = divmod(info[1] - info[0], 124)
    if remainder or formats[1] - formats[0] != count * 32:
        raise ValueError(f"{path}: inconsistent texture and format tables")
    # PC TPK pixel data begins at the next 128-byte boundary.
    base = (pixels[0] + 127) & ~127
    for index in range(count):
        record = info[0] + index * 124
        key, = struct.unpack_from("<I", data, record + 0x24)
        if selected is not None and key not in selected:
            continue
        offset, _, size = struct.unpack_from("<3I", data, record + 0x30)
        width, height = struct.unpack_from("<2H", data, record + 0x44)
        levels = data[record + 0x4e]
        fmt, = struct.unpack_from("<I", data, formats[0] + index * 32 + 20)
        if supported_only and fmt not in FOURCC and fmt not in UNCOMPRESSED:
            continue
        asset = image(path, base + offset, fmt, width, height, levels)
        if asset.size > size or asset.offset + size > pixels[1]:
            raise ValueError(f"{path}: invalid platform texture {key:08x}")
        yield key, asset


def map_images(root, work):
    packs = root / "NextGenGraphics" / "TexturePacks"
    named = {}
    for pack in MAPPINGS:
        folder = packs / pack
        info = json.loads((folder / "TexturePackInfo.json").read_text(encoding="utf-8-sig"))
        textures = folder / info["rootDirectory"]
        files = {p.relative_to(textures).as_posix().lower(): p for p in textures.rglob("*.dds")}
        for path in sorted(files.values()):
            if path.stem.startswith(".") or path.stem.endswith(" - Copy") or "Alternate Trees" in path.parts:
                continue
            key = game_hash(path.stem.strip())
            named.setdefault(key, path)
        for mapping in info["textureMappings"]:
            name = PurePosixPath(mapping["texturePath"].replace("\\", "/")).as_posix().lower()
            path = files.get(name)
            if not path and name == "sgn_lakesidelodge_ea's_rj.dds":
                path = files.get("sgn_lakesidelodge_ea.dds")
            if not path:
                print(f"Source mapping has no image: {mapping['gameId']}")
                continue
            named[game_hash(mapping["gameId"])] = path

    assets = {}
    dds = {}
    for key, path in named.items():
        if path not in dds:
            source = read_dds(path)
            dds[path] = (d3d_compatible_dds(source, work) if source.format in FOURCC and
                         (source.width % 4 or source.height % 4) else complete_dds_mips(source, work))
        assets[key] = dds[path]
    for name in ("CAREER_SAFEHOUSE.BIN", "PlatformCrib.BIN"):
        garage = dict(native_images(root / "FRONTEND" / "PLATFORMS" / name))
        if not garage:
            raise ValueError(f"{name}: no texture records found")
        assets.update(garage)
    for path in sorted((root / "NextGenGraphics" / "GenericTextures").iterdir()):
        if path.suffix.lower() == ".dds":
            assets[game_hash(path.stem)] = read_dds(path)
        elif path.suffix.lower() == ".png":
            assets[game_hash(path.stem)] = read_png(path, work)
    return assets


def deduplicate_images(assets):
    canonical = {}
    replacements = {}
    for asset in dict.fromkeys(assets.values()):
        content = hashlib.sha256(read_pixels(asset)).digest()
        key = (asset.size, asset.format, asset.width, asset.height, asset.levels, content)
        replacements[asset] = canonical.setdefault(key, asset)
    return {texture_id: replacements[asset] for texture_id, asset in assets.items()}


def write_pack(assets, output):
    table_end = HEADER.size + len(assets) * RECORD.size
    offsets = {}
    with output.open("w+b") as stream:
        stream.seek(table_end)
        for asset in dict.fromkeys(assets.values()):
            offsets[asset] = stream.tell()
            with asset.source.open("rb") as source:
                source.seek(asset.offset)
                pending = asset.size
                while pending:
                    data = source.read(min(4 * 1024 * 1024, pending))
                    if not data:
                        raise ValueError(f"{asset.source}: unexpected end of image")
                    stream.write(data)
                    pending -= len(data)
        length = stream.tell()
        stream.seek(0)
        stream.write(HEADER.pack(b"ULTX", 1, len(assets), length))
        for key, asset in sorted(assets.items()):
            stream.write(RECORD.pack(key, offsets[asset], asset.size, asset.format,
                                     asset.width, asset.height, asset.levels))
    full = sum(max(asset.width, asset.height) > 4096 for asset in assets.values())
    print(f"{len(assets)} texture IDs, {len(offsets)} unique images, {full} IDs above 4K, {length:,} bytes")


def read_pixels(asset):
    with asset.source.open("rb") as stream:
        stream.seek(asset.offset)
        return stream.read(asset.size)


def map_console_platforms(package, stock):
    candidates = {}
    conflicts = set()
    for filename in ("CAR_LOT.BIN", "CAREER_SAFEHOUSE.BIN", "CUSTOMIZATION_SHOP.BIN",
                     "CUSTOMIZATION_SHOP_BACKROOM.BIN", "PlatformCrib.BIN"):
        relative = Path("FRONTEND") / "PLATFORMS" / filename
        replacement = dict(native_images(package / relative, supported_only=True))
        original = dict(native_images(stock / relative, set(replacement), supported_only=True))
        for key, new in replacement.items():
            old = original.get(key)
            if not old or new.width * old.height != new.height * old.width or new.width < old.width:
                continue
            if new.width == old.width and new.format == old.format and read_pixels(new) == read_pixels(old):
                continue
            other = candidates.setdefault(key, new)
            if other != new and (other.width, other.height, other.format, other.levels, read_pixels(other)) != (
                    new.width, new.height, new.format, new.levels, read_pixels(new)):
                conflicts.add(key)
    return {key: asset for key, asset in candidates.items() if key not in conflicts}


def white_window_texture(path, work):
    with path.open("rb") as stream:
        signature = stream.read(4)
    if signature == b"JDLZ":
        from compile_console_materials import unpack_jdlz
        expanded = work / "global-textures.bin"
        expanded.write_bytes(unpack_jdlz(path.read_bytes()))
        path = expanded
    white = game_hash("WHITE32X32")
    return dict(native_images(path, {white}))[white]


def map_console_images(root, stock, work):
    mod = root / "ModData"
    files = mod / "Files"
    wizard = files / "TRACKS" / "TexWizardX360"
    # The four barrier variants are embedded with the console effects.
    metadata = (wizard / "meta.json").read_text(encoding="utf-8-sig")
    mappings = json.loads(re.sub(r"(?m)^\s*//[^\r\n]*", "", metadata))["textures"]
    native = dict(native_images(wizard / "textures.bin"))
    assets = {}
    missing = set()
    for original, replacement in mappings:
        key = game_hash(original)
        image = native.get(game_hash(replacement))
        if replacement == "WHITE32X32":
            image = white_window_texture(stock / "GLOBAL/GlobalB.lzc", work)
        elif not image and original == "TRNS_GCGRASS_LD_ROUGH_SANDA_01_OPACITY":
            # This source block keeps the destination name, without the leading underscore.
            image = native.get(key)
        if image:
            assets[key] = image
            missing.discard(original)
        elif key not in assets:
            missing.add(original)
    print(f"Console world: {len(assets)} native mappings")
    if missing:
        print(f"Unresolved world textures: {', '.join(sorted(missing))}")

    garage = map_console_platforms(files, stock)
    for key, asset in garage.items():
        assets.setdefault(key, asset)
    print(f"Console garages: {len(garage)} compatible texture slots")

    textures = mod / "Textures"
    # The listed groups have existing PC texture slots and retain native material
    # bindings. The other PC resources require new shader inputs or HUD artwork.
    paths = list((textures / "PARTICLETEXTURES").glob("*.dds"))
    paths.extend((textures / "GLOBALTEXTURES").glob("*.dds"))
    paths.extend((textures / "GLOBALTEXTURES" / "Flares").glob("*.dds"))
    paths.extend((textures / "INGAMETEXTURES").glob("SUN*.dds"))
    paths.extend((textures / "GLOBALTEXTURESPC").glob("SKY_NEXGEN_*.dds"))
    for path in sorted(paths):
        asset = read_dds(path)
        assets[game_hash(path.stem)] = d3d_compatible_dds(asset, work)
    print(f"Console effects: {len(paths)} sky, glare and particle textures")
    return assets


def map_console_embedded(package, global_file, work):
    wizard = package / "ModData" / "Files" / "TRACKS" / "TexWizardX360"
    names = ("SFX_TRACKBARRIERNEONARROW", "SFX_TRACKBARRIERNEONARROW_SOLID",
             "SFX_TRACKBARRIERLOCK", "SFX_TRACKBARRIERNEONLOCKED")
    text = (wizard / "meta+barriers.json").read_text(encoding="utf-8-sig")
    mappings = dict(json.loads(re.sub(r"(?m)^\s*//[^\r\n]*", "", text))["textures"])
    selected = {name: game_hash(mappings[name]) for name in names}
    native = dict(native_images(wizard / "textures.bin", set(selected.values())))
    result = {game_hash(name): native[key] for name, key in selected.items()}
    # Include both corrected world bindings in updates to existing console packs.
    sand = game_hash("TRNS_GCGRASS_LD_ROUGH_SANDA_01_OPACITY")
    result[sand] = dict(native_images(wizard / "textures.bin", {sand}))[sand]
    if global_file:
        result[game_hash("GLOSSYWINDOW")] = white_window_texture(global_file, work)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="8K remaster .7z or extracted directory")
    parser.add_argument("output", type=Path, help="Destination UnlimitedTextures.bin")
    parser.add_argument("--xbox-effects", action="store_true", help="Read selected effects from the v4.2 GlobalB.lzc")
    parser.add_argument("--xbox-barriers", type=Path, metavar="PACKAGE",
                        help="Embed barrier, glossy window and grass mask textures from a Complex Installation folder")
    parser.add_argument("--world-global", type=Path, help="PC or console GlobalB archive for the window texture")
    parser.add_argument("--xbox-complex", type=Path, metavar="STOCK_GAME",
                        help="Compile a Complex Installation folder against the installed PC resource names")
    parser.add_argument("--deduplicate-content", action="store_true", help="Store identical mip chains once")
    args = parser.parse_args()
    with TemporaryDirectory(prefix="unlimited-textures-") as tmp:
        work = Path(tmp)
        source = args.source.resolve(strict=True)
        if args.xbox_complex:
            assets = map_console_images(source, args.xbox_complex.resolve(strict=True), work)
        elif args.xbox_effects:
            selected = {game_hash(name) for name in ("HEADLIGHTFLAREINNER", "HEADLIGHTFLAREOUTER", "RAINDROPPC")}
            assets = dict(native_images(source, selected))
            if assets.keys() != selected:
                raise ValueError("Could not find the three compatible console effect textures")
        elif args.xbox_barriers:
            assets = {}
        else:
            if source.is_file():
                subprocess.run(["tar.exe", "-xf", str(source), "-C", str(work)], check=True)
                source = work
            if not (source / "NextGenGraphics").is_dir():
                candidates = sorted(source.glob("*/NextGenGraphics"))
                if len(candidates) != 1:
                    raise ValueError("Could not locate the remaster's NextGenGraphics folder")
                source = candidates[0].parent
            assets = map_images(source, work)
        if args.xbox_barriers:
            world_global = args.world_global or (source if args.xbox_effects else None)
            assets.update(map_console_embedded(args.xbox_barriers, world_global, work))
        if args.deduplicate_content:
            assets = deduplicate_images(assets)
        destination = args.output.resolve()
        temporary = destination.with_suffix(".bin.building")
        try:
            write_pack(assets, temporary)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()

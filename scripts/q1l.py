#!/usr/bin/env python3
"""Convert PNG to Q1L1 or decode Q1L1 to PNG using Pillow."""

import argparse
from pathlib import Path

from PIL import Image
from flipper.assets.q1l import from_image, to_image


def convert(source, target, *, delete_source=False):
    source, target = Path(source), Path(target)
    if target.exists():
        raise ValueError(f"Refusing to overwrite {target}")
    if source.suffix.lower() == ".png":
        with Image.open(source) as original:
            encoded = from_image(original)
            with to_image(encoded) as restored:
                if (
                    restored.size != original.size
                    or restored.tobytes() != original.convert("1").tobytes()
                ):
                    raise ValueError(f"Round-trip verification failed: {source}")
        target.write_bytes(encoded)
        # Verify the file on disk before removing any source.
        with to_image(target.read_bytes()) as restored:
            if from_image(restored) != encoded:
                raise ValueError(f"Written-file verification failed: {target}")
    elif source.suffix.lower() == ".q1l":
        with to_image(source.read_bytes()) as restored:
            restored.save(target, format="PNG")
            with Image.open(target) as written:
                if (
                    written.size != restored.size
                    or written.tobytes() != restored.tobytes()
                ):
                    raise ValueError(f"Written-file verification failed: {target}")
    else:
        raise ValueError(f"Unsupported input: {source}")
    if delete_source:
        source.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("encode", "decode"))
    parser.add_argument("input", type=Path)
    parser.add_argument("output", nargs="?", type=Path)
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Convert all matching files in a directory",
    )
    parser.add_argument(
        "--delete-source",
        action="store_true",
        help="Delete each source after verified conversion",
    )
    args = parser.parse_args()
    suffix, replacement = (
        (".png", ".q1l") if args.mode == "encode" else (".q1l", ".png")
    )
    try:
        if args.recursive:
            if not args.input.is_dir() or args.output:
                parser.error("--recursive requires an input directory and no output")
            sources = sorted(
                p
                for p in args.input.rglob("*")
                if p.is_file() and p.suffix.lower() == suffix
            )
        else:
            sources = [args.input]
        for source in sources:
            if source.suffix.lower() != suffix:
                parser.error(f"{args.mode} requires {suffix} input")
            convert(
                source,
                args.output or source.with_suffix(replacement),
                delete_source=args.delete_source,
            )
        print(f"Converted {len(sources)} file(s)")
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()

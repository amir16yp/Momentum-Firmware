"""Q1L1 lossless monochrome codec. No Pillow dependency for packed-byte I/O."""

import struct
import sys
from pathlib import Path

MAGIC = b"Q1L1"
# Bound both packed storage and Pillow's unpacked, one-byte-per-pixel image.
MAX_PIXELS = 64 * 1024 * 1024
MAX_DECODED_BYTES = 8 * 1024 * 1024


def decoded_size(width, height):
    if not (0 < width <= 0xFFFFFFFF and 0 < height <= 0xFFFFFFFF):
        raise ValueError("Q1L dimensions must be positive uint32 values")
    stride = (width + 7) // 8
    if (
        width > MAX_PIXELS // height
        or stride > min(sys.maxsize, MAX_DECODED_BYTES) // height
    ):
        raise ValueError("Q1L dimensions exceed supported allocation limits")
    return stride * height


def decode(data):
    """Return (width, height, packed MSB-first rows), rejecting invalid streams."""
    if len(data) < 12 or data[:4] != MAGIC:
        raise ValueError("Invalid Q1L1 header")
    width, height = struct.unpack_from(">II", data, 4)
    size = decoded_size(width, height)
    output = bytearray()
    pos = 12
    while len(output) < size:
        if pos == len(data):
            raise ValueError("Truncated Q1L payload")
        control = data[pos]
        pos += 1
        kind, length = control >> 6, (control & 63) + 1
        if length > size - len(output):
            raise ValueError("Q1L opcode exceeds decoded size")
        following = length if kind == 0 else int(kind == 3)
        if following > len(data) - pos:
            raise ValueError("Truncated Q1L opcode")
        if kind == 0:
            output.extend(data[pos : pos + length])
        else:
            value = data[pos] if kind == 3 else (255 if kind == 2 else 0)
            output.extend(bytes([value]) * length)
        pos += following
    if pos != len(data):
        raise ValueError("Trailing Q1L payload bytes")
    return width, height, bytes(output)


def encode(width, height, packed):
    """Encode packed rows using a minimum-size payload (dynamic programming)."""
    size = decoded_size(width, height)
    if len(packed) != size:
        raise ValueError("Packed data length does not match Q1L dimensions")
    packed = bytearray(packed)
    if width % 8:
        stride = (width + 7) // 8
        mask = (255 << (8 - width % 8)) & 255
        for pos in range(stride - 1, size, stride):
            packed[pos] &= mask
    # Compact arrays avoid Python-object overhead for large images.
    from array import array

    costs = array("I", [0]) * (size + 1)
    choices = bytearray(size)
    run = 0
    for pos in range(size - 1, -1, -1):
        value = packed[pos]
        run = min(run + 1, 64) if pos + 1 < size and value == packed[pos + 1] else 1
        best, control = size * 2 + 1, 0
        for length in range(1, min(64, size - pos) + 1):
            cost = 1 + length + costs[pos + length]
            if cost < best:
                best, control = cost, length - 1
        kind = 1 if value == 0 else 2 if value == 255 else 3
        for length in range(1, run + 1):
            cost = (2 if kind == 3 else 1) + costs[pos + length]
            if cost < best:
                best, control = cost, (kind << 6) | (length - 1)
        costs[pos], choices[pos] = best, control
    output = bytearray(struct.pack(">4sII", MAGIC, width, height))
    pos = 0
    while pos < size:
        control = choices[pos]
        kind, length = control >> 6, (control & 63) + 1
        output.append(control)
        if kind == 0:
            output.extend(packed[pos : pos + length])
        elif kind == 3:
            output.append(packed[pos])
        pos += length
    return bytes(output)


def from_image(image):
    """Use Pillow's default 1-bit conversion, matching the asset compiler."""
    decoded_size(*image.size)
    mono = image.convert("1")
    return encode(*mono.size, mono.tobytes())


def to_image(data):
    from PIL import Image

    width, height, packed = decode(data)
    return Image.frombytes("1", (width, height), packed)


def open_image(path):
    """Open either a Q1L source or an ordinary Pillow-supported image."""
    from PIL import Image

    if Path(path).suffix.lower() == ".q1l":
        return to_image(Path(path).read_bytes())
    return Image.open(path)

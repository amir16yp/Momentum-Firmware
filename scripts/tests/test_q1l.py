import io
import itertools
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from flipper.assets.q1l import decode, encode, from_image, to_image
from PIL import Image, ImageOps
from q1l import convert


def header(width, height=1):
    return struct.pack(">4sII", b"Q1L1", width, height)


class Q1LTests(unittest.TestCase):
    def test_file_conversion_and_deletion(self):
        with tempfile.TemporaryDirectory() as directory:
            png = Path(directory) / "input.png"
            q1l = png.with_suffix(".q1l")
            Image.new("1", (9, 3), 255).save(png)
            convert(png, q1l, delete_source=True)
            self.assertFalse(png.exists())
            convert(q1l, png, delete_source=True)
            self.assertFalse(q1l.exists())
            with Image.open(png) as image:
                self.assertEqual(image.size, (9, 3))
                self.assertEqual(set(image.getdata()), {255})
            q1l.write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                convert(png, q1l, delete_source=True)
            self.assertTrue(png.exists())
            with self.assertRaises(ValueError):
                convert(q1l, Path(directory) / "invalid.png", delete_source=True)
            self.assertTrue(q1l.exists())

    def test_spec_opcodes(self):
        for payload, expected in (
            (b"\x02\x12\x34\x56", b"\x12\x34\x56"),
            (b"\x43", b"\0" * 4),
            (b"\x81", b"\xff" * 2),
            (b"\xc2\x5a", b"\x5a" * 3),
        ):
            self.assertEqual(decode(header(len(expected) * 8) + payload)[2], expected)

    def test_invalid_streams(self):
        for data in (
            b"",
            b"Q1L1",
            b"BAD!" + b"\0" * 8,
            header(0),
            header(8, 0),
            header(0xFFFFFFFF, 0xFFFFFFFF),
            header(8),
            header(24) + b"\x02\x12\x34",
            header(8) + b"\xc0",
            header(8) + b"\x41",
            header(16) + b"\x40",
            header(8) + b"\x40\x40",
        ):
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode(data)

    def test_run_boundaries(self):
        rng = random.Random(1)
        for size in (1, 2, 63, 64, 65, 127, 128, 129, 1024):
            for data in (
                bytes(size),
                b"\xff" * size,
                b"\x5a" * size,
                rng.randbytes(size),
            ):
                encoded = encode(size * 8, 1, data)
                self.assertEqual(decode(encoded), (size * 8, 1, data))
                self.assertLessEqual(len(encoded), 12 + size + (size + 63) // 64)

    def test_rows_bit_order_and_padding(self):
        image = Image.new("1", (9, 2))
        for point in ((0, 0), (8, 0), (1, 1), (7, 1)):
            image.putpixel(point, 255)
        self.assertEqual(decode(from_image(image)), (9, 2, b"\x80\x80\x41\0"))
        self.assertEqual(
            decode(encode(9, 2, b"\x80\xff\x41\x7f"))[2], b"\x80\x80\x41\0"
        )
        # Readers ignore noncanonical padding visually.
        self.assertEqual(
            to_image(header(9, 2) + b"\x03\x80\xff\x41\x7f").getpixel((8, 1)), 0
        )

    def test_invalid_encoder_input(self):
        for width, height, data in ((0, 1, b""), (8, 1, b""), (8, 1, b"xx")):
            with self.assertRaises(ValueError):
                encode(width, height, data)

    def test_optimal_payload(self):
        # Independent exhaustive partition search for short inputs.
        def minimum(data):
            if not data:
                return 0
            candidates = []
            for length in range(1, min(64, len(data)) + 1):
                tail = minimum(data[length:])
                candidates.append(1 + length + tail)
                if len(set(data[:length])) == 1:
                    candidates.append((1 if data[0] in (0, 255) else 2) + tail)
            return min(candidates)

        for size in range(1, 6):
            for values in itertools.product((0, 255, 90), repeat=size):
                data = bytes(values)
                self.assertEqual(len(encode(size * 8, 1, data)) - 12, minimum(data))

    def test_pillow_and_firmware_polarity(self):
        for width in (1, 7, 8, 9, 17, 128):
            original = Image.new("RGB", (width, 8))
            original.putdata([(i * 37 % 256,) * 3 for i in range(width * 8)])
            restored = to_image(from_image(original))

            def xbm(image):
                out = io.BytesIO()
                ImageOps.invert(image.convert("1")).save(out, format="XBM")
                return out.getvalue()

            self.assertEqual(xbm(original), xbm(restored))


if __name__ == "__main__":
    unittest.main()

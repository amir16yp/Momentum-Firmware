# Q1L source assets

Firmware icons, application icons, animation frames, slideshows, and asset-pack
images use `.q1l` files with the `Q1L1` magic. Screenshots and documentation images
remain PNG. Q1L is a source format: the build decodes it into the existing firmware
bitmap/heatshrink representation, so the on-device renderer and asset ABI remain
compatible.

Install Pillow in your Python environment (`python -m pip install Pillow`), or use
the firmware toolchain's Python. Convert an image in either direction:

```sh
python scripts/q1l.py encode icon.png icon.q1l
python scripts/q1l.py decode icon.q1l preview.png
python scripts/q1l.py encode path/to/icons --recursive --delete-source
```

Output files are never overwritten. Encoding verifies pixels and the written
file before an optional source deletion. Directory conversion is per-file; an
error stops the command and leaves earlier successful conversions in place.
For non-monochrome inputs, Pillow's default `convert("1")` conversion is used,
matching the existing asset compiler. Only already-monochrome pixels are
losslessly preserved; Q1L cannot store color, alpha, or PNG metadata.

The reusable codec is `scripts/flipper/assets/q1l.py`. `encode(width, height,
packed)` and `decode(data)` operate on packed bytes without Pillow. `from_image`,
`to_image`, and `open_image` provide Pillow integration.

The 12-byte header contains `Q1L1`, a big-endian uint32 width, and a big-endian
uint32 height. Rows are byte-aligned and MSB-first, with black 0 and white 1.
Writers zero unused low bits at each row's end; readers ignore their visual
value. Control bits 7–6 select literal, zero, one, or repeated-byte runs. Bits
5–0 store the run length minus one (1–64 bytes). Literal runs carry their bytes;
repeated-byte runs carry one byte. The encoder uses dynamic programming to
minimize payload size across all four opcode kinds.

The decoder rejects bad headers, zero/unsupported dimensions, truncated opcodes,
output overruns, incomplete output, and trailing data. Python integer arithmetic
does not overflow; allocation checks occur before decoding. This implementation
supports at most 64 Mi pixels and 8 MiB of packed data, and also checks the native
size limit. Q1L has no checksum and cannot detect every structurally valid
corruption.

Run codec tests with:

```sh
python -m unittest discover -s scripts/tests -p test_q1l.py -v
```

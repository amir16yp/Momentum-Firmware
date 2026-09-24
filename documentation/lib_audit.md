# Non-submodule C library audit

Baseline: `e4a02f75a`.

## Scope

The inventory contains 433 tracked C translation units under `lib`, in 24 directories.
Gitlinks were identified using `git ls-files --stage lib`; no submodule was modified.
Repository-wide searches covered allocations, resizing, copies, string scans,
integer bounds, lookup tables, and loops. Detailed review concentrated on shared
bit/CRC helpers, containers, stream buffers, integer serialization, decoder
ownership, and file/input boundaries. This is a pattern-based review across the
whole tree, not a line-by-line verification of every protocol or vendor implementation.

The excluded gitlinks are FreeRTOS-Kernel, heatshrink, libusb_stm32, mbedtls,
microtar, mlib, nanopb, stm32wb_cmsis, stm32wb_copro, stm32wb_hal, and uzlib.
Non-submodule vendored code such as mJS, FatFs and u8g2 remained in the search scope.

## Changes

| Area | Change and rationale |
| --- | --- |
| Bit operations | Write a field using at most two byte updates instead of a per-bit loop. Avoid reading the following byte when a field fits in the current byte. Handle empty push/reverse operations. Simplify 16-bit reversal and use the byte reversal helper in CRCs. |
| NFC CRC | Replace eight polynomial iterations per byte with an equivalent byte update; no lookup-table RAM or flash cost. |
| Buffer pool | Allocate descriptors and aligned payloads together, removing one allocation/free per buffer. Preserve fundamental alignment even for odd buffer sizes. Check allocation/write arithmetic, support indices above 127, and avoid duplicate queuing after overruns. Reset size before publishing a buffer as free. |
| Infrared | Allocate the decoder context-pointer array with its owner, removing one allocation/free per decoder handler. Decoder callbacks and their ordering are preserved. |
| BitBuffer | Support overlapping self-slices, reject overflowing append lengths, clear stale bits when appending zero, and use a size-sized parity bit position. Accept the exact required parity output capacity. |
| SimpleArray | Preserve self-copy, compare all element bytes, check allocation multiplication, and avoid a redundant reset during copy. Constructor/copy/destructor callbacks retain their ordering. |
| Varints | Use unsigned ZigZag arithmetic for the full signed range. Bound decoding to five bytes, reject truncation/overflow without modifying outputs, and propagate failure through RFID pair decoding. Valid encodings and noncanonical but representable encodings are preserved. |
| mJS | Keep the reserved-word pointer table static and const instead of recreating an automatic array. |
| Asset packs | Validate representable dimensions/frame metadata and allocation lengths before allocating. Reject empty animation frames, truncated static headers and undersized fonts. Preserve successful loading and cleanup. |
| Input wrappers | Keep bytes 128–255 distinct from EOF; terminate a one-byte input buffer without reading or writing past it. |

Pool allocation consolidation reduces allocator bookkeeping and allocation calls,
but requires a larger contiguous free block. Alignment padding can add up to
`alignof(max_align_t)-1` bytes per payload. No claim is made about total live RAM
or final linked firmware size without a target build.

## Verification

New host tests compile production functions with strict warnings and independent
reference implementations. They cover all 65,536 16-bit reversals; field values,
alignments and exact allocation boundaries; CRC reflection modes and both NFC CRC
initializations; 10,000 randomized varint roundtrips and truncations; signed limits;
array callbacks and self-copy; 130-buffer pools, alignment and overrun recovery;
decoder ownership; asset allocation boundaries; and input byte/buffer boundaries.
The existing BitBuffer test adds overlapping slices, stale zero bits, overflowing
lengths and an 8192-byte parity export, alongside its 900 allocation lifecycles.

Commands (PowerShell):

```powershell
$env:HOT_PATH_ASAN='1'
$env:BIT_BUFFER_ASAN='1'
python -m unittest discover -s scripts/tests -p test_lib_audit.py -v
python -m unittest discover -s scripts/tests -p test_bit_buffer_memory.py -v
```

All six new tests and the extended BitBuffer test passed with AddressSanitizer.
Existing `test_hot_path_optimization.py`, `test_lib_memory.py` and
`test_storage_optimization.py` also passed (10 additional tests; 17 total).
`git diff --check` passed, and changed C files were formatted with clang-format.

### Directional Cortex-M4 code-size measurements

`python scripts/tests/measure_lib_audit.py e4a02f75a` compiles the helpers with
Clang, `--target=arm-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -Os`.
Firmware checks are replaced with traps. These are individual object-symbol
sizes, not the project's GCC/LTO linked image or CPU-cycle measurements.

| Function | Before (bytes) | After (bytes) |
| --- | ---: | ---: |
| bit_lib_set_bits | 84 | 118 |
| bit_lib_get_bits | 42 | 76 |
| bit_lib_reverse_16_fast | 8 | 8 |
| bit_lib_crc8 | 124 | 72 |
| bit_lib_crc16 | 106 | 106 |
| iso13239_crc_calculate | 80 | 76 |

The field setter trades some code size for bounded byte operations. The getter's
extra checks avoid out-of-bounds reads. The compiler already recognized the old
16-bit reversal; simplifying it does not reduce its measured machine code.

The checkout lacks the ARM GCC toolchain and populated submodules, so a full
firmware build and on-device timing/protocol tests were not performed. Host mocks
validate ownership and buffer ordering but do not establish ISR timing or hardware
behavior. Changes to peripheral timing and protocol state machines were avoided.

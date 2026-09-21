# Memory optimization checklist

Scope: firmware, libraries, and external apps under `applications/external` and
`applications_user`. Work proceeds from locally verifiable lifetime fixes to
changes needing hardware measurements. Each completed optimization gets its own
commit. Existing const and streaming work is documented in `const_ram_audit.md`
and the repository history.
Further 2048 optimizations are excluded at the user's request.

## Baseline (2026-09-22)

Default F7 compact build, `DEBUG=0`, starting revision `c9d8537ac`:

| Artifact | .text | .rodata | .data | .bss |
| --- | ---: | ---: | ---: | ---: |
| firmware.elf | 656568 | 181148 | 640 | 4972 |
| game_2048_d.elf | 2932 | 296 | 3136 | 0 |

Firmware `.data + .bss` is 5612 bytes, **not total RAM consumption**. The linker
also reserves the main stack and wireless shared-memory sections. RTOS stacks,
heap allocations and loader allocations require separate runtime measurements.
Firmware `.rodata` is flash; FAP code, constants and writable allocatable sections
are loaded into RAM. Making a FAP table const alone does not save its runtime RAM.

After a build, capture sections, largest symbols and source references for stack
configuration (including external manifests):

```sh
python scripts/memory_snapshot.py build/f7-firmware-C/firmware.elf --tools toolchain/x86_64-windows/bin --output build/memory-firmware.json
python scripts/memory_snapshot.py build/f7-firmware-C/.extapps/game_2048_d.elf --tools toolchain/x86_64-windows/bin --output build/memory-2048.json
```

Keep before/after `.elf.map` files alongside snapshots. Source references are
an inventory starting point; resolve multiline arguments and configuration
macros before assigning sizes to workers. Snapshots label runtime data pending.

## Easiest to hardest

- [x] Establish linked baseline and repeatable section/symbol/stack-reference capture.
- [x] External 2048: remove leaked temporary game-over allocation.
- [x] Firmware storage: remove duplicate per-file deletion paths.
- [x] Firmware dialogs: avoid an unused base-path allocation.
- [x] Resource extraction: reuse copy/path workspaces and compressed-seek scratch space.
- [x] Resource extraction: skip unchanged progress percentages.
- [x] Shared bit buffers: reject unsafe lengths and prevent reads past valid data.
- [x] Shared bit buffers: consolidate context, data and parity into one allocation.
- [x] Heap reallocation: bound copies by the old allocation's usable capacity.
- [x] Heap calloc: reject overflowing count-times-size requests before allocation.
- [x] NFC key dictionaries: validate hex before counting or returning keys.
- [x] NFC key dictionaries: bound retained line data to the key prefix.
- [x] RPC file downloads: release the payload after short or failed reads.
- [ ] External 2048: pack monochrome tile bitmaps (deferred by user request).
- [ ] Audit remaining small allocations, error cleanup and draw callbacks.
- [ ] Measure structure layouts and immutable data placement before modifying them.
- [ ] Inventory and measure every worker stack; reduce only with observed margin.
- [ ] Bound collections and parser lengths after measuring real bursts and inputs.
- [ ] Stream remaining whole-file readers; reduce simultaneous pipeline buffers.
- [ ] Allocate optional UI/state lazily and reuse mutually exclusive workspaces.
- [ ] Add a configuration-specific static RAM CI budget once validated.
- [ ] Complete hardware peak-memory, cancellation and concurrency verification.

## Hardware measurement protocol (pending)

Use CLI `free` and `top` (which includes configured stack bytes and minimum free
stack bytes). Record before launch, immediately after startup, worst-case activity,
and after exit: free heap, minimum-ever free heap, largest free block, and each
thread's stack minimum. The heap minimum is global since boot; reboot between
independent comparisons. Polling may miss brief peaks, and CLI instrumentation
itself consumes memory; keep instrumentation identical for before/after runs.

Repeat at least 100 open/work/close cycles, including failed opens, malformed and
maximum-size inputs, cancellation and disconnects. Compare post-exit heap and
largest free block across cycles. Exercise Bluetooth, GUI, storage and protocol
work concurrently. Record firmware revision, FAP revision, input sizes, stack
margin and observed worst peak; do not infer these values from section sizes.

No device measurements or stack reductions have been claimed in this pass.
Existing build warnings: invalid manifests for `cli_bridge` and `mtp`.

## Completed: external 2048

Game-over checks now inspect empty cells and mergeable neighbors directly,
eliminating the leaked 8-byte `MoveResult` allocation (plus allocator overhead)
on every full-board check and the 16-byte scratch board. The persistent move
result is allocated only after mutex creation succeeds, avoiding an error-path
leak. Stack configuration and saved-game layout are unchanged.

ARM FAP build and import checks pass. `.text` decreases from 2932 to 2680 bytes,
`.rodata` from 296 to 175, and `.data` stays at 3136: 373 fewer loaded section
bytes before loader alignment/metadata. Host regression compares all 65536
binary full boards and 10000 deterministic mixed boards against the existing
move functions, checks that input is unchanged, and rejects allocation in the
game-over function. Run `python -m unittest scripts.tests.test_2048_memory`.
Hardware repeated launch/exit and heap measurements remain pending.

## Completed: firmware recursive deletion

Reuse the traversal path for each file removal, then truncate it back to the
directory length. `storage_common_remove()` waits for the storage service before
returning, so the path stays valid throughout the request. This removes one
temporary `FuriString` and its duplicate directory prefix per file; backing-buffer
growth can still allocate. No fixed byte reduction in peak heap is claimed.

The host storage mock verifies 100 traversals with nested directories, siblings,
empty directories, 253-character filenames, missing paths and standalone files.
Root/nested directory-open failures release the file handle and string. It asserts
one string object allocation per traversal and preserves paths outside the tree.
Run `python -m unittest scripts.tests.test_recursive_remove_memory`.
This validates traversal and object lifetime, not the target allocator or SD driver.

Clean ARM firmware and 2048 FAP builds pass, including SDK/import checks. Final
firmware sections: `.text` 656572, `.rodata` 181148, `.data` 640, `.bss` 4972.
Static RAM is unchanged; the intended benefit is removal of overlapping dynamic
path objects. Final snapshots are in `build/memory-final-{firmware,2048}.json`.
The initial raw map/snapshot files were removed by an external build-directory
cleanup during this pass; the baseline section values above were recorded before
that cleanup. Future passes should retain raw baselines outside a cleaned build tree.

## Completed: optional file-browser base path

The dialog now allocates a base-path string only when the caller supplies one.
Otherwise it passes the same empty path as a static literal. Supplied paths still
undergo alias resolution and remain alive until the dialog service finishes;
selection and cancellation use the same cleanup. This avoids one 12-byte
`FuriString` object plus allocator overhead for the entire default dialog lifetime
(object size confirmed in the ARM `furi_string_alloc` disassembly).

ARM firmware and SDK checks pass; formatting and the downstream browser/worker
lifetimes were reviewed. `.data` remains 640 and `.bss` 4972. This saves dynamic
memory, not static RAM; actual hardware peak-heap comparisons remain pending.

## Completed: resource extraction and decompression workspace

Tar extraction reuses one 10 KB copy buffer across accepted files and one output
path string across entries. Filename conversion gets a scratch string only when
a converter is supplied. The immutable archive size is queried once per unpack
operation rather than after every output chunk. Output files are still closed
between entries, and negative reads or partial writes now fail extraction.

Compressed tar forward seeks discard data through microtar's existing 512-byte
raw-header buffer. The parsed header lives separately, and raw-header bytes are
not used during seeks. This removes the previous temporary 10 KB allocation for
each padding/skip seek, without a new decode buffer or a larger thread stack.
The compressed stream gains one pointer to its tar reader. Large skipped files
are discarded in 512-byte chunks; their device timing still needs measurement.
The gzip/heatshrink formats, dictionaries and compression settings are unchanged.

`python -m unittest scripts.tests.test_tar_extraction` compiles the production
extraction/seek code with the actual microtar parser and uzlib gzip decoder.
The mock output disk verifies every byte for 0/1/511/512/513/25001-byte files,
a skipped 23001-byte file, repeated extraction, conversion and single-file lookup.
It injects open/read/write/mkdir failures and checks allocation cleanup.
Six output files use one copy-buffer allocation; seven write chunks use one
archive-size query. These are operation-count improvements, not measured SD-card
speedups. Hardware elapsed time, heap minimum and cancellation tests remain pending.
Both `firmware_all` and `updater_all` ARM builds pass, including SDK checks.

## Completed: extraction progress coalescing

Only notify the updater UI when the resource percentage changes. The previous
path formatted an allocated status string and invoked the UI callback on every
output chunk, even when compressed-input buffering left progress unchanged.
The existing percentage calculation, stage transitions and error reporting are
preserved. No per-operation state or extra RAM is introduced.

`python -m unittest scripts.tests.test_extraction_progress` verifies every
percentage change, repeated input positions and an empty/reset stream. A synthetic
100001-notification sequence now invokes the UI 99 times while preserving every
displayed percentage. This reduces formatting/UI work; it is not an end-to-end
device timing benchmark. Static sections after the extraction changes remain
`.data` 640 / `.bss` 4972 for firmware and 312 / 4196 for updater.
Firmware and updater ARM rebuilds and formatting checks pass for this change.

## Completed: bit-buffer boundary checks

Bit reads no longer unconditionally fetch a second byte at the end of a buffer.
They validate the logical bit index and return zero for bits beyond the stored
length, including unused bits in a partial final byte. Parity imports reject
decoded data larger than the destination before writing, and zero-bit imports
do not dereference input. Slice writes check bounds using subtraction so that
an overflowing offset-plus-length cannot bypass validation. Allocation rejects
capacities whose bit count cannot fit in `size_t`.

`python -m unittest scripts.tests.test_bit_buffer_memory` compiles the production
implementation and checks 900 lifecycles across capacities 1 through 512 bytes,
every bit offset, partial final bytes, independently encoded parity, oversized
inputs, overflowing slices, cleanup and allocation canaries. The same test passes
with AddressSanitizer enabled by `BIT_BUFFER_ASAN=1`. Firmware and updater ARM
builds and SDK checks pass. Hardware NFC interoperability and heap measurements
remain pending.

## Completed: contiguous bit-buffer storage

The opaque context now owns its byte data and parity in one allocation instead
of three. A flexible array removes the data pointer, reducing the ARM context
from 16 to 12 bytes; ARM disassembly confirms the new offset and single malloc.
No extra stack or static buffer is introduced, and the public API is unchanged.
Allocation sizes are checked before calculating the combined request.

With the current allocator's 8-byte block header and 8-byte alignment, a 32-byte
buffer requires 56 rather than 80 heap bytes, and a 256-byte buffer requires 312
rather than 328 bytes, assuming normal block splitting. These are calculated
allocation costs, not hardware peak-heap readings; unsplittable free-block tails
and fragmentation can affect actual consumption. Each lifecycle also removes
two malloc/free pairs.

The 900-cycle host regression now asserts exactly one allocation and no retained
bytes after each free, and passes with AddressSanitizer. Firmware and updater
ARM builds and SDK checks pass. Compared with the saved pre-pass firmware ELF,
`.text` changes from 656768 to 656832 bytes; `.rodata` stays 181156, `.data` 640
and `.bss` 4972. This is a dynamic allocation optimization, not a static RAM
reduction. Before/after ELF and map files are retained in `.memory-audit/`.

## Completed: safe heap reallocation

Growing `realloc()` previously copied the requested new size from the old block,
reading beyond its allocation. The heap implementation now obtains the old usable
capacity from its own block header and copies only the smaller of that capacity
and the new size. Heap metadata stays private to the allocator; the public
`realloc` and newlib wrapper APIs are unchanged. Zero-size reallocation still
frees the old block, and null-input reallocation still allocates a new block.
Usable capacity includes allocator padding; the heap does not track exact
original request sizes. Existing fail-fast allocation-failure behavior is retained.

`python -m unittest scripts.tests.test_memmgr_memory` compiles the production
reallocation helper and wrapper with guarded mock heap blocks. It checks 7200
grow/shrink/equal-size/free lifecycles, null input, preserved bytes, copy bounds
and balanced ownership, and passes with `MEMMGR_ASAN=1` (AddressSanitizer).
This is a host check of the copy and ownership logic, not an RTOS allocator or
concurrency test. Firmware and updater ARM builds and SDK checks pass. Firmware
`.text` increases by 16 bytes to 656848; `.data` 640 and `.bss` 4972 are unchanged.
Hardware repetition and concurrent workloads remain pending.

## Completed: calloc multiplication overflow

`calloc()` now checks the product before multiplying count by element size.
Previously, an overflowing request could wrap to a small allocation and allow
subsequent caller writes to overrun it. The new guard follows the firmware's
existing fail-fast allocator policy; zero-size requests retain their existing
behavior. The newlib calloc wrapper delegates to the same guarded function.

The memory-manager host regression now also covers overflowing products that
wrap to zero and to nonzero values, reversed factors, the newlib wrapper, all
1089 positive count/size combinations from 1 through 33, zero-filled contents,
and zero-size behavior. Overflow cases assert that allocation is never attempted.
The complete regression passes with AddressSanitizer; firmware and updater ARM
builds and SDK checks pass. Firmware `.text` increases by a further 32 bytes to
656880; `.data` 640 and `.bss` 4972 remain unchanged. This is a memory-safety fix,
not a reduction in runtime heap consumption.

## Completed: dictionary hex validation

Dictionary entries must now contain a valid hexadecimal key prefix before they
are counted or returned. Previously the parser accepted any prefix of the right
length, and ignored conversion failure, allowing an uninitialized or previous
byte to become part of the returned key. Malformed entries are skipped without
changing the output key. The key-size calculation is checked for overflow before
allocating the dictionary. Case-insensitive hex, comments, CRLF, final lines
without a newline and the existing ignored-suffix behavior remain supported.

`python -m unittest scripts.tests.test_keys_dict_memory` compiles the production
parser, iterator, hex decoder and line reader with tracked host strings and a
mock stream. AddressSanitizer passes for 3200 repeated scans with read sizes
1 through 32, invalid characters and embedded NUL at every key position, empty
input, short keys, and one-megabyte comments/suffixes. Startup counting and
iteration agree on the accepted entries, and no string objects remain allocated.
Firmware and updater ARM builds and SDK checks pass. Hardware dictionary attack
and storage-error testing remain pending. The long-line regression still reaches
2097152 bytes of string capacity in the host growth model before the separate
bounded-reader optimization; this is not a measured target allocator value.

## Completed: bounded dictionary line memory

Dictionary reads now retain at most `2 * key_size` characters while consuming
the rest of each line in 32-byte chunks. Long comments and ignored suffixes no
longer grow a temporary string with the input length. Six-byte Classic keys
retain at most 12 characters; 16-byte Ultralight C keys retain at most 32.
The reader uses a 32-byte stack scratch buffer, the same chunk size used by
the previous generic line reader; configured thread stacks are unchanged.
An unsuccessful seek back after a newline discards the partial result and ends
iteration rather than returning a key with an uncertain next-entry position.

The same one-megabyte comment/suffix fixtures now peak at 16 bytes of backing
string capacity in the host growth model, versus 2097152 before this change.
That compares the test allocator, not target heap measurements. Tests additionally
cover 1/6/16/32-byte keys, blank lines, short reads, embedded NUL, failed seeks
and a long final line without a newline. The full dictionary regression passes
with AddressSanitizer. Firmware and updater builds and SDK checks pass; firmware
`.text` is 657040, `.rodata` 181156, `.data` 640 and `.bss` 4972. Before/after
ELFs and maps are retained under `.memory-audit/keys-*`. Hardware heap minima,
dictionary-attack throughput and concurrent storage workloads remain pending.

## Completed: RPC download error cleanup

RPC file downloads now release nested protobuf allocations on every exit. A
short or failed read previously skipped the send-and-release call, leaving its
payload allocated. Nanopb clears released pointer fields, so final cleanup is
also safe after successful sends and failed file opens.

`python -m unittest scripts.tests.test_rpc_read_memory` uses the production read
handler, generated message descriptors and real nanopb encoder/decoder/release.
With `RPC_READ_ASAN=1`, 2100 transfer scenarios pass: empty, 1/511/512/513/1024/
2601-byte files, open failure, absent send callback, and zero/short reads at each
chunk. Every delivered byte and continuation flag is checked, with no retained
payloads or file handles. Firmware/updater ARM builds and SDK checks pass.
Hardware transport timing and disconnect concurrency remain pending.

# Fork optimization summary

Reviewed on 2026-09-22, from the parent of `72b1ad8b07db3458fe823ac3d3329ea64392654d` through `0ddb14616`, including the first commit and the two committed external-app changes. Estimates come from source changes, allocation sizes, and recorded build sections—not benchmarks.

The fork has become more memory-efficient and substantially better at preventing memory loss during repeated use. Most improvements target heap usage, allocation overhead, and reliability; everyday UI speed probably changes much less.

## Overall impact

- **About 0.87 KiB less baseline RAM use** when virtual disks have not been used: 312 bytes moved to flash, plus roughly 576 heap bytes from deferring the virtual filesystem object.
- **Hundreds of bytes to several KiB saved in particular workflows**, depending on entries, buffers, and input sizes.
- **Much less accumulating memory waste** across repeated browsing, app launches, Bluetooth profile changes, JavaScript activity, and gameplay.
- **Often 67–99% fewer allocations for the specific buffers optimized.** That percentage applies to those allocations, not total CPU time or firmware speed.
- **Likely small everyday speed gains**, with stronger benefits in allocation-heavy operations and longer sessions that previously accumulated leaks. There is no defensible single “firmware is X% faster” figure.

## RAM and allocation improvements

| Area | What changed | Approximate benefit |
| --- | --- | --- |
| Immutable firmware data | Read-only tables and callbacks moved into flash | **312 bytes permanently saved.** Initialized writable data fell from 952 to 640 bytes: **33% smaller `.data`**, or about **5.3% less `.data + .bss`**. These sections are only part of total RAM use. |
| Virtual filesystem | Allocate `/mnt` FatFs state on first mount | **~576 heap bytes saved until first use**; the object stays allocated afterward. |
| Archive file entries | Ordinary files/folders no longer allocate empty custom names | **~24 bytes per retained ordinary entry**. For 50 entries, ~1.2 KB; for 100, ~2.4 KB. |
| Archive icons | Reuse destination icons and free obsolete ones | Prevents losing **32-byte icon allocations**, normally ~40 bytes including heap overhead, on affected assignments. |
| Shared bit buffers | Context, data, and parity share one allocation | **3 allocations → 1**, a **67% reduction**. A 32-byte buffer uses about **80 → 56 heap bytes, 30% less**; a 256-byte buffer **328 → 312 bytes, 5% less**. |
| Default file dialogs | Allocate the optional base-path string only when supplied | **~24 heap bytes saved** throughout an ordinary dialog’s lifetime. |
| BadUSB | Create text/byte editors only while their configuration screen is open | Avoids keeping two editor object trees alive during normal use. **Hundreds of bytes of gross savings**, partly offset by retained rendering scratch space. |
| 2048 | Directly inspect empty cells and mergeable neighbors | Removes a leaked **8-byte object, roughly 16 heap bytes, per affected full-board check**, plus the 16-byte scratch board. Recorded loaded sections shrink **373 bytes, roughly 6%**. |
| NFC dictionaries | Retain only the key prefix instead of whole lines | Temporary retained text becomes **12 characters for Classic keys or 32 for Ultralight C**, regardless of comment/suffix length. Large lines go from potentially heap-exhausting to a tiny bounded buffer. |

The fixed 312-byte saving is documented in the [immutable-data audit](const_ram_audit.md); allocation calculations are covered in the [memory optimization notes](MemoryOptimizationChecklist.md).

## Less repeated work and temporary buffering

| Area | Improvement | Rough scale |
| --- | --- | --- |
| Resource extraction | Reuse one 10 KiB copy buffer and output-path string across files | For 100 extracted files, **100 copy-buffer allocations → 1**, about **99% fewer**. The active copy buffer still occupies 10 KiB. |
| Compressed archive seeks | Reuse an existing 512-byte header buffer | Eliminates a separate **10 KiB temporary allocation per forward skip/seek**. This is not automatically 10 KiB less peak usage for the entire extraction. |
| Extraction progress | Notify the UI only when the percentage changes | Roughly **100 distinct progress updates**, instead of potentially thousands of repeated formatting/callback operations. |
| Archive-size queries | Read the size once per unpack operation | **One query instead of one per output chunk.** |
| RPC downloads | Reuse one payload across 512-byte chunks | A 1 MiB download needs **1 payload allocation instead of 2,048**, over **99.9% fewer payload allocations**. Serialization allocations remain. |
| Directory walking | Keep reusable filename scratch storage | Removes an allocation/free pair per iterator call; a large traversal avoids roughly thousands of pairs. |
| Recursive deletion | Reuse the traversal path | Removes a temporary string object and duplicate path prefix **per deleted file**. |
| Sub-GHz rendering | Reuse a drawing string and skip labels that timestamps replace | Removes repeated string creation/destruction on redraws and some unnecessary formatting. |
| BadUSB rendering | Reserve reusable drawing storage; avoid unchanged interface redraw requests | Removes per-frame temporary string allocation and some redundant rendering. |
| EMV processing | Avoid temporary Track 1 copies; format record labels only with trace logging enabled | Removes an **80-byte temporary array**, per-record heap-string work, and unused save-time string allocation. |
| Asset signatures | Compare in 512-byte chunks | Removes a whole-signature heap copy; uses **512 bytes of stack scratch** instead. |
| INA Meter configuration | Stream records instead of loading the whole file | Memory scales with the **longest retained line/section plus 512-byte scratch**, rather than the entire file. Large files with short lines benefit most; tiny configurations may not save memory. |

These changes also reduce opportunities for heap fragmentation. Reused buffers sometimes remain allocated longer, so **less allocation churn does not always mean less idle RAM**.

## Leak fixes across the firmware

The initial cleanup covered Clock Settings timers, the NFC API resolver, updater status strings, desktop timers and animation-manager resources, Sub-GHz dialogs and keystore cleanup, browser selection strings, Archive MD5 text, Saflok parsing, infrared button naming, application asset paths, JavaScript failed file/directory opens, Bluetooth HID/serial profiles, and desktop storage-record ownership.

Later changes also fixed RPC failed-read payload cleanup, JavaScript rejected/pending queue messages, Archive icon ownership, and the 2048 leak.

Their benefit grows with use. For example:

- A typical leaked empty string costs around **24 heap bytes**, with longer strings costing more. Preventing 100 such leaks preserves roughly **2.4 KB or more**.
- Preventing 1,000 affected 2048 checks from leaking ~16 bytes each preserves roughly **16 KB**.
- JavaScript queue fixes release both message holders and, for rejected sends, GC roots that could otherwise keep larger JS values alive.

These are illustrative accumulated savings, not amounts every session will recover. Desktop teardown fixes, for example, only help when that teardown actually occurs.

## Reliability improvements

The changes also fixed unsafe `realloc` copying, `calloc` multiplication overflow, bit-buffer bounds, invalid dictionary keys, typed-array index overflow, negative JS I2C lengths, EMV field bounds/serialization, string-buffer reuse state, incorrect frees of flash icons, and virtual-disk transfer truncation at the storage API’s 16-bit limit.

These primarily prevent crashes, corruption, and failed operations. Some add small checks or code size. The `realloc` fix also reduces copying when growing a buffer: approximately doubling a block now copies the old capacity instead of the new size—**roughly half the bytes for that copy**.

## Work not counted as delivered performance gains

The responsiveness/CPU inventories identify future work; they do not themselves accelerate firmware. Q1L tooling likewise does not establish an on-device RAM or rendering improvement. Rebranding and added tests are excluded from the savings.

At review time, the external-app working tree also had **uncommitted changes across 11 files** involving AVR ISP, CAN Commander, legacy NFC, Polybius, Flip Downloader, and Flip Social. These mostly improve bounds, string handling, and invalid-input behavior, with **little predictable speed or RAM benefit**. They are separate from the committed results above.

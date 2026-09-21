# Memory optimization checklist

Scope: firmware, libraries, and external apps under `applications/external` and
`applications_user`. Work proceeds from locally verifiable lifetime fixes to
changes needing hardware measurements. Each completed optimization gets its own
commit. Existing const and streaming work is documented in `const_ram_audit.md`
and the repository history.

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
- [ ] External 2048: pack monochrome tile bitmaps without a decode buffer.
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

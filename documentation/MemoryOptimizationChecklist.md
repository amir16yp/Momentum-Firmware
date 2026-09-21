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
- [ ] External 2048: remove leaked temporary game-over allocation.
- [ ] Firmware storage: remove duplicate per-file deletion paths.
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

# Immutable data RAM audit

Audited against the default F7 compact firmware configuration (`COMPACT=1`,
`DEBUG=0`). Changes are limited to objects whose uses establish immutability;
no casts were added to bypass const checking.

## Placement and scope

The audit combined source searches for initialized globals, static locals,
arrays, pointer tables and callback structures with inspection of the linked
firmware's writable `.data` and `.bss` symbols. Uses of changed objects were
checked for writes, escaping pointers, and the receiving APIs' behavior.

`targets/f7/stm32wb55xx_flash.ld` places `.rodata` in FLASH and `.data` in RAM1.
An array of `const char*` still has writable pointer elements; its pointers
need their own `const` qualification.

External applications and plugins are different: `elf_preload_section()` and
`elf_load_section_data()` in `lib/flipper_application/elf/elf_file.c` allocate
all nonempty allocatable sections, including `.rodata`, in RAM. External-only
tables were therefore not changed under the premise that const would put them
in flash. This also applies to main-menu apps configured as `MENUEXTERNAL`.
The RAM-executed updater likewise intentionally places `.rodata` in RAM.

The measured result covers the default firmware, not every possible combination
of application manifests, vendor ports, or conditional compilation options.

## Confirmed RAM savings

These symbols moved from writable RAM (`D`/`d`, addresses starting `0x200...`)
to read-only flash (`R`/`r`, addresses starting `0x080...`) in the rebuilt ELF.
Their symbol sizes did not change.

| Object | Bytes | Evidence of immutability |
| --- | ---: | --- |
| `cli_shell_line_key_combo_set` | 120 | Parser reads key records and calls handlers; mutable line state is passed separately as context. |
| `cli_shell_completions_key_combo_set` | 64 | Same dispatch pattern; completion state is separate. |
| `component_key_combo_sets` | 8 | Parser only indexes the two fixed set pointers. |
| `desktop_keybinds_defaults` | 32 | Defaults are copied into mutable `FuriString` settings. |
| `desktop_keybind_keys` | 16 | Used only to format setting names. |
| `desktop_keybind_types` | 8 | Used only to format setting names. |
| Archive `units` | 20 | Indexed only for display formatting. |
| Storage `mnt_driver` | 20 | FATFS already accepts and stores a pointer to a const driver; dispatch only reads callbacks. |
| CLI `cdc_callbacks` | 20 | CDC stores the pointer and calls callbacks; it never changes the callback record. |
| `FLIPPER_AUTORUN_APP_NAME` | 4 | Generated once from the build option; loader only reads it. |
| **Total** | **312** | |

Firmware section comparison:

| Section | Before | After |
| --- | ---: | ---: |
| `.data` | 952 | 640 |
| `.bss` | 4972 | 4972 |
| `.rodata` | 180828 | 181140 |

Other verified immutable objects now explicitly express their existing read-only
use: archive and file-browser icon/name tables, keyboard pointers, dolphin deed
limits, RPC callbacks, the BLE HID configuration copy template, font names, NFC
names and dispatch tables, POCSAG text, update-result descriptions, and infrared
GPIO pointers. The compiler already optimized these into read-only data or
eliminated their storage in this configuration; they are not counted as extra
RAM savings.

## Const propagation

The CLI set declarations and parser pointer now agree on const qualification.
The CDC setter accepts `const CdcCallbacks*`; its registration slots remain
writable, and their existing volatile access to the callback records is retained.
Existing callers supplying mutable callback records remain accepted. The SDK
signature records the qualification change; the function's name, calling
convention, and structure layout are unchanged.

The autorun pointer's generator and extern declaration were updated together.
The application source generation rule now depends on its generator, avoiding
stale generated declarations during incremental firmware and updater builds.

## Candidates deliberately left writable

- NexWatch `magic_items`: the decoder stores calculated checksums in `.chk`.
- Firmware `version`: `version_set_custom_name()` changes its name pointer.
- HID/CCID device descriptors: initialization changes VID/PID and string indexes.
- USB interfaces: CDC and HID/CCID initialization changes descriptor pointers;
  the U2F interface also remains exposed through the mutable USB configuration
  API, so it was not const-qualified through a cast.
- GPIO bus structures, serial state, Sub-GHz settings, RGB settings/state,
  FATFS bookkeeping, runtime buffers, RTOS state, and BLE shared-memory queues:
  these contain runtime state or receive writes through driver APIs.
- `memset_func` and `uxTopUsedPriority`: already `const volatile`, with deliberate
  security/debugger semantics. Removing volatile is outside this change.

## Validation

- ARM firmware and updater builds, including the SDK consistency check.
- GPIO external application build and import checks, exercising an existing
  caller of the const-qualified CDC setter.
- Before/after `arm-none-eabi-nm -S` symbol sizes, types and addresses, and
  `arm-none-eabi-size -A` section sizes.
- Formatting and `git diff --check`.

The build emits existing invalid-manifest warnings for `cli_bridge` and `mtp`.
No on-device runtime test was performed.

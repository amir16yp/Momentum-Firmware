# Firmware responsiveness audit

Audited 2026-09-22. Firmware HEAD: `c9d8537ac`; checked-out external applications: `f82ee1235d3540f77a1cbc21c71c4f2668d7a838`.

This is a source audit and a decision document. No firmware behavior was changed. No device timings, CPU profiles, SD benchmarks, or hardware reproduction results were collected. Numeric delays below come from code; they are not measured end-to-end latency. Source paths are relative to the repository root, and line numbers refer to this revision.

The strongest candidates for everyday sluggishness are release-based menu navigation, optional input vibration, SD/display bus contention, repeated directory and application-metadata reads, and synchronous work while GUI models are locked. Severe intermittent stalls can also propagate through blocking queues and the shared timer task. These are different problems and should not be treated with one global timeout or priority change.

## Scope and confidence

Manually traced: input -> pubsub -> GUI -> view dispatcher/view holder -> model -> canvas -> display; storage request serialization and SD recovery; file picker and Archive enumeration, metadata, favorites and search; menu and FAP loading; desktop animations and asset packs; logging, timers, notifications, allocator/build settings; selected built-in and external app delays.

The [searchable inventory](performance/responsiveness_inventory.csv) covers timing/blocking patterns in checked-out C/C++ sources under `applications`, `applications_user`, `furi`, `targets`, and `lib`, including external apps and vendor libraries. Regenerate with `python documentation/performance/generate_inventory.py` from the repository root. It follows ripgrep's normal ignore rules. It is a lexical inventory: comments, declarations, inactive configurations, tests, and healthy idle waits can match; multiline expressions may need surrounding context. It is not a call graph, proof of reachability, or a complete inventory of expensive computation or synchronous I/O.

At this revision the inventory has **3,172 category/line records across 1,111 files**: 1,011 explicit-delay matches, 1,091 unbounded-wait matches, 456 timer/UI-cadence matches, 247 joins, 90 blocking notifications, 263 critical-section/scheduler-lock matches, and 14 priority changes. A source line can have multiple categories. These counts are search coverage, not defect counts.

It is not possible to establish *every* cause of perceived slowness from source alone. External apps received a broad pattern scan and selected manual checks, not exhaustive individual reviews. Radio protocol implementations, vendor drivers, JavaScript execution/GC, interrupt load, Bluetooth connection behavior, SD electrical/card behavior, and the actual flashed build remain areas for hardware profiling. The report distinguishes:

- **Confirmed mechanism:** the code directly establishes the delay, repeated work, or blocking dependency. Its real-world frequency/size may still be unknown.
- **Conditional risk:** a stall requires contention, unusual input, a failing peripheral, or another slow component. No occurrence is claimed.
- **Measurement candidate:** plausible overhead without evidence that it dominates latency.

Priorities are investigation order, not instructions to implement: **P1** shared/high-impact or common interaction; **P2** workflow-specific; **P3** small or unmeasured overhead. A rare hang can be serious without being the first performance optimization.

## Decision overview

| IDs | Symptom | Priority | Most useful decision |
| --- | --- | --- | --- |
| 01–03 | Buttons feel late; held scrolling starts slowly | P1 | Keep release/long-press semantics, or change navigation behavior; retain vibration but move its timing off input scanning |
| 04–07 | Whole UI stalls when an app is busy | P1 | Bound callback work and define queue backpressure/cancellation behavior |
| 08–11 | SD activity freezes screen or delays storage readiness | P1 | Measure bus hold times; separate recovery/maintenance from interactive work |
| 12–16 | Folders, app lists, context menus, search feel slow | P1/P2 | Cache metadata/favorites; avoid repeated scans; improve cancellation |
| 17–20 | Launch, exit, boot, desktop transitions pause | P2 | Profile each loading stage; choose RAM/cache versus I/O tradeoffs |
| 21–24 | Animation jitter, verbose-log slowdown, delayed notifications | P2/P3 | Profile draw/lock time; shorten timer callbacks; reduce synchronous logging |
| 25–28 | Specific actions/app workflows contain visible pauses | P2 | Keep protocol timing while making UI handling asynchronous where useful |
| 29–32 | Low-memory jitter, build-sensitive speed, remote-screen lag, conditional hang | P3 / conditional | Measure before optimizing; address demonstrated correctness issues separately |

## Input and shared UI infrastructure

### 01. Main-menu movement waits for release; held movement waits for repeat

**Confirmed mechanism, P1.** `applications/services/gui/modules/menu.c:580` handles navigation on `InputTypeShort` or `InputTypeRepeat`. `applications/services/input/input.c:140` emits Short on release, provided the long-press threshold has not been reached. Physical button-down therefore does not immediately move this menu. The user's own hold time contributes directly to perceived latency.

`input.c:16–17,37–54` configures a 150-tick timer, Long on callback 2, then Repeat on subsequent callbacks. With the 1 kHz tick in `targets/f7/inc/FreeRTOSConfig.h:28`, nominal Long is 300 ms, first Repeat 450 ms, then repeats every 150 ms (~6.7 per second), after debounced press and assuming timely callbacks. These timings are not a global frame-rate limit. A long hold in a menu that ignores Long has no navigation response until Repeat.

**Options/tradeoff:** move directional navigation to Press, keeping Short/Long for actions; add repeat acceleration; or keep existing behavior. Changing a shared input threshold affects all apps and long-press gestures. Prevent duplicate movement on both Press and Short. **Verify:** measure press-to-highlight and release-to-highlight separately; test quick taps, held keys, diagonals, and long-press actions.

### 02. Debounce and timer shutdown add a smaller baseline

**Confirmed mechanism, P3.** `targets/f7/furi_hal/furi_hal_resources.h:12` sets `INPUT_DEBOUNCE_TICKS` to 4. `input.c:118–174` moves a saturating counter one step per scan and sleeps one tick while changing. This is roughly a few milliseconds under an otherwise idle scheduler, not a guaranteed exact 4 ms. On release it stops the press timer and waits in one-tick steps until that timer stops (`input.c:144–147`). A backed-up timer service can stretch the release path considerably.

**Options/tradeoff:** leave debounce unless measured as significant; investigate timer-service stalls before shortening it. Less debounce risks duplicate/noisy presses. **Verify:** scope GPIO edges against event publication and repeat with timer-task contention.

### 03. Optional vibration sleeps inside the sole input scanning task

**Confirmed mechanism, P1 when enabled.** `input.c:159–165` publishes the current Press/Release, then sleeps one tick, enables vibration, sleeps the configured level, and disables it. `applications/settings/input_settings_app/input_settings_app.c:23–35` exposes 13–36 ticks and Press/Release/Both triggers: nominal **14–37 ms per configured trigger** without scanning other buttons. Both triggers can occupy 28–74 ms across a full press/release cycle. This is not necessarily that much extra latency for the already-published event; it delays subsequent scanning and can miss a sufficiently short intervening tap.

Defaults fall back to level zero (`input_settings.c:55–60`), so this is not an always-on cost. **Options/tradeoff:** leave off, use shorter/one-sided haptics, or schedule pulse completion outside Input. A worker/timer solution must handle overlapping pulses and vibration ownership. **Verify:** fast alternating taps with levels 0 and 9, for each trigger mode.

### 04. Queue backpressure can spread an app stall across the device

**Conditional risk, P1.** `gui.c:55,65,644` uses blocking puts into eight-entry GUI input/ASCII queues. `view_dispatcher.c:5,261,271,397` uses 16-entry queues with blocking puts for app input/ASCII/custom events. `furi/core/pubsub.c` (`furi_pubsub_publish`) calls subscribers synchronously while holding its mutex.

Dependency: slow app callback -> app queue fills -> GUI blocks delivering input -> GUI cannot draw/drain its input queue -> Input publisher can block. Repeat publication originates in the shared timer task, so this can also delay unrelated timers. It takes a backlog or a circular dependency; `FuriWaitForever` alone is not evidence of a bug.

**Options/tradeoff:** remove long work from callbacks first; instrument queue depth and blocking time; define which repeat/update events may be coalesced. Simply enlarging queues can retain more stale input. Dropping arbitrary events is unsafe because Press/Release pairing is used to track held keys. **Verify:** sustained input during slow storage and remote ASCII bursts, including release delivery after cancellation.

### 05. Draw callbacks and model locks block shared GUI progress

**Confirmed dependency, conditional latency, P1.** `gui.c:280–313` holds its mutex across redraw and `canvas_commit`. `view.c:113` takes a model mutex indefinitely. `view_port.c` also locks around callbacks. Expensive draw callbacks, model writers, or display transfer therefore delay shared GUI work. Expensive work in a ViewHolder input callback executes directly in the GUI path; a ViewDispatcher moves event handling into the app loop but does not remove model contention.

Concrete instances are findings 15 and 26–27. **Options/tradeoff:** perform I/O/computation outside view locks; publish a prepared snapshot with a short lock. Snapshot lifetime and ownership must remain correct. **Verify:** measure GUI lock wait/hold time, model lock hold time, and each draw callback separately.

### 06. Shared timer callbacks can delay unrelated input, animations, and transitions

**Conditional risk, P1.** `furi/core/timer.c:27,59,156` uses FreeRTOS software timers and pending functions. Input repeat, animation ticks, menu scroll, popup timers, and RGB rainbow callbacks share this infrastructure. `lib/drivers/rgb_backlight.c:212–252` takes a mutex in its timer callback; saving settings holds the same mutex while persisting data (`:92–103`). That creates a concrete route for storage latency to delay the timer task when RGB is enabled. View-model locks and blocking event publication provide other routes.

**Options/tradeoff:** timer callbacks should post bounded work to a worker/app loop; avoid blocking locks and I/O in timer context. Event queues then need explicit backpressure rules. **Verify:** record scheduled versus actual callback timestamps during RGB setting saves, scrolling, and SD activity.

### 07. View changes and app exit wait for input release

**Confirmed mechanism, P2.** `view_holder.c:53–59` waits while `ongoing_input` is nonzero. `view_dispatcher.c:138–148` drains outstanding Press/Release state after the event loop stops. Holding a button can delay completion; a missing release can make the wait indefinite. `loader_menu.c:188–213` already defers certain switches to avoid a GUI callback deadlock; those deferred functions still share timer-task scheduling.

**Options/tradeoff:** keep release synchronization, or design explicit input handoff/reset on transition. Do not remove the wait without preventing input leakage to the next screen. **Verify:** keep Back/OK held while switching and closing, then repeat with remotely injected press/release sequences.

## Storage and browsing

### 08. SD and LCD share a bus; recovery directly freezes display traffic

**Confirmed mechanism, P1.** `targets/f7/furi_hal/furi_hal_spi_config.c:409–443` assigns display and both SD handles to `furi_hal_spi_bus_d`. `lib/u8g2/u8g2_glue.c:29–46` acquires that bus for display transfers. `furi_hal_sd.c:916–954` holds it through card initialization, including **250 + 100 ms sleeps on a power reset**, followed by initialization loops. Display commits cannot acquire the bus during that interval. Ordinary reads/writes also hold it through transactions and card readiness polling (`:843–889`).

**Options/tradeoff:** measure and bound transaction/recovery bus occupancy; consider smaller transfer batches or deferred recovery. Power-reset code deliberately grounds the shared bus, so blindly releasing its mutex during those sleeps is unsafe. **Verify:** trace bus ownership against LCD commits for healthy transfers and controlled card errors.

### 09. SD recovery has multi-second tails, not one global one-second timeout

**Confirmed mechanism, P1 on failing cards.** `storage/storages/storage_ext.c:30–94` allows ten mount attempts and sleeps 1,000 ms after each failed attempt. If all ten run, that is 10 seconds of explicit sleep alone, plus initialization/filesystem work. `furi_hal_sd.c:18,910–1039` has 1,000 ms low-level timeout constants, up to ten read/write recovery attempts, and up to 128 initialization attempts within `furi_hal_sd_init`. Several operations use scaled timeouts. These are nested budgets, not a proven total upper bound.

**Options/tradeoff:** decide acceptable interactive recovery time versus tolerance for marginal cards; make error/progress/cancellation visible; distinguish boot recovery from ordinary I/O. Shorter timeouts may reject cards that currently recover. **Verify:** record per-stage elapsed time and attempt count on controlled failures; do not infer normal-card speed from error-path constants.

### 10. One synchronous storage service serializes clients; opens can wait forever

**Confirmed dependency, conditional stalls, P1.** `storage.c:39,136–150` processes one message at a time through an eight-entry queue. `storage_external_api.c:15–26` makes callers wait for request completion. A slow operation delays other storage users regardless of their own work size. `storage_file_open` (`:90–115`) and the analogous directory-open path wait indefinitely for a close event after `FSE_ALREADY_OPEN`. A handle retained by another operation can look like frozen loading.

**Options/tradeoff:** avoid storage calls on interactive/model-locked paths; bound bulk-operation chunks; consider cancellable or nonwaiting opens where appropriate. A simple timeout on the existing synchronous API is not safe if queued messages still point to the caller's stack. **Verify:** simultaneous browsing/copying/settings saves and opening an already-held path; log queue wait separately from actual I/O.

### 11. Mount free-space work and idle-only polling can delay readiness

**Confirmed mechanism, P2.** `storage_ext.c:60–64` calls `f_getfree` on mount. Depending on FAT state this can require FAT scanning; the repository code alone does not establish the cost for a particular card. `storage.c:9,143–147` calls `storage_tick` only after a 1,000-tick queue receive times out. It is an **idle gap**, not a guaranteed once-per-second maintenance tick; continuous requests can postpone card-state maintenance.

**Options/tradeoff:** use a deadline-based maintenance schedule and consider deferring expensive free-space work. Changing free-space handling must preserve correct reporting and filesystem behavior. **Verify:** mount time with different valid FAT states and insertion/removal recognition during continuous storage requests.

### 12. Entering a folder counts it first, then enumerates it again

**Confirmed mechanism, P1 for large folders.** `gui/modules/file_browser_worker.c:181–238` scans every directory entry, including ones later filtered out. The worker then handles Load using another enumeration (`:243–360,479–491`). For <=220 accepted items it loads the full list; above 220 it uses chunks (`file_browser_worker.h:10`). The comment mentioning about 400 files is stale relative to that constant.

Archive and the general picker share this worker. A folder containing thousands of irrelevant files still costs a full count scan. **Options/tradeoff:** one-pass progressive enumeration, bounded caching, or deferred counts; each affects sorting/scrollbar/selection behavior and RAM. **Verify:** compare 50/220/221/1,000/5,000 entries and high rejection rates; measure first visible row as well as full completion.

### 13. Chunked browsing repeatedly rescans from directory start

**Confirmed mechanism, P1 for deep scrolling.** `file_browser_worker.c:243–308` reopens the directory and reads/filter-skips up to the requested offset before producing each chunk. Work grows with offset. Traversing many successive chunks can accumulate roughly quadratic directory-entry work in folder size for a fixed chunk size. That describes algorithmic work, not a measured latency curve; filesystem/cache behavior affects wall time.

**Options/tradeoff:** retain a directory cursor or bounded entry/checkpoint cache; invalidate it correctly when files change. Sorting all entries instead spends more RAM. **Verify:** compare equal-sized chunk loads near the start, middle, and end, in both directions.

### 14. Metadata and sort work add to directory latency; cancellation is coarse

**Confirmed mechanism, P2.** `archive_browser.c:437–471` opens FAP metadata and allocates icon data for each application item before adding it. `loader_menu.c:248–350` also loads metadata for custom absolute-path menu entries while building the menu. Directory listing time is therefore not just `readdir` time.

`archive_browser.c:74–88` and `file_browser.c:492–511` sort lists <=220 entries while holding the view model. Above the threshold sorting is skipped; increasing it trades memory/lock duration for ordering. `file_browser_worker.c` checks Stop only after pending scan/load branches; `file_browser_worker_free` joins the worker. A stop cannot interrupt an in-progress scan or stuck storage call.

**Options/tradeoff:** lazy/cache FAP names/icons, sort a prepared list outside the model lock, and check cancellation between entries. Cache invalidation must handle replacement FAPs and card removal. **Verify:** same-size folders of plain files versus FAPs; close a browser halfway through a large scan; record cancellation latency.

### 15. Archive context menu scans favorites while holding its view model

**Confirmed mechanism, P1.** `archive_browser.c:474–520` calls `archive_is_favorite` inside `with_view_model`. `archive_favorites.c:231–265` opens the favorites file and reads lines until a match/end. Opening a context menu thus depends on SD I/O and favorite position/count while the renderer may be waiting for that model lock. Contended file-open behavior from finding 10 can amplify this.

**Options/tradeoff:** cache a favorites set, or read outside the lock then publish the result if selection is still current. **Verify:** first/last/absent favorites with small and large lists, plus concurrent SD traffic.

### 16. Archive search scans the card and accumulates results

**Confirmed mechanism, P2.** `archive/scenes/archive_scene_search.c:35–94` recursively walks external storage, excluding asset-pack and animation directories, and adds each matching file to the browser. Matching FAPs also incur metadata work. It runs on a worker (`:109` onward), so it is not inherently a synchronous UI search, but it competes for storage, consumes memory as matches accumulate, and only checks cancellation between walk calls.

**Options/tradeoff:** index names, limit/paginate results, or reduce search scope; these change freshness/storage footprint or user experience. **Verify:** large trees, broad queries, many FAP matches, and cancellation during slow I/O. The recent DirWalk buffer reuse optimization does not eliminate traversal cost.

## Launch, boot, and visual work

### 17. FAP startup performs multiple stages before the app can respond

**Confirmed mechanism, P2.** `loader.c:536–626` preloads the manifest, handles asset-pack flags, preloads the application, maps/relocates it, validates compatibility, and allocates its thread. Supporting work is in `lib/flipper_application/elf/elf_file.c` and `flipper_application.c`. SD transfer, allocations, and relocation/symbol processing contribute separately. Manifest-first loading is purposeful: it discovers flags before full loading.

**Options/tradeoff:** instrument stages before selecting a cache or loader change; potentially cache metadata or improve read locality. Preloading/caching costs scarce RAM and must respect application replacement and API compatibility. **Verify:** cold/warm launches of small/large FAPs with equal asset state; do not blame the loading animation without timestamps.

### 18. New/changed FAP assets require signature checks, deletion, and extraction

**Confirmed mechanism, P2.** `lib/flipper_application/application_assets.c:196–356` compares signatures; on mismatch it removes the old asset directory and creates/copies the bundled directories/files before writing the signature. First launch after update can be much slower than a repeated launch. Signature equality skips extraction.

The current revision already compares the on-disk signature in 512-byte chunks. The embedded signature is still loaded into memory; do not report the previous whole on-disk signature allocation as an unfixed issue. **Options/tradeoff:** improve progress/cancellation, batch extraction appropriately, or stage assets during installation. Preserve integrity on interrupted updates. **Verify:** missing/equal/changed signatures and many-small-file versus few-large-file bundles.

### 19. Desktop animation and asset-pack loading creates transition costs

**Confirmed mechanism, P2.** `desktop/animations/animation_storage.c:112,222,294–377` reads manifests and external frame files, stats/opens/allocates/reads each frame, and caches a loaded animation. `animation_manager.c` replaces/frees animations and has explicit unload/resume paths. `lib/momentum/asset_packs.c:178–217` enumerates candidate icon/font replacements and loads enabled pack data. `loader.c:875–877` reloads packs after an app that unloaded them exits.

Frames are loaded and cached; this is **not** evidence of an SD read for every displayed frame. Transition/reload churn and persistent RAM usage are the concerns. **Options/tradeoff:** retain a bounded cache, preload likely next animations, reduce pack complexity, or offer a static desktop. Each exchanges RAM/battery/visual behavior for smoother transitions. **Verify:** built-in assets versus a large pack, app exit with/without unload flags, and repeated animation changes.

### 20. Boot waits for storage-dependent customization before starting other services

**Confirmed mechanism, P2.** `furi/flipper.c:169–225` starts storage first, waits for its record, then on normal boot with ready storage runs migration, namespoof initialization, settings load, and asset-pack initialization before starting the remaining services. `flipper_mount_callback` (`:129–155`) repeats relevant setup on card insertion, with a guard against double initialization on boot.

**Options/tradeoff:** measure each stage; defer nonessential customization with safe defaults if faster usable startup is desired. Some migration/settings ordering is intentional and affects service initialization. **Verify:** ready/absent/failing card, no pack/large pack, migration needed/already done, and time-to-first-accepted-input rather than just time-to-logo.

### 21. Every redraw sends a full framebuffer and runs synchronous callbacks

**Confirmed mechanism; magnitude unmeasured, P2.** `canvas.c:63–86` clears/rebuilds and sends the buffer, then calls canvas callbacks synchronously under its callback mutex. `gui.c:280–313` wraps this in the GUI lock. A 128x64 one-bit framebuffer is 1,024 bytes; with the configured 4 MHz display SPI (`furi_hal_spi_config.c:403–411`), pixel payload alone is about **2.048 ms** per full frame, excluding commands, CPU drawing, scheduling, callback time, and bus contention.

`canvas.c:294,317,462–476` decodes compressed bitmap/icon data during drawing. `menu.c` and `file_browser.c:575` also allocate temporary strings during draws. Existing decompressor reuse avoids a fresh decompressor allocation per icon; remaining formatting/decompression may still matter.

**Options/tradeoff:** reduce redundant redraws, reuse scratch strings where safe, cache expensive decoded assets, or investigate dirty-region updates. More caches cost RAM; partial refresh complicates orientation/overlap handling. **Verify:** frame CPU time versus bus wait versus transfer time; static menus and animated packs separately.

### 22. Animation and text cadence can feel choppy without slow event handling

**Confirmed mechanism, P3.** `menu.c:622–640` advances text scroll on a 333-tick timer. `desktop/animations/views/bubble_animation_view.c:188–199,383` schedules frames at `1000 / frame_rate`. These are local presentation cadences, not a universal GUI FPS cap. `gui.c:665–692` drains input/ASCII queues before drawing; sustained input can postpone drawing, and draw flag coalescing can skip intermediate visual states.

**Options/tradeoff:** adjust animation/text pacing independently of event latency; consider a draw fairness budget under sustained input. Faster redraws increase CPU/bus/battery use and do not fix storage stalls. **Verify:** event-to-model and model-to-display timing independently, including ASCII floods and held scrolling.

## Background work and deliberate application timing

### 23. Logging is synchronous and serialized

**Confirmed mechanism, conditional impact, P2.** `furi/core/log.c:103–176` locks global logging state, allocates/formats strings, and calls registered handlers synchronously. A slow handler delays both its originating thread and other logging threads. Default level is Info (`:9`); enabling Debug/Trace exposes more hot-path work. `cli_main_commands.c` explicitly warns of performance impact for those levels.

**Options/tradeoff:** keep normal logging levels, rate-limit hot-path logs, or use a bounded asynchronous buffer. Decide how dropped logs are counted; logging can be essential for diagnosis. **Verify:** identical workload at None/Info/Trace with and without an active sink, using a nonlogging measurement method.

### 24. Notification sequencing can delay callers and later notifications

**Confirmed mechanism, P2.** `notification_app.c:319,330,378` sleeps while executing sequences. `notification_app_api.c:24–48` blocking variants wait until completion; even nonblocking variants wait indefinitely to enqueue if the queue fills. Delays mostly affect the notification task unless a caller waits or queue backpressure propagates. A late vibration/backlight response can feel like slow input even if the UI handled it promptly.

**Options/tradeoff:** reserve blocking notification calls for sequences whose completion matters; coalesce redundant cosmetic requests where safe. Preserve order for hardware state changes. **Verify:** rapid repeated notifications and compare input/model timestamps with physical feedback.

### 25. Success popups and shutdown contain intentional dwell time

**Confirmed mechanism, P2 product choice.** Save/delete/write success scenes across NFC, Sub-GHz, LF RFID, and IR commonly configure **1,500 ms** popups (for example `applications/main/lfrfid/scenes/lfrfid_scene_save_success.c:14`). `gui/modules/popup.c:77–86` allows dismissal on Short when a callback exists; these are not necessarily mandatory 1.5-second input freezes. `desktop/scenes/desktop_scene_lock_menu.c:73,108` deliberately sleeps **500 ms** in Lock + Power Off paths, not ordinary keypad locking.

**Options/tradeoff:** shorter configurable success dwell or inline confirmation; retain enough feedback to communicate success. Review shutdown timing for its original hardware/product purpose. **Verify:** auto-dismiss and manual dismissal; do not infer a CPU bottleneck from a timed confirmation.

### 26. HID TikTok gestures sleep while holding the view model

**Confirmed mechanism, P2.** `applications/system/hid_app/views/hid_tiktok.c:170–224` performs double-tap timing (25 + 75 + 25 = **125 ms**) and a 50 ms pause gesture inside `with_view_model`. Cursor-reset helpers also sleep (`:117–124`). This delays input handler completion and can block drawing on the model mutex. Host transport/device scheduling can add latency.

**Options/tradeoff:** run gesture sequencing in a worker/state machine and publish visual state separately, preserving host-required gesture timing. Removing delays changes gesture meaning. **Verify:** rapid gesture inputs, Back/cancel during a gesture, and host recognition success.

### 27. An external game sleeps for one second under its drawing mutex

**Confirmed mechanism, P2 app-specific.** `applications/external/4inrow/4inrow.c:264–277` acquires the game-state mutex and on a win sleeps **1,000 ms** before resetting the board. This blocks the game's event loop; the draw callback uses the same mutex. It is a concrete example of an external app making the shared GUI appear stalled.

**Options/tradeoff:** represent the win-display period as timed state rather than sleeping with the lock held. Decide whether navigation/exit should remain possible during that period. **Verify:** win a round, immediately press Back, and observe screen/input progress through the dwell.

### 28. Peripheral/protocol workflows contain polling, settling, and presentation delays

**Confirmed examples; broader inventory untriaged, P2.** `applications/external/flip_weather/app.c:28–34` can wait nine 100 ms intervals for PONG before starting the dispatcher: up to **900 ms explicit wait** on that branch, plus other initialization. HID setup paths (`applications/system/hid_app/hid.c:32,321,355`; `applications/main/bad_usb/helpers/bad_usb_hid.c`) contain 200 ms waits. `subghz_scene_decode_raw.c:156` has a 100 ms delay; Sub-GHz and Infrared dispatcher ticks are 100 ticks (`subghz.c:117`; `infrared_app.c:176`). Only tick-driven work is subject to that cadence; queued input need not wait for a tick.

The frequency analyzer worker has 10 ms outer-loop and 2 ms settling waits (`subghz_frequency_analyzer_worker.c:106,138,183`). RF/NFC/LF/IR transaction duration, scan breadth, retries, and external hardware response can dominate time-to-result independently of UI speed. CLI loop sleeps often just pace output or cancellation polling and are not home-screen delays.

**Options/tradeoff:** distinguish required protocol pacing from avoidable UI blocking; offer asynchronous progress/cancel, selective scan scope, or faster presentation where appropriate. Protocol delay reductions require correctness testing with actual devices. **Verify:** measure time-to-first-feedback, time-to-first-result, total operation time, and cancellation separately. See the inventory for other apps; no claim is made that every matching delay is harmful.

## Lower-confidence overhead and conditional correctness hazards

### 29. Allocation churn and memory pressure can add jitter

**Measurement candidate, P3.** Menus/browser draws, metadata, logging, animation replacement, and FAP loading allocate strings/arrays/buffers. `lib/FreeRTOS-Kernel/portable/MemMang/heap_4.c:146–193` searches free blocks while the scheduler is suspended. Fragmentation can increase allocation search work and prevent a large contiguous allocation despite free memory. This is not paging: there is no evidence here of a swap-based slowdown, and low memory may produce failures instead of gradual slowness.

Already addressed in recent commits: immutable data moved into flash; DirWalk reuses filename scratch; Sub-GHz receiver reuses a model scratch string; EMV tracing avoids some temporary allocation; asset signature comparison streams disk data. Do not count those old patterns as current defects.

**Options/tradeoff:** measure allocation rates, minimum free heap, largest free block, and peak live assets before adding more caches. Reuse only where lifetimes/locking are clear. **Verify:** long launch/browse/exit cycles with the same pack and compare first-run versus later latency distributions.

### 30. Build flags prioritize size; disabling COMPACT does not select speed optimization

**Confirmed configuration, unmeasured effect, P3.** `fbt_options.py:13–16` defaults to `COMPACT=1`, `DEBUG=0`. `site_scons/firmwareopts.scons:4–32` chooses `-Os`; with COMPACT disabled, base firmware uses `-Og`, not `-O2`. Actual command-line/build overrides still need inspection on the flashed binary.

**Options/tradeoff:** benchmark selected hot functions under speed-oriented optimization instead of assuming a global flag change helps. Code growth can exceed flash budgets; optimization can affect timing-sensitive code. **Verify:** compare actual compile commands, image size, and the same hardware workloads. No claim is made that the present size-optimized build is the main bottleneck.

### 31. RGB updates and remote screen streaming have separate costs

**Confirmed mechanisms, unmeasured impact, P3.** `lib/drivers/sk6805.c:60–103` bit-bangs LEDs inside a critical section using cycle-counter waits. It adds brief interrupt latency when enabled; there is no measurement showing it explains human-scale freezes. The much larger RGB risk is blocking the timer task through its mutex (finding 06).

`rpc/rpc_gui.c:86–133` copies the framebuffer and obtains optional RGB color state in the synchronous canvas callback. The transmit worker (`:135–158`) sends separately, then adds `transmit_time / 20` ticks capped at 500. Thus remote-screen delay is not automatically on-device UI delay; however the callback and RGB lock are on the local drawing path.

**Options/tradeoff:** cache color state, measure critical-section length, and compare physical screen versus streamed view. Faster remote transmission trades bandwidth and CPU. **Verify:** USB/BLE screen streaming separately, RGB off/on, and busy settings saves.

### 32. Duplicate log-handler registration has a conditional infinite loop

**Confirmed control-flow defect; trigger not observed, conditional priority.** In `furi/core/log.c:41–66`, `furi_log_add_handler` sets `ret = false` when `memcmp` finds an identical registered handler, but advances the iterator only in the `else` branch. An exact match therefore repeats forever while holding the global logging mutex. This is a possible hang, not ordinary low FPS. Normal registration paths were located in CLI logging and serial control; this audit did not establish that they register an identical handler twice in normal use.

**Options/tradeoff:** address separately as a bounded correctness fix (break or advance on duplicate), with a targeted duplicate-registration test and preserved return semantics. **Verify:** register the same initialized handler twice in an appropriate test environment, then confirm logging still works. No production fix was made as part of this audit.

## What not to conclude from the scan

- An empty-queue `FuriWaitForever` usually lets a task sleep efficiently. It is not itself input latency.
- `furi_delay_ms` yields its task; it becomes a shared UI problem when that task owns a needed lock/bus, processes input, or provides a service other tasks await.
- Timeout constants are usually error-path ceilings, not delays on every successful operation. Nested recovery makes some total failure times longer than one constant.
- A worker thread keeps computation off the event loop but can still contend for storage, CPU, heap, buses, or model locks.
- A 100 ms app tick is not automatically a 100 ms minimum input latency; the dispatcher is event-driven.
- Installed FAP count does not mean all apps execute in the background. Large directories/manifests/assets are the relevant storage costs.
- Raising all priorities, shortening all timeouts, removing protocol sleeps, or increasing every queue would not be evidence-based fixes.

## Measurement plan before choosing implementations

Use a release build matching the audited source. Record card model/filesystem/occupancy, active asset pack, haptic settings, logging level, RGB state, connected transports/peripherals, and app versions. Use the same files and settings across comparisons. The hardware and flashed configuration were not available during this audit.

For each interactive scenario, record at least 30 repetitions when practical, report median/p95/max, and distinguish cold versus warm runs. Keep first-launch asset extraction and failed-card recovery in separate groups. A few averages can hide the long stalls people notice most.

Instrument with low-overhead cycle timestamps or a bounded in-memory trace, or GPIO markers plus a logic analyzer. Avoid synchronous log output in timed paths. Suggested stages:

1. Physical key edge -> debounced event -> pubsub return -> GUI dequeue -> app dequeue -> handler completion -> draw start -> LCD transfer completion. Capture both Press and Short.
2. Queue depths/put wait time, model/GUI mutex hold and wait times, software-timer lateness, and SD/display bus ownership duration.
3. Storage queue wait -> operation start -> operation completion, with bytes/entries and retry counts.
4. FAP manifest/preload/map/assets/thread-start/first-frame; app shutdown and desktop reload separately.
5. Boot storage/migration/settings/pack/services/first accepted input.

| Scenario | Controlled variants | Resolves |
| --- | --- | --- |
| Menu taps and holds | Haptics 0/9; press/release/both | 01–03 baseline and haptic cost |
| Input during slow operation | Local buttons and remote ASCII; observe queue depths | 04–07 stall propagation |
| SD activity while animating | Healthy sequential/random I/O; controlled error recovery | 08–11 bus and service contention |
| Folder browse | 50/220/221/1,000/5,000 entries; many rejected files; plain files/FAPs | 12–14 scaling and threshold behavior |
| Context menu and search | Many favorites; broad query; cancel during scan | 15–16 lock and cancellation latency |
| Launch/exit | Small/large FAP; assets equal/missing/changed; pack on/off | 17–19 true launch stages |
| Boot/card insertion | No card/healthy card/failing card; migration/no migration | 20 initialization ordering |
| Visual/background load | Static/animated pack; logs None/Info/Trace; RGB and RPC off/on | 21–24,29–31 overhead |
| Workflow-specific pauses | Dismiss popup; Lock + Off; HID gesture; game win; missing PONG | 25–28 intentional timing versus blocking |

There is no universal target imposed here. Reasonable decisions should specify acceptable tap-to-feedback, hold-repeat behavior, launch time, cancellation time, and error recovery behavior for the product. Optimize tail latency and responsiveness during work as well as total throughput.

## Choices available without committing to a redesign

1. **Keep behavior and measure first.** Instrument the shared path and reproduce the visible symptoms. Lowest behavior risk; produces defensible priorities.
2. **Change interaction feel.** Press-based directional navigation, optional accelerated repeat, and shorter success confirmations. Potentially immediate perceived improvement, but semantics change even on an otherwise fast device.
3. **Remove avoidable UI blocking.** Move haptic timing, favorites I/O, gesture sequencing, and long model-locked work off interactive paths. Preserve intended timing while improving responsiveness; requires lifecycle/cancellation work.
4. **Reduce storage work.** Cache names/icons/favorites; retain directory progress; improve loading feedback. Particularly relevant for large SD collections, with RAM and invalidation costs.
5. **Improve recovery and fairness.** Bound queue/bus work and schedule storage maintenance by deadlines. Relevant for worst-case stalls; requires careful synchronization and peripheral testing.
6. **Optimize rendering/CPU only after profiling.** Redraw coalescing, scratch reuse, decoded caches, or selective compiler optimization. Likely less valuable than fixing a demonstrated multi-hundred-millisecond blocking path.

These are independent choices, not an approved implementation plan. The source and settings remain unchanged; the audit, inventory, and inventory generator are the only additions.

# NVK Performance Monitor — local feature

**Status: implementation under local build/review validation; hardware unverified.**

This feature is isolated in `switch/performance-monitor`, worktree
`/home/gro/dusklight-deko3d/performance-monitor`, from NVK game source
`2258b9a194d8477442ede0723b1f04ebdf09c6a3`. It does not merge into or modify
`switch/nvk-reference` or `switch/deko3d`. Their existing evidence and original
NROs remain authoritative. The original hardware-smoke-tested NRO is
`../snapshots/nvk-reference-00b7271-shallow/build/switch-nvk-upstream-0ff5207-release/platforms/switch/dusklight.nro`,
SHA-256 `c1758fd9d5d66c98eb211b9f4b0c0814aea9d826eb956a5ea3cb9d9d83e2611d`.

## Evidence classes and preserved observations

- **Source-code evidence:** additive Aurora hook, UI/configuration integration,
  bounded transport/capture, statistics and CSV implementation; exact dependency
  commit/diff are recorded under `docs/switch/evidence/` after validation.
- **Static analysis:** independent UI/concurrency audits and code reviews;
  original `calculate_fps`, `after_present`, and `aurora_get_fps` preservation check.
- **Host tests:** synthetic timestamps and real production queue/drain/statistics/
  CSV code, sanitizers, existing Aurora I/O/time/thread/render-worker tests,
  package validator exercised against the actual preserved baseline NRO.
- **Cross-build/static artifact inspection:** separate Release AArch64 NVK build;
  ELF/link, NRO/ASET, NACP, ROMFS inventory/content, SDK receipt and hashes.
- **Real-device observation (original NRO only):** prior user-reported startup,
  gameplay smoke pass and almost-instant repeat pipeline initialization. The
  mission additionally reports three docked Faron Woods runs facing the
  waterfall, typical observed FPS 30, lowest 29, no visible problems. These are
  uninstrumented user observations, not frame-time measurements, calibrated
  clock evidence, or repeatable hardware proof.
- **Emulator test / instrumented-device observation / repeatable hardware
  result:** none. No instrumented NRO deployment or device contact is authorized.

## UI and compatibility

Video → Display contains Performance Monitor **Off / Basic / Detailed**, corner,
manual Benchmark Scene Label, Start Benchmark, Stop Benchmark and Export Results.
Start is disabled in prelaunch settings. Capture/results survive closing settings.
The right-hand help pane shows capture status or completed summary metrics.

Off hides the panel and unregisters the optional callback when no explicit
benchmark is active. The remaining presentation-path cost is an atomic
callback-presence check and branch; **zero overhead has not been demonstrated**.
Basic retains the existing `aurora_get_fps()` calculation and rounded `N FPS`
label, with no continuous interval collection. Legacy `game.enableFpsOverlay`
and `game.fpsOverlayCorner` identifiers are preserved; a legacy enabled counter
maps to Basic. Explicit mode selection clears the legacy enable flag.

Detailed refreshes its passive panel twice per second, independently of capture:
existing FPS; latest presentation interval; median/P95/P99 over up to 256 recent
valid intervals; draw counts labeled **worker sample**, pipeline counters labeled
**async**; GPU time **Unavailable**. Invalid/missing/stale recent observations
produce Unavailable instead of fabricated zero values. Selecting a corner does
not enable the monitor. Scene labels are manual, not location detection; the UI
supplies Unspecified/Faron Woods presets and JSON accepts a custom
`video.benchmarkSceneLabel` (export limited to 160 bytes).

## What is measured

Aurora's dedicated dependency change derives from Switch-specific
`6d9f9d9fe8952aada5274154645610042d0a036e`, not a wholesale upstream update.
The local commits are sync bookkeeping fix
`0574751289b80838079b2223eb3dfe5f2f93fb82` and observer
`bc112ad008ecbbb0a4934283c637ad945747667c`; the exact combined diff is
`evidence/performance-monitor-aurora.patch`. They have not been pushed.
The observer runs in the successful `Surface::Present()` branch, after the
existing surface lock scope and before SaltyNX/pacing work. It uses
`std::chrono::steady_clock` converted to integer nanoseconds.

This is a **host-observed successful-present-return interval**. It is neither a
physical scanout-completion interval nor GPU render time. Failed presents do not
emit events. The observer's monotonic presentation ID is not a render-packet ID.
Draw counters are copied on their owning worker but are deliberately not claimed
to identify a particular frame. Pipeline counters use independent matching atomic
loads and are not frame-correlated. CPU timing, GPU timing/calibration, verified
CPU/GPU/memory clocks, and GPU utilization are unsupported future work. Tracy's
optional GPU diagnostics are not used or promoted to calibrated measurements.

The original FPS and pacing timestamp/EMA/deque code is unchanged. No graphics
settings, cap, VSync, resolution, clocks, renderer, Mesa/NVK/Dawn/deko3d/UAM code
are changed by starting/stopping capture.

## Capture, boundaries and statistics

Default request window: **60 seconds** from the monotonic Start request.
Only whole intervals whose two presentation endpoints lie in that closed request
window are retained. The first eligible event establishes a baseline and is not
an interval. An endpoint beyond the deadline ends capture **before** counting or
appending that crossing interval. No clipping/interpolation is performed.
Automatic completion waits for a presentation at/after the deadline, so delayed
UI consumption gives the same eligible population as regular consumption. If
presentation stops, completion can remain pending; Stop is available. Menus,
startup-to-scene transitions and pauses within the window are not silently removed.
A long hitch crossing the boundary is censored; compare requested duration with
the actual first-to-last retained presentation window and do not treat the
sequence-derived FPS as a full-window count divided by 60.

Raw storage is fixed at 8192 intervals, chronological, no overwriting. Capacity
ends capture with explicit truncation. The transport queue holds 8192 events;
full transport discards the new event, counts loss and marks a relevant capture
truncated/invalid. Loss accounting is keyed to the capture epoch and only lost
endpoints inside its request window. Accounting survives completion during drain;
losses entirely after the deadline do not invalidate that completed window.
Counts freeze at the completion snapshot. Later producer losses after an early
capacity stop are not retroactively added; that capture is already truncated.

Zero/duplicate/backwards timestamps and event-ID discontinuities are flagged.
Missing events break the interval chain; never join across a gap. An interrupted
capture is explicitly truncated. Empty/single-event captures export counts and
Unavailable statistics. Sample count and presentation-event count are separate.

Mean = sum of valid intervals / sample count. Median = middle sorted value or
arithmetic mean of the middle pair. P95/P99 use **nearest rank**:
`sorted[ceil(p*N)-1]`, not interpolation. Minimum/maximum preserve observed hitches.
Sequence average FPS = `valid_interval_count * 1e9 / sum(valid_interval_ns)`;
never derive percentiles from averaged FPS. Requested duration, observed
presentation-window duration and valid interval sum are separate export fields.

## Ownership and bounded work

Aurora's serialized presentation producer copies events to fixed SPSC slots.
Release/acquire index transfer gives exclusive slot ownership; unread slots are
never overwritten. Callback registration and stream bounds are atomic, and the
callback target is process-lifetime storage; an in-flight unregister cannot free
its target. Epoch filtering rejects stale events across start/restart. Capture-
window loss counters are producer-owned plus atomic publication for the current
consumer-controlled epoch. Target atomic types are compile-time lock-free checked.

The game/UI thread owns capture, recent statistics, metadata, finalization and
export. Each drain processes a bounded prefix, not an unbounded producer race.
No new producer allocation, logging, sorting, mutex acquisition, wait, file read
or write occurs. Metadata/associated-manifest reads finish before capture starts.
Sorts and panel formatting happen on the UI thread; sorting the full capture and
CSV file I/O happen after capture ends. Export can still stall the game/UI thread;
its overhead has not been measured on hardware.

An existing Aurora worker test exposed sync acknowledgement before pending-item
bookkeeping. The unmodified worker failed 450 of 10,000 stress repetitions.
The dedicated dependency fix moves the existing decrement before the existing
acknowledgement (no added locking/waits, no render-command reordering); the same
10,000 repetitions then passed. The original failure and logs are retained.

## Export contract and identity

Exports go to **`dusk::ConfigPath / "benchmarks"`**, the active established data
root, not a hard-coded or restart-pending path. Writes first use an exclusive
`.partial` file (`O_CREAT|O_EXCL`). Only after every write AND close succeed does
the exporter atomically reserve a new uniquely named capture directory and rename
the closed partial to its never-before-existing `results.csv`. No prior benchmark
is overwritten; no hardlinks or Linux-only rename flags are required. Ownership
of this new directory belongs exclusively to the single UI exporter. Final paths
are `benchmarks/benchmark-<build-id>-<unique-suffix>/results.csv`.

Failures preserve `.partial` diagnostics, even if a close failure happened after
all payload/footer bytes were written. Consumers must reject `.partial` files
regardless of their footer, and require the final `metadata,export_complete,true`
row in a published `results.csv`. Short-write/write-error/close-error injection
tests exercise this real path. There is no per-frame I/O or automatic export.

CSV is a six-column table: `record_type,key,value,presentation_id,
presentation_timestamp_ns,presentation_interval_ns`. Metadata/summary rows use
key/value; chronological sample rows contain exact integer timestamps/intervals.
Commas/quotes/newlines in labels are CSV-escaped; numeric formatting uses the
classic locale. Assets, shader contents, user paths and save data are not exported.

Metadata includes build ID, exact source/dependency revisions and dirty status,
compiler/image, backend, wall-clock start (explicitly uncalibrated), monotonic
start/window/duration, start/end reported GX/output framebuffer sizes and their
configuration-report provenance, configured cap/VSync/interpolation/resolution
scale, display mode when available, manual scene label, changed-condition flag,
counts, validity/truncation/reason, algorithm and capacity. Unsupported clocks and
timing are Unavailable. The module does not set clocks or graphics parameters.

**No in-process NRO hash is fabricated.** Every CMake configuration rotates its
compiled ID; this ID is not an artifact hash. The host verifier requires CURRENT
clean game/Aurora sources (including untracked files), independently repackages
the verified ELF with the same assets using real `elf2nro`, and requires complete
byte equality with the NRO. Only then does it write the associated
`performance-monitor-build.json` containing the actual SHA-256 and seal the build
configuration. Both CMake reconfiguration and NRO packaging reject that sealed
directory; another distributed artifact needs a fresh build directory/identity.

If later authorized for installation, place the matching sidecar in the active
data root. Runtime checks ID and both source revisions, but cannot authenticate
the external file or verify running-NRO bytes. The CSV explicitly says
**externally supplied identity-matched manifest; running NRO bytes not verified**.
Missing/mismatched files yield **NRO SHA-256: Unavailable**. This is not on-device
hashing, a signed attestation or hardware validation.

## Local commands and preserved outputs

All work is inside the approved workspace. Use the preserved toolchain image
`dusklight-mesa-upstream:0ff5207-toolchain`, immutable ID
`sha256:6611f256a5f9c97ff9b934c7a7c0ec26552aee0fe94d4cd59d849def637e376c`.
Run containers as the workspace owner's UID/GID; do not change global Git trust
settings to work around root-container ownership checks. Mount the workspace at
its existing absolute path; `HOME`/`TMPDIR` are workspace-local build directories.

From the feature worktree, inside that container:

```sh
cmake -S tests/performance-monitor -B build/performance-monitor-host-cmake -G Ninja \
  -DSDL_SOURCE=/home/gro/dusklight-deko3d/dependencies/SDL \
  -DGTEST_SOURCE=/home/gro/dusklight-deko3d/dependencies/googletest \
  -DTRACY_SOURCE=/home/gro/dusklight-deko3d/performance-monitor/build/switch-nvk-performance-monitor-release/_deps/tracy-src
cmake --build build/performance-monitor-host-cmake --parallel 4
ctest --test-dir build/performance-monitor-host-cmake --output-on-failure
python3 tests/test_performance_artifact.py -v

cmake --preset switch-devkitA64-release \
  -B build/switch-nvk-performance-monitor-release -UCMAKE_EXE_LINKER_FLAGS \
  -DNXVK_USE_UPSTREAM_SDK=ON \
  -DNXVK_ROOT=/home/gro/dusklight-deko3d/builds/mesa-upstream-0ff5207/output/sdk \
  -DDUSK_PERFORMANCE_TOOLCHAIN_ID=sha256:6611f256a5f9c97ff9b934c7a7c0ec26552aee0fe94d4cd59d849def637e376c
cmake --build build/switch-nvk-performance-monitor-release \
  --target dusklight_nro --parallel 4 --verbose
```

SDL/gtest identities are the already recorded workspace pins; native tests disable
GPU/audio/video/joystick/haptic backends. Their test-only temporary directories are
isolated and I/O fixtures serialized. New tests exercise real production code,
including delayed/regular UI-drain schedules, post-deadline hitches, and capacity
completion with already-observed transport loss. Sanitizer and stress logs,
including negative evidence, are in `../logs/performance-monitor/`.

Generated files stay under the separate feature `build/` and are not committed.
The packaged output name is `platforms/switch/dusklight-performance-monitor.nro`.
`platforms/switch/performance-monitor/verify_artifact.py --help` lists its required
host-verification inputs. It refuses dirty configured sources, mismatched source
HEADs/image, broken ELF/public symbols/package, nonmatching ROMFS resource content
or changed SDK archives; it requires independent ELF→NRO regeneration equality
and creates an exclusive new local delivery directory. Pass both `--nm` and
`--elf2nro` from the preserved toolchain. A successful verification seals its
build directory with `.performance-monitor-packaged.json`.

## Bounded future hardware validation — approval required

Do not deploy or execute this feature automatically. After explicit approval:

1. Verify the exact original/instrumented artifact hashes and matching sidecar;
   preserve the original NRO under its original name. Record device/software,
   docked mode, warm-cache conditions, configured settings, resolution and clocks
   only when independently verified (otherwise Unavailable).
2. Use Faron Woods facing the waterfall, docked, with equivalent save/camera,
   resolution/cap/VSync/clocks and warm-cache state. Record individual runs, not
   a pooled recollection. Do not compare against native-deko3d yet.
3. Run three bounded 60-second trials per condition: preserved original NRO,
   instrumented NRO Off/no capture, instrumented NRO with capture. Separate
   menu/scene-return transition time in the protocol rather than deleting raw
   samples. Retain every exported CSV and all failures.
4. Check startup/gameplay/render/audio/input/menu/exit correctness, mode/corner
   persistence and legacy Basic FPS display. Check start/early stop/restart,
   closed-menu auto completion, CSV paths, unique filenames and matching hash
   association. Device-read back exported CSV and validate its final completion
   row, metadata, ordering, counts and summary against the raw sequence on host.
5. Compare correctness and possible instrumentation overhead before treating
   these captures as a baseline for future NVK/deko3d comparisons. No-overhead,
   pacing improvement, clock calibration, physical scanout timing and GPU timing
   require additional evidence; a host test cannot establish them.

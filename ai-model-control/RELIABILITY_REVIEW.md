# AI model control reliability review

Review date: 2026-09-12. Baseline: `94044d1`. Changes are on
`codex/model-dial-reliability` and cover the Mac bridge and menu-bar app.
Firmware source and VERSION remain unchanged under the v0.90 release freeze.

## Fixed on this branch

| Priority | Finding and effect | Change |
| --- | --- | --- |
| P1 | Serial reconnects retained accepted revisions, pending targets and applied-value caches. A fresh snapshot with unchanged revisions could be ignored, or a disconnected target could continue posting keys. | `Sources/Serial.swift` exposes connection generations and discards frames collected before a read failure. `Sources/BridgeRuntime.swift` clears pending work and caches on read/write connection changes and clears applied values after a new boot announcement. |
| P1 | The app selected the first sorted serial path on every poll. Adding/removing another ESP32 could redirect the bridge. | `AppSources/DialController.swift` retains a selected path. The Device menu provides an explicit choice when multiple candidates exist and keeps a missing selection instead of choosing another board. |
| P1 | Stop cleared the child reference before exit; settings changes could launch overlapping helpers competing for exclusive serial ownership. | Keep ownership until the termination callback, serialize restarts, use a one-second forced-stop fallback, and ignore old-child output. Quitting terminates the owned child even when the app run loop is ending. |
| P2 | ChatGPT/OpenCode failures and input-filter creation failures could retry indefinitely, repeatedly interrupting the user. | One three-attempt budget per target covers all apps; new dial input or Sync resets it. |
| P2 | Launch success was displayed as a connection; status and permission errors were not shown in the menu. | Display connection progress, received panel state, apply failure, and permission guidance. Native checked menu controls support keyboard navigation. Surface login registration errors and approval requirements. |
| P2 | Sync waited for SQLite on the main thread and exported the entire persistent application-state value to a temporary file. | Read only the catalog and overrides off the main thread, bound the child to three seconds, restrict temporary-file permissions, and retain saved preferences on failure or a concurrent edit. |
| P2 | Pipe callbacks treated arbitrary chunks as whole UTF-8 messages and retained callbacks at EOF; the persistent log grew without limit. | Assemble bounded complete lines, remove EOF callbacks, rotate at 2 MiB with one previous file, and correct shell quoting for Open Log. |
| P2 | ChatGPT picker geometry was force-cast and used without validating AX value types or dimensions. | Validate types, successful extraction, finite coordinates, and positive dimensions before clicking. |

Paths in this table are relative to `mac-chatgpt-bridge/`. Process supervision,
Cursor catalog reading, and output framing now have separate focused files.

## Remaining findings

### P1: configuration changes can leave the wire snapshot stale

In `main/main.c`, the `serial_sync_take_config_changed()` branch clamps the
model/effort, saves it and repaints, but never calls `serial_sync_update()`.
`serial_sync_poll()` can therefore send the preceding cached value after a
Settings mask removes the selected model or effort. If the foreground app does
not change, this discrepancy can persist until another local input updates the
snapshot. A future firmware change should publish the final clamped fields
before polling transmissions, including the configuration-only path.

### P2: calibration can suspend the main loop indefinitely

`main/calibrate.c:wait_release()` has no timeout. `sample_point()` starts its
60-second timeout only after that wait returns. A stuck touch signal can keep
`calibrate_run()` active indefinitely while the main loop stops handling serial
and normal UI work. Raw read failures also count as release in this calibration
path, bypassing the normal touch release debounce. Make calibration cooperative,
allow bounded cancellation, and distinguish a read failure from a confirmed
release when the firmware freeze is lifted.

### P2: saved effort records need stronger structural validation

`main/model_nvs.c:model_nvs_load()` accepts an effort blob when its version and
count match and its length is at least two bytes. It does not validate the
length against the declared record count or ensure each stored string is
terminated. Later `find_slot()` and restore paths use C string operations on
those records. Validate record lengths, app IDs, and bounded string contents
before making persisted records available to the catalog.

### Follow-up requiring live app evidence

Accessibility searches in `Sources/ChatGPTPicker.swift`, `Sources/CursorPicker.swift`
and `Sources/PromptFocus.swift` have node limits, but no overall traversal
time budget. Per-call timeouts can accumulate while traversing an unresponsive
app. The independent InputGuard watchdog releases user input, but it cannot
make a stalled search return immediately. Add cooperative traversal deadlines
and inspect focused-window behavior with the actual apps before changing their
picker/focus recipes.

ChatGPT effort posting uses the enabled dial-list index as its shortcut offset.
The bridge's Settings mask is sent only to the panel. Verify the native app's
shortcut order when intermediate efforts are disabled; if the app retains the
full order, the shortcut offset and clamp must use that full native order.

## Validation and limits

Both `chatgpt-bridge` and `model-dial` were compiled using `swift build -c release`
on the Mac. The final diff is checked for whitespace errors. No tests were
created, modified or run, per repository instructions.

No running app was replaced, no hardware was flashed, and no live serial or
Accessibility interaction was exercised. The compiled binaries do not update
the installed app or the previously packaged app under `dist/`. Firmware findings
above are source-review findings, not newly reproduced hardware symptoms.

The Device menu remembers a path, not a verified physical board identity. A
path reused by another USB device still needs a user-selected correction;
protocol-level device identification is a separate future improvement.

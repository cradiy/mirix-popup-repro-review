# Input-method popup placement becomes stale after its parent client exits

## Summary

On Niri 26.04, a persistent `zwp_input_popup_surface_v2` can become incorrectly positioned after its focused text-input client exits and another client takes focus. The popup appears below the cursor and is clipped by the output edge, despite enough space above. Re-sending the identical text-input cursor rectangle after the popup is visible corrects its position.

A minimal shared-memory input method and a minimal text-input client reproduce this placement issue.

## Tested environment

- Fedora, installed Niri package `niri-26.04-1.fc44.x86_64`.
- Installed Niri executable run as a nested Wayland compositor, without `--session`.
- Nested output: 1244 × 1481 physical pixels, scale 1.5, logical 829 × 987; reported transform `Flipped180`.
- Niri v26.04 Cargo.lock pins Smithay `ff5fa7df392cecfba049ffed55cdaa4e98a8e7ef`.
- This has not been tested against Smithay/Niri main, other compositors, or other input methods.

## Minimal participants

- `src/probe.c`: xdg-toplevel + text-input-v3 client. Places its cursor near the window bottom. Preedit changes do not change the cursor rectangle. F6 or a mouse click sends the exact same rectangle again.
- `src/ime.c`: input-method-v2 client. Creates one persistent input-popup surface, shows a 411 × 65 SHM buffer 500 ms after activation, and keeps submitting buffers every 100 ms. Detaches its buffer on deactivation but retains the popup object.
- Automatic cursor replay and automatic popup hide/remap are disabled for this experiment.
- The input client is terminated with SIGTERM without a graceful protocol teardown. Thus the verified trigger is client exit/disconnect, not every possible window-close sequence.
- `src/keys.c` is only an automation helper for F6. It does not drive the input engine.

## Reproduction

1. Start a fresh nested Niri and the minimal input method.
2. Open the minimal text-input client. The input method automatically sends preedit `s` and shows its popup.
3. Observe the popup above the cursor, fully visible.
4. Terminate the text-input client while keeping the input method and popup surface alive.
5. Open a new instance of the text-input client. Observe the popup below the cursor, clipped at the output bottom.
6. Press F6 in the client. This only repeats `set_cursor_rectangle(32, height - 44, 2, 24)` followed by `text_input.commit`; it does not resize the window, change the rectangle, or recreate the popup.
7. Observe that placement returns above the cursor. Repeat steps 4–6.

The popup continues receiving buffer commits before F6, so continued drawing alone does not correct this instance.

## Observed results

Pixel intervals below are physical screenshot rows occupied by the teal popup.

| Client instance | Before identical-rectangle replay | After replay |
| --- | --- | --- |
| First, fresh compositor | 1305–1402, fully visible | 1305–1402 |
| Second, after previous client exits | 1439–1480, clipped | 1305–1402 |
| Third | 1439–1480, clipped | 1305–1402 |
| Fourth | 1439–1480, clipped | 1305–1402 |

Raw results: `evidence/results.jsonl`. Screenshots: `0-before.png` (initial correct), `1-before.png` (incorrect), `1-after.png` (recovered). The experiment disables automatic replay with `MIRIX_PROBE_NO_REPLAY=1`.


## Source evidence and proposed explanation

The following is a source-level explanation consistent with the reproduction, not a traced or patch-validated proof of every internal step.

1. Smithay activation dismisses the old popup clone, changes the input-method handle's parent, and registers a new clone. Deactivation also dismisses before clearing its parent. See [input_method_handle.rs, lines 125–162](https://github.com/Smithay/smithay/blob/ff5fa7df392cecfba049ffed55cdaa4e98a8e7ef/src/wayland/input_method/input_method_handle.rs#L125-L162).
2. Niri delegates dismissal to `PopupManager::dismiss_popup` and ignores its result. See [Niri handlers/mod.rs, lines 249–253](https://github.com/niri-wm/niri/blob/v26.04/src/handlers/mod.rs#L249-L253).
3. Smithay `PopupManager::dismiss_popup` immediately returns `DeadResource` when the parent surface is no longer alive. `find_popup` searches registered trees and returns the first matching popup surface. See [manager.rs, lines 154–194](https://github.com/Smithay/smithay/blob/ff5fa7df392cecfba049ffed55cdaa4e98a8e7ef/src/desktop/wayland/popup/manager.rs#L154-L194).
4. `PopupSurface` stores its parent by value, while rectangle and location are shared. Its `alive()` checks popup surface/role liveness, not parent liveness. Tree cleanup uses this popup liveness. A clone associated with the dead parent can therefore remain eligible for lookup. See [input_method_popup_surface.rs](https://github.com/Smithay/smithay/blob/ff5fa7df392cecfba049ffed55cdaa4e98a8e7ef/src/wayland/input_method/input_method_popup_surface.rs#L20-L80) and [manager.rs cleanup](https://github.com/Smithay/smithay/blob/ff5fa7df392cecfba049ffed55cdaa4e98a8e7ef/src/desktop/wayland/popup/manager.rs#L428-L450).
5. Niri's surface-commit path obtains the popup through `find_popup`, then resolves its parent to position it. A stale dead parent cannot resolve to a mapped window. See [xdg_shell.rs, lines 1201–1259](https://github.com/niri-wm/niri/blob/v26.04/src/handlers/xdg_shell.rs#L1201-L1259).
6. Cursor-rectangle updates instead use the current popup from the input-method handle and call `popup_repositioned` directly. That bypasses the stale lookup and explains why an identical rectangle can restore positioning. See [Smithay rectangle handling](https://github.com/Smithay/smithay/blob/ff5fa7df392cecfba049ffed55cdaa4e98a8e7ef/src/wayland/input_method/input_method_handle.rs#L103-L121) and [Niri reposition handler](https://github.com/niri-wm/niri/blob/v26.04/src/handlers/mod.rs#L244-L247).

## Suggested fix scope / remaining verification

Investigate removing stale input-method popup registrations after parent destruction or before tracking the same popup under a new parent. The correct ownership of the fix (Smithay manager versus Niri integration) remains open. A fix should preserve live popup reuse and avoid returning a stale parent association.

Acceptance: the same client-exit/recreation sequence places every popup correctly before any repeated cursor update; ordinary focus switching and hide/show still work. No compositor patch has been built or tested. No claim is made that other input methods are affected or unaffected.

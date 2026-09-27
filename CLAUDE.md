# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Workflow

When the user asks to commit/push, check whether CLAUDE.md or README.md need updates to reflect the changes made in the session. Update them in the same commit if so. Only update these files at commit time, not during development.

## Project

foot is a fast, lightweight Wayland-native terminal emulator written in C11. This is a fork with custom multi-tab support being actively developed.

## Build Commands

```bash
# Setup (one-time)
meson setup build --buildtype=debug

# Build
meson compile -C build

# Run tests
meson test -C build

# Smoke-test a build under a nested compositor
systemd-run --user --scope --collect -q sh -c 'kwin_wayland --virtual --no-lockscreen --no-global-shortcuts --socket wayland-foottest-$$ & sleep 1; WAYLAND_DISPLAY=wayland-foottest-$$ timeout 20 ./build/foot true; kill $!'

# Performance-optimized release build
meson setup --buildtype=release --prefix=/usr -Db_lto=true build
```

The build uses meson/ninja. Dependencies (fcft, tllist) are auto-fetched as subprojects if not installed system-wide. GCC produces significantly faster binaries than Clang.

Nested `kwin_wayland --virtual` instances run at realtime priority and keep running if the launching terminal dies, so always start them inside a scope that kills the compositor with the test, as above. Before and after a smoke run, check for leftovers with `ls /run/user/1000/ | grep foottest` and `pgrep -af 'kwin_wayland --virtual'`; kill any that remain.

## Continuous Integration

`.github/workflows/ci.yml` is the authoritative CI pipeline. It runs on
Ubuntu 24.04 and includes static Python checks, GCC debug and Clang release
builds, tests, binary smoke checks, and a 7% line-coverage ratchet (raise it
as coverage grows, never lower it). CI forces
the pinned fcft, tllist, and wayland-protocols fallbacks while allowing their
nested dependencies to resolve from the system.

CI-only Python tools are declared in `.github/requirements-ci.in` and
hash-locked in `.github/requirements-ci.txt`. Regenerate the lock file with the
`uv pip compile` command documented in the input file. Dependabot checks
GitHub Actions and Python dependencies monthly. The fcft, tllist, and
wayland-protocols `.wrap` pins are bumped by hand.

Run the CI static checks and the coverage gate locally:

```bash
# Static checks (codespell, mypy, ruff) from the hash-locked tools
python3 -m venv .venv-ci && .venv-ci/bin/pip install --require-hashes -r .github/requirements-ci.txt && .venv-ci/bin/codespell && .venv-ci/bin/mypy && .venv-ci/bin/ruff check .

# Coverage gate (needs .venv-ci for gcovr)
meson setup build-coverage --buildtype=debug -Db_coverage=true && meson test -C build-coverage && .venv-ci/bin/gcovr --root . --exclude tests/ --exclude subprojects/ --exclude build-coverage/ --fail-under-line 7 --print-summary build-coverage
```

C has no separate linter or formatter. GCC and Clang with `-Werror -pedantic`
act as the linter and type checker. Code follows upstream's style, with
whitespace set by `.editorconfig`: reformatting upstream files with
clang-format would turn every upstream merge into conflicts.

Only the fork's pure logic has unit tests (`tests/test-tab-*.c`). `tab.c`
has no test file of its own because it needs a live compositor: move pure
decisions out into small helpers (`tab-close.c`, `tab-pin.c`,
`tab-activity.c`) and test them there, and exercise the rest with the
nested-compositor smoke run above. Upstream modules keep upstream's coverage.

## Distribution and changelog

foot is installed as the `foot-custom` Arch package. `PKGBUILD` clones the
pushed `main` from GitHub, not the local tree, so push before building. Build
and install it with `makepkg -si` in a checkout; `pkgver()` counts commits so
every rebuild reads as an upgrade. A running foot keeps its old binary until
its window closes.

`CHANGELOG.md` is upstream's and is not maintained for fork changes. The fork
changelog is git history plus the fork summary at the top of `README.md`.

## Architecture

**Entry point:** `main.c` - initialization and main event loop.

**Core terminal state:** `terminal.c/h` - the central state machine managing grid, VT state, scrollback, configuration, and tab bar integration. Most features connect through the terminal struct.

**VT sequence processing chain:** `vt.c` (state machine) dispatches to `csi.c` (CSI sequences), `dcs.c` (device control strings), `osc.c` (OS commands), and `sixel.c` (sixel images).

**Rendering:** `render.c/h` - pixman-based rendering pipeline handling font caching, glyph rendering, cell-to-pixel conversion, color management, sixel images, and box-drawing characters. Tab bar rendering (`render_tab_bar()`) is also here.

**Wayland integration:** `wayland.c/h` - xdg-shell window management, surface/subsurface handling, input (keyboard/mouse/touch), clipboard, and protocol support (fractional scaling, color management).

**Configuration:** `config.c/h` parses `foot.ini`. Key bindings are handled in `key-binding.c/h`. Default tab keybindings are defined in `config.c` near the end of the defaults struct.

**Input handling:** `input.c/h` (keyboard/mouse), `search.c/h` (scrollback search), `selection.c/h` (text selection modes), `url-mode.c/h` (URL detection/opening). Tab bar mouse hit-testing uses `tab_x_ends` from the `tab_bar` struct.

**Infrastructure:** `grid.c/h` (cell storage), `server.c/h` (daemon mode), `fdm.c/h` (fd multiplexing), `shm.c/h` (shared memory buffers), `slave.c/h` (PTY management).

## Tab support (custom feature)

`tab.c/h` - tab list management, active/inactive switching, detaching a
terminal from its shared window on shutdown, and per-tab title tracking.
`tab-close.c/h` contains the unit-tested focus-selection helper.
`tab-pin.c/h` contains the unit-tested pinned-tab ordering and width helpers.

Key implementation details:
- Tab bar renders as a Wayland subsurface positioned at (0,0), drawn in `render_tab_bar()` in `render.c`. The main surface already sits below any CSD title bar, so no title offset is applied.
- `tab_bar_height()` returns physical pixels (`roundf(20 * scale)`). This value must be included in `set_size_from_grid()` and subtracted from available height in `render_resize()` margin calculations.
- The bar's font is the regular font at its configured size (zoom-independent), loaded by `term_load_font_at_config_size()` and reloaded lazily in `render_tab_bar()` when the DPI or scale changes.
- Tab titles show the shell's current working directory, read from `/proc/<pid>/cwd` via `term_shell_cwd()`. `$HOME` is collapsed to `~`. Titles are refreshed from `tab_on_output()` on PTY output (debounced to 250 ms, a cwd change always comes with a new prompt), on OSC 7, and on window title changes. Nothing polls `/proc` from the render path.
- Unpinned tab widths are equal, dividing the bar width left after pinned tabs evenly (`tab_pin_widths()` in `tab-pin.c`). Remainder pixels go to the leftmost unpinned tabs. Cumulative x positions stored in `tab_bar.tab_x_ends` for mouse hit-testing in `input.c`.
- Each tab is enclosed in a 1px border (all four sides) drawn with foreground color at `0x4000` alpha.
- New tabs inherit the parent's `font_sizes` array so zoom level carries over.
- `do_tab_switch()` must transfer both `seat->kbd_focus` and `term->kbd_focus` to avoid hollow cursor on the new tab. It also clears the old tab's `render.pending` flags and destroys the window's in-flight frame callback: `frame_callback()` only services its own terminal, so a leftover callback would draw the old tab and strand the new tab's render.
- Grid vertical margin is anchored to the top (`pad_top`, not centered) to prevent text jumping during zoom.
- Shutdown of a tab sharing its window goes through one choke point: `term_shutdown()` unregisters the PTY (which needs the configured window), then calls `tab_detach()` to remove the tab, move focus, and clear `term->window`, so the deferred `fdm_shutdown()` finds no window to destroy. This covers Ctrl+W, the context menu, the shell exiting, and `footclient` teardown alike. Closing the window from the compositor (`xdg_toplevel_close`) calls `tab_shutdown_window()`, which shuts every tab down; the last one destroys the window. `wayl_win_destroy()` unmaps and frees the tab bar and panes via `tab_bar_unmap()` / `tab_bar_destroy()`. Closed tabs are not retained or recoverable.
- The tab activity indicator is process-agnostic infrastructure. `[tab-bar]`
  `activity-pulse-processes` is a comma-separated list of `name[:RRGGBB]`
  entries (default `claude:d97757,codex:10a37f,agy:1a73e8`); names without a
  color use `activity-pulse-color` (`00cc33`). Only hidden tabs show it (not
  the active tab, not any pane in split mode): a hidden tab pulses while its
  matched process produces a run of output, then stays solid once the run
  ends (`activity-pulse-quiet-ms` of silence, default 700) until the tab is
  shown. A run must last `TAB_ACTIVITY_MIN_RUN_MS` (1 s) to count, because
  Claude Code repaints on focus-out and that burst must not mark the tab.
  The working/done state machine is pure and unit-tested in
  `tab-activity.c` (`struct tab_activity_run`, `tests/test-tab-activity.c`),
  along with process-list matching and color lookup; `tab.c` feeds it from
  `tab_on_output()` and derives visibility lazily, so tab switches need no
  hook. `term_foreground_pgid()` / `term_process_comm()` in `terminal.c` read
  the foreground process from `/proc`, and `render.c` draws the indicator.
  The pulse timer only marks the bar dirty; the render hook renders just the
  bar and then commits the window surface, since the bar is a synchronized
  subsurface whose commit does not show until its parent commits.

Keybindings: Ctrl+T (new tab), Ctrl+W (close tab), Ctrl+N (new window in same cwd), Ctrl+Tab / Ctrl+Shift+Tab (next/prev). Also Ctrl+PageDown/PageUp and Shift+Left/Right for next/prev. Ctrl+E toggles split pane mode. Ctrl+D pins/unpins the active tab. Ctrl+Left/Right sends ESC b/f for word movement. PageUp/PageDown scroll the scrollback by a page and Shift+Home/Shift+End jump to its top/bottom (bare Home/End reach the shell). F1 shows the keyboard shortcuts help card; keep its entry table in `render_overlay()` in sync with the default bindings in `config.c`.

Pinned tabs (Ctrl+D, `tab-pin` action): `tab.pinned` marks a tab, and pinned tabs always form a contiguous group at the front of the tab list. `tab_toggle_pin()` in `tab.c` moves the active tab to the end of that group when pinning and to the first slot after it when unpinning (`tab_pin_toggle_target()`, unit-tested in `tests/test-tab-pin.c`). Moving reallocates the list node, so `tb->active` is re-found by terminal afterwards. Cycling, clicking, and closing use plain list order, so pinned tabs behave like normal tabs there. Pinned tabs render as unlabeled squares (width = bar height, capped at an equal share). The toggle calls `render_refresh()` because the tab bar is a synchronized subsurface: a bar-only commit does not show until the window surface commits. The toggle is a no-op (key passes through) in split mode, where pane stacking follows list order. New tabs are always unpinned.

Ctrl+W close behavior: when a subprocess is running, Ctrl+W still closes the tab unless the process name is listed in `[tab-bar] close-passthrough-processes` (default `nano`), in which case the key is sent to the application. The check is `term_foreground_process_matches()`. After closing, focus moves to the right neighbor; if the closed tab was rightmost, focus falls back to the left neighbor.

Ctrl+A select-all behavior: when the foreground process name is listed in `[main] select-all-passthrough-processes` (default `claude`), Ctrl+A passes through to the application instead of triggering select-all + copy. Same check as Ctrl+W.

`footclient --tab` (alias `-b`): adds a new tab to an existing foot window rather than opening a new window. Carries the same payload as a normal spawn (cwd, argv, envp, overrides) plus a new `as_tab:1` bit on `struct client_data` in `client-protocol.h` (consumed one of the 5 reserved bits, struct size unchanged). Server-side handling lives in `fdm_client()` in `server.c`: when `as_tab` is set, it picks a target window (focused terminal's `wl_window` first, else `tll_front(wayl->terms)->window`, else NULL which falls back to a normal new-window spawn), passes it to `term_init()`, then calls `tab_attach()`. `tab_attach()` is the post-`term_init` half of the original `tab_new()` exposed in `tab.h`; the Ctrl+T path now goes through `term_init() + tab_attach()` as well.

Right-click context menu on tab bar: right-clicking a tab opens a small menu with a single `Close Tab` item. State (`ctx_menu_*`) lives on `tab_bar`. Rendered via `OVERLAY_TAB_MENU` in `render_overlay()` at the click anchor (clamped to window bounds). While the menu is open, `wl_pointer_button` and `wl_pointer_motion` short-circuit through `tab_ctx_menu_handle_click()` and `tab_ctx_menu_update_hover()` regardless of which surface the pointer is over (overlay subsurface has empty input region, so events land on the underlying grid/tab-bar surface). Any keypress dismisses (handled in `key_press_release()` before the help-overlay check). `tab_close_at_index()` implements the action; `tab_close_active()` is now a thin wrapper around the shared `tab_close_internal()` so non-active-tab closes work too.

## Split pane mode (custom feature, WIP)

Ctrl+E toggles between tabbed and split-pane view. In split mode, all tabs become simultaneously visible panes arranged in a grid layout.

Key implementation details:
- Each pane is a Wayland subsurface (`tab.pane`) in desync mode, positioned as a child of the main window surface.
- All terminals render independently via per-pane frame callbacks (`tab.pane_frame_cb`), stored on the `struct tab`.
- The render loop (`fdm_hook_refresh_pending_terminals`) skips the inactive-tab filter when `split_mode` is true, allowing all terminals to render.
- `grid_render()` detects split mode and commits to the pane subsurface instead of the window surface. Damage is reported on the pane surface.
- `do_tab_switch()` does not suppress rendering or resize terminals in split mode, since all panes render independently.
- Click-to-focus: clicking a pane switches focus (hover just tracks `split_hovered` for hit-testing). `do_tab_switch()` redraws both old and new panes to update dim state.
- Inactive panes are dimmed with a semi-transparent black overlay applied at the end of `grid_render()`.
- `render_resize()` skips the window geometry in split mode (pane sizes are not window sizes). A configure event in split mode goes to `tab_split_resize()`, which re-lays out the panes for the new size via `split_layout()` and sets the full-window geometry through `render_set_window_geometry()`.
- Pane dimensions use the pre-split content area (tab bar space is not reclaimed). The last column and row absorb the integer-division remainder so panes cover the window.
- Overlays (help card, context menu, flash) are one subsurface per window. `overlay_place()` stacks it above the topmost pane (or the tab bar) and positions it over the active pane; flash messages are additionally drawn into each pane buffer by `render_flash_message()`.
- Creating a new tab or closing down to 1 tab exits split mode automatically; closing one of three or more panes re-lays out the rest.

## Mouse interaction (custom features)

- URLs are underlined on hover. `urls_hover_update()` / `urls_hover_clear()` in `url-mode.c` manage a cached URL list (`term->url_hover`) and toggle `cell->attrs.url` on the live grid. The cache is dropped on scroll (view offset change) and by `fdm_ptmx()` whenever PTY output changes the grid; the next pointer motion rebuilds it.
- Ctrl+Click opens URLs under the cursor in the default browser. Uses `urls_collect()` to find regex and OSC-8 URLs, then `urls_open_at_position()` in `url-mode.c` launches via the configured URL launcher with XDG activation token support.
- Right-click with an active selection copies the selected text to clipboard and deselects. No flash notification -- the deselection itself is the feedback.
- Flash notification positioning supports `term->flash.use_mouse_pos` to anchor the pill at the mouse cursor (top-right) instead of screen center. Currently only used by the Ctrl+A select-all flash (centered).

## Help overlay (custom feature)

F1 toggles a keyboard shortcuts help card rendered as an `OVERLAY_HELP` overlay in `render_overlay()` in `render.c`. The card uses a two-column layout (key + description) with fixed pixel column positions for alignment. State is tracked via `term->help_visible` in `terminal.h`. Any keypress dismisses the overlay (handled in `key_press_release()` in `input.c` before normal binding dispatch). The `BIND_ACTION_SHOW_HELP` action is defined in `key-binding.h` with default F1 binding in `config.c`.

## Window state persistence (custom feature)

`state_save()` / `state_load()` in `terminal.c` keep the last floating window
size, maximized state, and zoom level in `$XDG_STATE_HOME/foot/state`
(default `~/.local/state/foot/state`). State is written when a window's last
tab shuts down and read in `term_init()` for new windows only (tabs inherit
from their window). Restored values are fallbacks for what the configuration
leaves alone: the file records the configured window size and primary font
size in effect when it was written, and each value is applied only while that
configuration is unchanged. Editing `initial-window-size-*` or the font size
in `foot.ini`, or passing `-w`/`-W`, therefore takes effect immediately.
Zoom is re-applied as the same uniform adjustment the zoom bindings make
(`state_apply_zoom()`), so secondary fonts keep their configured relation to
the primary font. The config struct is never mutated.

## Bell command ${pty} template (legacy compatibility)

The `[bell]` command in foot.ini supports a `${pty}` template variable that
expands to the ringing terminal's pty device (e.g. `/dev/pts/5`). Expansion
happens in `term_bell()` in `terminal.c` via `spawn_expand_template()` with
`ptsname(term->ptmx)`; the expanded argv is freed after spawning. Configs
without `${pty}` keep working.

This remains for already-running Claude sessions that captured the old hook
configuration and for the unrelated BEL fallback sound. The current
`~/git/claude-ai-notifs` Linux path uses OSC 777 plus foot's standard
`[desktop-notifications]` adapter and does not depend on `${pty}`.

## Build Options

Key meson options: `ime` (IME support), `grapheme-clustering` (Unicode via libutf8proc), `tests`, `terminfo`, `docs`. See `meson_options.txt` for the full list.

## Compiler Settings

The build enables `-Werror` - warnings are treated as errors. Uses `-fstrict-aliasing` and `-pedantic`.

## Decision log

Dated reasons for choices that are not obvious from the code.

- 2026-09-27: A background-tab activity run must last
  `TAB_ACTIVITY_MIN_RUN_MS` (1 s) to count. Claude Code repaints when it
  loses focus, so leaving an idle Claude tab produced a burst of output that
  would otherwise mark the tab done.
- 2026-09-27: A bar-only redraw commits the window surface. The tab bar is a
  synchronized subsurface, so the background pulse froze whenever the visible
  tab was idle.
- 2026-09-27: Test files that rely on `assert()` start with `#undef NDEBUG`.
  The Clang release CI job defines `NDEBUG`, which compiled the checks out
  and failed the build on unused variables.
- 2026-09-07: `pkgver()` counts all commits. The 1.26.1 tag is not in this
  fork, so the tag-relative count was always 0 and pacman compared commit
  hashes, reporting spurious downgrades.
- 2026-09-07: Tab shutdown goes through `term_shutdown()` and `tab_detach()`.
  Closing a tab whose shell exited, or closing a window with tabs open,
  crashed the server with a use-after-free of the shared window.
- 2026-04-30: `fdm_ptmx()` dispatches Wayland input between PTY read
  iterations. A tab streaming heavy output (an AI session) kept clicks,
  keys, and tab switches waiting in the socket until the read drained.
- 2026-04-10: The pending frame callback is destroyed and cleared before
  the window pointer is nulled. It was otherwise destroyed twice when
  closing a tab with a running subprocess.

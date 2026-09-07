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

# Performance-optimized release build
meson setup --buildtype=release --prefix=/usr -Db_lto=true build
```

The build uses meson/ninja. Dependencies (fcft, tllist) are auto-fetched as subprojects if not installed system-wide. GCC produces significantly faster binaries than Clang.

## Continuous Integration

`.github/workflows/ci.yml` is the authoritative CI pipeline. It runs on
Ubuntu 24.04 and includes static Python checks, GCC debug and Clang release
builds, tests, binary smoke checks, and a 6% line-coverage ratchet. CI forces
the pinned fcft, tllist, and wayland-protocols fallbacks while allowing their
nested dependencies to resolve from the system.

CI-only Python tools are declared in `.github/requirements-ci.in` and
hash-locked in `.github/requirements-ci.txt`. Regenerate the lock file with the
`uv pip compile` command documented in the input file. Dependabot checks
GitHub Actions and Python dependencies monthly.

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

Key implementation details:
- Tab bar renders as a Wayland subsurface positioned at (0,0), drawn in `render_tab_bar()` in `render.c`. The main surface already sits below any CSD title bar, so no title offset is applied.
- `tab_bar_height()` returns physical pixels (`roundf(20 * scale)`). This value must be included in `set_size_from_grid()` and subtracted from available height in `render_resize()` margin calculations.
- The bar's font is the regular font at its configured size (zoom-independent), loaded by `term_load_font_at_config_size()` and reloaded lazily in `render_tab_bar()` when the DPI or scale changes.
- Tab titles show the shell's current working directory, read from `/proc/<pid>/cwd` via `term_shell_cwd()`. `$HOME` is collapsed to `~`. Titles are refreshed from `tab_on_output()` on PTY output (debounced to 250 ms, a cwd change always comes with a new prompt), on OSC 7, and on window title changes. Nothing polls `/proc` from the render path.
- Tab widths are equal, dividing the full bar width evenly (`buf_width / tab_count`). Remainder pixels go to leftmost tabs. Cumulative x positions stored in `tab_bar.tab_x_ends` for mouse hit-testing in `input.c`.
- Each tab is enclosed in a 1px border (all four sides) drawn with foreground color at `0x4000` alpha.
- New tabs inherit the parent's `font_sizes` array so zoom level carries over.
- `do_tab_switch()` must transfer both `seat->kbd_focus` and `term->kbd_focus` to avoid hollow cursor on the new tab. It also clears the old tab's `render.pending` flags and destroys the window's in-flight frame callback: `frame_callback()` only services its own terminal, so a leftover callback would draw the old tab and strand the new tab's render.
- Grid vertical margin is anchored to the top (`pad_top`, not centered) to prevent text jumping during zoom.
- Shutdown of a tab sharing its window goes through one choke point: `term_shutdown()` unregisters the PTY (which needs the configured window), then calls `tab_detach()` to remove the tab, move focus, and clear `term->window`, so the deferred `fdm_shutdown()` finds no window to destroy. This covers Ctrl+W, the context menu, the shell exiting, and `footclient` teardown alike. Closing the window from the compositor (`xdg_toplevel_close`) calls `tab_shutdown_window()`, which shuts every tab down; the last one destroys the window. `wayl_win_destroy()` unmaps and frees the tab bar and panes via `tab_bar_unmap()` / `tab_bar_destroy()`. Closed tabs are not retained or recoverable.
- The tab activity pulse is process-agnostic infrastructure. `[tab-bar]`
  controls whether it is enabled, the comma-separated foreground process
  names to match, its RGB color, and how recently the PTY must have produced
  output. Defaults preserve the original Claude indicator (`claude`, green
  `00cc33`, 700 ms). `tab-activity.c/h` provides exact process-list matching;
  `term_foreground_pgid()` / `term_process_comm()` in `terminal.c` read the
  foreground process from `/proc`, and `render.c` draws the pulse. The pulse
  timer only marks the bar dirty; the render hook picks that up without a
  grid render.

Keybindings: Ctrl+T (new tab), Ctrl+W (close tab), Ctrl+N (new window in same cwd), Ctrl+Tab / Ctrl+Shift+Tab (next/prev). Also Ctrl+PageDown/PageUp and Shift+Left/Right for next/prev. Ctrl+E toggles split pane mode. Ctrl+Left/Right sends ESC b/f for word movement. PageUp/PageDown scroll the scrollback by a page and Shift+Home/Shift+End jump to its top/bottom (bare Home/End reach the shell). F1 shows the keyboard shortcuts help card; keep its entry table in `render_overlay()` in sync with the default bindings in `config.c`.

Ctrl+W close behavior: when a subprocess is running, Ctrl+W still closes the tab unless the process name is listed in `[tab-bar] close-passthrough-processes` (default `nano`), in which case the key is sent to the application. The check is `term_foreground_process_matches()`. After closing, focus moves to the right neighbor; if the closed tab was rightmost, focus falls back to the left neighbor.

Ctrl+A select-all behavior: when the foreground process name is listed in `[main] select-all-passthrough-processes` (default `claude`), Ctrl+A passes through to the application instead of triggering select-all + copy. Same check as Ctrl+W.

`footclient --tab` (alias `-b`): adds a new tab to an existing foot window rather than opening a new window. Carries the same payload as a normal spawn (cwd, argv, envp, overrides) plus a new `as_tab:1` bit on `struct client_data` in `client-protocol.h` (consumed one of the 5 reserved bits, struct size unchanged). Server-side handling lives in `fdm_client()` in `server.c`: when `as_tab` is set, it picks a target window (focused terminal's `wl_window` first, else `tll_front(wayl->terms)->window`, else NULL which falls back to a normal new-window spawn), passes it to `term_init()`, then calls `tab_attach()`. `tab_attach()` is the post-`term_init` half of the original `tab_new()` exposed in `tab.h`; the Ctrl+T path now goes through `term_init() + tab_attach()` as well.

Right-click context menu on tab bar: right-clicking a tab opens a small menu with `Close Tab` and `Duplicate Tab`. State (`ctx_menu_*`) lives on `tab_bar`. Rendered via `OVERLAY_TAB_MENU` in `render_overlay()` at the click anchor (clamped to window bounds). While the menu is open, `wl_pointer_button` and `wl_pointer_motion` short-circuit through `tab_ctx_menu_handle_click()` and `tab_ctx_menu_update_hover()` regardless of which surface the pointer is over (overlay subsurface has empty input region, so events land on the underlying grid/tab-bar surface). Any keypress dismisses (handled in `key_press_release()` before the help-overlay check). `tab_close_at_index()` and `tab_new(target_term)` implement the actions; `tab_close_active()` is now a thin wrapper around the shared `tab_close_internal()` so non-active-tab closes work too.

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

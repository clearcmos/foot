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

`tab.c/h` - tab list management, active/inactive switching, close-with-undo, per-tab title tracking.

Key implementation details:
- Tab bar renders as a Wayland subsurface positioned at (0,0), drawn in `render_tab_bar()` in `render.c`.
- `tab_bar_height()` returns physical pixels (`roundf(20 * scale)`). This value must be included in `set_size_from_grid()` and subtracted from available height in `render_resize()` margin calculations.
- Tab titles show the shell's current working directory, read from `/proc/<pid>/cwd` via `title_from_cwd()` in `tab.c`. `$HOME` is collapsed to `~`. Titles refresh on every render cycle via `tab_bar_refresh_titles()`.
- Tab widths are equal, dividing the full bar width evenly (`buf_width / tab_count`). Remainder pixels go to leftmost tabs. Cumulative x positions stored in `tab_bar.tab_x_ends` for mouse hit-testing in `input.c`.
- Each tab is enclosed in a 1px border (all four sides) drawn with foreground color at `0x4000` alpha.
- New tabs inherit the parent's `font_sizes` array so zoom level carries over.
- `do_tab_switch()` must transfer both `seat->kbd_focus` and `term->kbd_focus` to avoid hollow cursor on the new tab.
- Grid vertical margin is anchored to the top (`pad_top`, not centered) to prevent text jumping during zoom.

Keybindings: Ctrl+T (new), Ctrl+W (close), Ctrl+Tab / Ctrl+Shift+Tab (next/prev), Ctrl+Shift+D (undo close). Also Ctrl+PageDown/PageUp and arrow keys for next/prev.

## Build Options

Key meson options: `ime` (IME support), `grapheme-clustering` (Unicode via libutf8proc), `tests`, `terminfo`, `docs`. See `meson_options.txt` for the full list.

## Compiler Settings

The build enables `-Werror` - warnings are treated as errors. Uses `-fstrict-aliasing` and `-pedantic`.

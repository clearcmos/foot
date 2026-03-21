# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

**Tab support (custom feature):** `tab.c/h` - tab bar rendering, tab list management, active/inactive switching, close-with-undo, per-tab title tracking. This is the primary area of active development.

**VT sequence processing chain:** `vt.c` (state machine) dispatches to `csi.c` (CSI sequences), `dcs.c` (device control strings), `osc.c` (OS commands), and `sixel.c` (sixel images).

**Rendering:** `render.c/h` - pixman-based rendering pipeline handling font caching, glyph rendering, cell-to-pixel conversion, color management, sixel images, and box-drawing characters.

**Wayland integration:** `wayland.c/h` - xdg-shell window management, surface/subsurface handling, input (keyboard/mouse/touch), clipboard, and protocol support (fractional scaling, color management).

**Configuration:** `config.c/h` parses `foot.ini`. Key bindings are handled in `key-binding.c/h`. Commands are defined in `commands.c/h`.

**Input handling:** `input.c/h` (keyboard/mouse), `search.c/h` (scrollback search), `selection.c/h` (text selection modes), `url-mode.c/h` (URL detection/opening).

**Infrastructure:** `grid.c/h` (cell storage), `server.c/h` (daemon mode), `fdm.c/h` (fd multiplexing), `shm.c/h` (shared memory buffers), `slave.c/h` (PTY management).

## Build Options

Key meson options: `ime` (IME support), `grapheme-clustering` (Unicode via libutf8proc), `tests`, `terminfo`, `docs`. See `meson_options.txt` for the full list.

## Compiler Settings

The build enables `-Werror` - warnings are treated as errors. Uses `-fstrict-aliasing` and `-pedantic`.

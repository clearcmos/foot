# Expose/Split Mode - Resume Notes

## What we're building

Ctrl+E toggles an "expose" mode that shows all tabs simultaneously as split panes in one window. The goal is live, usable split terminals (like tmux panes), not just a preview.

## Current state

The feature is partially implemented but visually broken. The basic infrastructure works:
- Keybinding: Ctrl+E toggle (`BIND_ACTION_TAB_EXPOSE` in key-binding.h, config.c, input.c)
- State: `expose_mode` and `expose_hovered` fields on `tab_bar` struct (tab.h)
- Toggle functions: `tab_expose_enter()` / `tab_expose_exit()` in tab.c
- Mouse click handler in input.c to select a pane and exit expose
- `render_expose()` in render.c that composites all terminal `last_buf`s into one buffer

## What's broken

The `render_expose()` function uses pixman scaling to composite each terminal's `last_buf` (full-size snapshot) into a grid cell. The visual output is "completely fucked" - likely the pixman transform/compositing is producing garbage or the coordinates/scaling are wrong.

## Approaches tried and failed

### 1. Wayland subsurfaces (scrapped)
- Created a `wl_subsurface` per terminal, resized each terminal to cell dimensions
- Failed because foot's render pipeline is deeply single-terminal: frame callbacks, buffer management, and the render loop all assume one active terminal
- Specific issues: frame callback contention (only one terminal could render per frame), tight render loops (no throttling), sync-mode subsurfaces needing parent commits
- The subsurface code has been fully removed

### 2. Pixman scaling compositing (current, broken)
- `render_expose()` in render.c gets a full-window buffer, then for each tab scales its `last_buf` down into a grid cell using `pixman_transform_init_scale` + `pixman_image_composite32`
- The active terminal's `last_buf` updates live; inactive terminals show stale snapshots
- Visual output is wrong - needs debugging of the pixman transform math, buffer formats, or coordinate calculations

## Key files modified

- **tab.h** - `expose_mode`, `expose_hovered` fields on `tab_bar`; `tab_expose_enter/exit` declarations
- **tab.c** - `tab_expose_enter()`, `tab_expose_exit()` functions; expose exit on tab close
- **render.c** - `render_expose()` static function; render loop dispatches to it when `expose_mode` is true (via `goto done_rendering`)
- **input.c** - `BIND_ACTION_TAB_EXPOSE` handler (Ctrl+E toggle); mouse click handler for expose grid cells
- **key-binding.h** - `BIND_ACTION_TAB_EXPOSE` enum value
- **config.c** - `"tab-expose"` string mapping and Ctrl+E default binding
- **terminal.h** - no expose-specific changes remain (subsurface field removed)

## What to try next

1. **Debug the pixman compositing** - The `render_expose()` function's pixman scaling is likely the issue. Check:
   - Are the `last_buf->pix[0]` images valid and in the expected format?
   - Is the transform matrix correct? `pixman_double_to_fixed((double)src_w / cell_w)` should scale src down to cell size
   - Are the cell coordinates (cx, cy) correct in physical pixels?
   - Does the output buffer format match the source buffers?

2. **Alternative: render each terminal's grid directly** - Instead of scaling full-size buffers, modify the render pipeline to render each terminal's grid cells directly into a region of the output buffer. This would require:
   - A version of `render_row()` that takes an x/y pixel offset
   - Temporarily swapping which terminal's grid is being rendered
   - This would give crisp text at native resolution instead of blurry scaled text

3. **Alternative: actual terminal resize with single-buffer compositing** - Resize each terminal to cell dimensions, let each one render to its own buffer at native cell size, then composite (no scaling needed, just copy at offset). The challenge is that only the active terminal renders via `grid_render`, so you'd need to force renders for inactive terminals too.

## Other changes in this session (already committed)

- Equal-width tabs (render.c)
- Tab titles from /proc/pid/cwd (tab.c, osc.c)
- 1px borders on all tab sides (render.c)
- Center-aligned tab titles (render.c)
- Shift+Arrow for tab switching (config.c)

## Architecture notes for the expose feature

The core constraint is that `grid_render()` in render.c is tightly coupled to single-terminal rendering:
- It gets a buffer at `term->width x term->height`
- Renders all grid rows into it
- Attaches to `term->window->surface.surf`
- Sets `term->window->frame_callback` (one per window)
- Only called for the active tab (render loop skips inactive)

Any working expose implementation must work within or around these constraints.

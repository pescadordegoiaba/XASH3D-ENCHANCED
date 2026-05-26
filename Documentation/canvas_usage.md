# Xash3D + CS 1.6 Client - Visibility & Canvas Features (Clean / Legitimate)

This document describes the **current legitimate visibility and drawing tools** after full removal of cheat code.

## Current Visibility Solution (Recommended)

The main clean visibility features are **3D outlines rendered inside the player model pipeline** (GameStudioModelRenderer.cpp). They are:

- Always correctly occluded by walls and geometry (no wallhacks)
- High quality (silhouette via polygon offset + precise hitbox wireframes)
- Performance aware (distance culling + head-only mode by default)

### Cvars (registered in hud.cpp)

- `cl_player_outline 1` — Cyan edge/silhouette on other visible players
- `cl_hitbox_outline 1` — 3D hitbox wireframes
- `cl_hitbox_outline_head_only 1` (default) — Only draw the head hitbox (clean aiming reference)
- `cl_hitbox_outline_max_dist 1800` — Maximum distance for hitbox drawing (perf)

See `GameStudioModelRenderer.cpp:988` for the implementation.

## Canvas 2D API

Canvas is a high-level 2D drawing system (thick lines via quads, circles, rects, scissor, state safety) available for **HUDs, debug overlays, and custom projected drawing**.

It is **not** the primary player outline system (the 3D one above is better for that requirement).

It is much more convenient than raw `triangleapi_t` for general 2D work.

## Basic Usage

```c
#include "canvas.h"

// In your drawing function (HUD_Redraw, DrawTransparentTriangles, etc.)
gCanvas->BeginFrame( scr_width, scr_height );

gCanvas->SetColor( 0, 255, 255, 220 );
gCanvas->Line( 100, 100, 200, 150, 3.0f );

gCanvas->PolyLine( points, numPoints, 2.5f );   // Great for player outlines

gCanvas->EndFrame();
```

## Using Canvas from Client DLL

### 1. Renderer Canvas ABI

```cpp
#include "engine_canvas_api.h"

canvas_t *c = ...; // supplied by the engine/renderer integration point
if (c && c->BeginFrame)
{
    c->BeginFrame(ScreenWidth, ScreenHeight);
    c->SetColor(0, 255, 210, 220);
    c->Line(50, 50, 200, 80, 2.5f);
    c->Circle(400, 300, 40, 24, 1.5f);
    c->EndFrame();
}
```

### 2. Local C++ Canvas wrapper (alternative)

See `cl_dll/canvas.h` + `canvas.cpp`. It has extra GL state safety via `glPushAttrib`.

New convenience helpers (added in this cleanup):

- `WorldToScreen(const float world[3], float& sx, float& sy)`
- `Line3D(const float from[3], const float to[3], float thickness)`

These make it easy to do projected 2D drawing.

Example:
```cpp
#include "canvas.h"   // for the local gCanvas, or use engine one + helpers

float start[3] = { ... }, end[3] = { ... };
gCanvas.BeginFrame(w, h);
gCanvas.SetColor(255, 100, 0, 200);
gCanvas.Line3D(start, end, 1.8f);
gCanvas.EndFrame();
```

**Important**: Canvas drawing must happen while 2D projection is active. Safe places are usually `HUD_Redraw` or `DrawTransparentTriangles`.

The 3D player outlines (silhouette + hitboxes) remain the recommended solution for "know exactly where to aim" without visual clutter or wallhacks. Canvas is excellent for additional HUD elements and custom debug.

## Threading Note

Canvas drawing is currently done on the main thread. For very complex 2D scenes you can record commands and replay them, but this is advanced.

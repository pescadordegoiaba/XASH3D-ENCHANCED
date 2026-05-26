/***
*
*	Xash3D Canvas API
*	High-level 2D drawing canvas for modern overlays, player outlines, HUDs, etc.
*	Designed to be much more convenient than raw triangleapi for 2D work.
*
****/

#ifndef CANVAS_H
#define CANVAS_H

#include "xash3d_types.h"

#define CANVAS_API_VERSION 1

typedef struct canvas_s
{
	int version;

	// Frame management
	void (*BeginFrame)( int width, int height );
	void (*EndFrame)( void );

	// State
	void (*SetColor)( byte r, byte g, byte b, byte a );
	void (*SetColorf)( float r, float g, float b, float a );

	// Drawing primitives (screen space, top-left origin like most 2D APIs)
	void (*Line)( float x1, float y1, float x2, float y2, float thickness );
	void (*PolyLine)( const float *points, int numPoints, float thickness ); // x,y,x,y...

	void (*Rect)( float x, float y, float w, float h, float thickness );
	void (*FilledRect)( float x, float y, float w, float h );

	void (*Circle)( float x, float y, float radius, int segments, float thickness );

	// Advanced
	void (*SetScissor)( int x, int y, int w, int h ); // optional
	void (*ResetScissor)( void );

} canvas_t;

// Exported by engine
extern canvas_t *gCanvas;  // or accessed via enginefuncs

#endif // CANVAS_H
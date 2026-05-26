// Basic GL implementation of Xash3D Canvas API
// This is a starting point. Full version should use VBOs + shaders for best performance.

#include "ref_api.h"
#include "gl_local.h"
#include "canvas.h"

#include <math.h>
#include <alloca.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static canvas_t g_canvas;
static int canvas_width, canvas_height;

static GLboolean oldDepthTest, oldCullFace, oldBlend;
static GLint   oldScissor[4];
static GLboolean oldScissorEnabled;

static void Canvas_BeginFrame( int w, int h )
{
	canvas_width = w;
	canvas_height = h;

	oldDepthTest = pglIsEnabled( GL_DEPTH_TEST );
	oldCullFace = pglIsEnabled( GL_CULL_FACE );
	oldBlend = pglIsEnabled( GL_BLEND );
	oldScissorEnabled = pglIsEnabled( GL_SCISSOR_TEST );
	pglGetIntegerv( GL_SCISSOR_BOX, oldScissor );

	pglDisable( GL_DEPTH_TEST );
	pglDisable( GL_CULL_FACE );
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
}

static void Canvas_EndFrame( void )
{
	if( oldDepthTest ) pglEnable( GL_DEPTH_TEST ); else pglDisable( GL_DEPTH_TEST );
	if( oldCullFace  ) pglEnable( GL_CULL_FACE );  else pglDisable( GL_CULL_FACE );
	if( oldBlend     ) pglEnable( GL_BLEND );      else pglDisable( GL_BLEND );
	// Note: we don't restore exact blend func src/dst for GL1.1 compatibility.
	// Canvas always sets SRC_ALPHA, ONE_MINUS_SRC_ALPHA on Begin anyway.

	if( oldScissorEnabled ) pglEnable( GL_SCISSOR_TEST ); else pglDisable( GL_SCISSOR_TEST );
	pglScissor( oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3] );
}

static void Canvas_SetColor( byte r, byte g, byte b, byte a )
{
	pglColor4ub( r, g, b, a );
}

static void Canvas_SetColorf( float r, float g, float b, float a )
{
	pglColor4f( r, g, b, a );
}

static void Canvas_DrawLine( float x1, float y1, float x2, float y2, float thickness )
{
	// Proper thick line using quad (much better than glLineWidth)
	if( thickness <= 1.0f )
	{
		pglBegin( GL_LINES );
		pglVertex2f( x1, y1 );
		pglVertex2f( x2, y2 );
		pglEnd();
		return;
	}

	float dx = x2 - x1;
	float dy = y2 - y1;
	float len = sqrtf( dx*dx + dy*dy );
	if( len < 0.001f ) return;

	float nx = -dy / len * (thickness * 0.5f);
	float ny =  dx / len * (thickness * 0.5f);

	pglBegin( GL_QUADS );
	pglVertex2f( x1 - nx, y1 - ny );
	pglVertex2f( x1 + nx, y1 + ny );
	pglVertex2f( x2 + nx, y2 + ny );
	pglVertex2f( x2 - nx, y2 - ny );
	pglEnd();
}

static void Canvas_PolyLine( const float *points, int numPoints, float thickness )
{
	if( numPoints < 2 ) return;

	if( thickness <= 1.0f )
	{
		pglBegin( GL_LINE_STRIP );
		for( int i = 0; i < numPoints; i++ )
			pglVertex2f( points[i*2], points[i*2+1] );
		pglEnd();
		return;
	}

	// Draw as thick quad strip for better quality
	for( int i = 0; i < numPoints - 1; i++ )
	{
		float x1 = points[i*2];
		float y1 = points[i*2+1];
		float x2 = points[(i+1)*2];
		float y2 = points[(i+1)*2+1];

		float dx = x2 - x1;
		float dy = y2 - y1;
		float len = sqrtf( dx*dx + dy*dy );
		if( len < 0.001f ) continue;

		float nx = -dy / len * (thickness * 0.5f);
		float ny =  dx / len * (thickness * 0.5f);

		pglBegin( GL_QUADS );
		pglVertex2f( x1 - nx, y1 - ny );
		pglVertex2f( x1 + nx, y1 + ny );
		pglVertex2f( x2 + nx, y2 + ny );
		pglVertex2f( x2 - nx, y2 - ny );
		pglEnd();
	}
}

static void Canvas_Rect( float x, float y, float w, float h, float thickness )
{
	float pts[10] = {
		x, y,
		x + w, y,
		x + w, y + h,
		x, y + h,
		x, y
	};
	Canvas_PolyLine( pts, 5, thickness );
}

static void Canvas_FilledRect( float x, float y, float w, float h )
{
	pglBegin( GL_QUADS );
	pglVertex2f( x, y );
	pglVertex2f( x + w, y );
	pglVertex2f( x + w, y + h );
	pglVertex2f( x, y + h );
	pglEnd();
}

static void Canvas_Circle( float x, float y, float radius, int segments, float thickness )
{
	if( segments < 3 ) segments = 16;

	float *pts = (float*)alloca( (segments + 1) * 2 * sizeof(float) );
	int n = 0;

	for( int i = 0; i <= segments; i++ )
	{
		float ang = (float)i / segments * (2.0f * M_PI);
		pts[n++] = x + cosf(ang) * radius;
		pts[n++] = y + sinf(ang) * radius;
	}

	Canvas_PolyLine( pts, n / 2, thickness );
}

static void Canvas_SetScissor( int x, int y, int w, int h )
{
	pglEnable( GL_SCISSOR_TEST );
	pglScissor( x, y, w, h );
}

static void Canvas_ResetScissor( void )
{
	pglDisable( GL_SCISSOR_TEST );
}

canvas_t *R_GetCanvas( void )
{
	static qboolean initialized = false;

	if( !initialized )
	{
		memset( &g_canvas, 0, sizeof(g_canvas) );

		g_canvas.version       = CANVAS_API_VERSION;
		g_canvas.BeginFrame    = Canvas_BeginFrame;
		g_canvas.EndFrame      = Canvas_EndFrame;
		g_canvas.SetColor      = Canvas_SetColor;
		g_canvas.SetColorf     = Canvas_SetColorf;
		g_canvas.Line          = Canvas_DrawLine;
		g_canvas.PolyLine      = Canvas_PolyLine;
		g_canvas.Rect          = Canvas_Rect;
		g_canvas.FilledRect    = Canvas_FilledRect;
		g_canvas.Circle        = Canvas_Circle;
		g_canvas.SetScissor    = Canvas_SetScissor;
		g_canvas.ResetScissor  = Canvas_ResetScissor;

		initialized = true;
	}

	return &g_canvas;
}

/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* gpustats.c - see gpustats.h. */
#include "platform/gpustats.h"

#include "rlgl.h"

static unsigned g_calls;

/* GL types spelled out so no GL header is needed: GLenum = unsigned int,
 * GLsizei = GLint = int on every platform we target. */
#if defined(BPL_PLATFORM_DRM)

void __real_glDrawElements(unsigned mode, int count, unsigned type, const void *indices);
void __wrap_glDrawElements(unsigned mode, int count, unsigned type, const void *indices);
void __real_glDrawArrays(unsigned mode, int first, int count);
void __wrap_glDrawArrays(unsigned mode, int first, int count);
void glFinish(void);

void __wrap_glDrawElements(unsigned mode, int count, unsigned type, const void *indices)
{
    g_calls++;
    __real_glDrawElements(mode, count, type, indices);
}

void __wrap_glDrawArrays(unsigned mode, int first, int count)
{
    g_calls++;
    __real_glDrawArrays(mode, first, count);
}

void gpustats_init(void) {}

static void do_finish(void) { glFinish(); }

#if defined(BPL_GLES3)
/* raylib 5.5 built with GRAPHICS_API_OPENGL_ES3 still calls the ES2 extension
 * name glDrawBuffersEXT (rlActiveDrawBuffers), which Mesa's libGLESv2 does not
 * export; in ES 3.0 the same function is core glDrawBuffers. */
void glDrawBuffers(int n, const unsigned *bufs);
void glDrawBuffersEXT(int n, const unsigned *bufs);
void glDrawBuffersEXT(int n, const unsigned *bufs) { glDrawBuffers(n, bufs); }
#endif

#else

typedef void (*DrawElementsFn)(unsigned mode, int count, unsigned type, const void *indices);
typedef void (*DrawArraysFn)(unsigned mode, int first, int count);
typedef void (*FinishFn)(void);
/* Defined by the glad loader compiled into libraylib.a. */
extern DrawElementsFn glad_glDrawElements;
extern DrawArraysFn glad_glDrawArrays;
extern FinishFn glad_glFinish;

static DrawElementsFn real_draw_elements;
static DrawArraysFn real_draw_arrays;

static void count_draw_elements(unsigned mode, int count, unsigned type, const void *indices)
{
    g_calls++;
    real_draw_elements(mode, count, type, indices);
}

static void count_draw_arrays(unsigned mode, int first, int count)
{
    g_calls++;
    real_draw_arrays(mode, first, count);
}

void gpustats_init(void)
{
    if (real_draw_elements) return;
    real_draw_elements = glad_glDrawElements;
    real_draw_arrays = glad_glDrawArrays;
    glad_glDrawElements = count_draw_elements;
    glad_glDrawArrays = count_draw_arrays;
}

static void do_finish(void) { if (glad_glFinish) glad_glFinish(); }

#endif

unsigned gpustats_take(void)
{
    unsigned n = g_calls;
    g_calls = 0;
    return n;
}

unsigned gpustats_peek(void) { return g_calls; }

void gpustats_finish(void)
{
    rlDrawRenderBatchActive();
    do_finish();
}

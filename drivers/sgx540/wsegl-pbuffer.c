#include "drveglplatform.h"
#include "wsegl.h"

/* DDK 1.8 counts nonzero drawable masks and adds only window/pixmap bits to EGL. */
#define WSEGL_PBUFFER_ONLY (1UL << 2)

static const WSEGLCaps capabilities[] = { { WSEGL_NO_CAPS, 0 } };
static WSEGLConfig configs[] = {
	{ .ui32DrawableType = WSEGL_PBUFFER_ONLY, .ePixelFormat = WSEGL_PIXELFORMAT_ARGB8888 },
	{ .ui32DrawableType = WSEGL_NO_DRAWABLE },
};

static WSEGLError validate_display(NativeDisplayType display)
{
	return display == WSEGL_DEFAULT_DISPLAY ? WSEGL_SUCCESS : WSEGL_BAD_NATIVE_DISPLAY;
}

static WSEGLError open_display(NativeDisplayType display, WSEGLDisplayHandle *handle,
			       const WSEGLCaps **caps, WSEGLConfig **available)
{
	WSEGLError result = validate_display(display);
	if (result != WSEGL_SUCCESS)
		return result;
	*handle = configs;
	*caps = capabilities;
	*available = configs;
	return WSEGL_SUCCESS;
}

static WSEGLError close_display(WSEGLDisplayHandle handle)
{
	return handle == configs ? WSEGL_SUCCESS : WSEGL_BAD_NATIVE_DISPLAY;
}

static WSEGLError reject_window(WSEGLDisplayHandle display, WSEGLConfig *config,
				WSEGLDrawableHandle *drawable, NativeWindowType window,
				WSEGLRotationAngle *rotation)
{
	return WSEGL_BAD_NATIVE_WINDOW;
}

static WSEGLError reject_pixmap(WSEGLDisplayHandle display, WSEGLConfig *config,
				WSEGLDrawableHandle *drawable, NativePixmapType pixmap,
				WSEGLRotationAngle *rotation)
{
	return WSEGL_BAD_NATIVE_PIXMAP;
}

static WSEGLError reject_copy(void *pixels, unsigned long width, unsigned long height,
			      unsigned long stride, WSEGLPixelFormat format, NativePixmapType target)
{
	return WSEGL_BAD_NATIVE_PIXMAP;
}

WSEGL_EXPORT const WSEGL_FunctionTable *WSEGL_GetFunctionTablePointer(void)
{
	static const WSEGL_FunctionTable functions = {
		.ui32WSEGLVersion = WSEGL_VERSION,
		.pfnWSEGL_IsDisplayValid = validate_display,
		.pfnWSEGL_InitialiseDisplay = open_display,
		.pfnWSEGL_CloseDisplay = close_display,
		.pfnWSEGL_CreateWindowDrawable = reject_window,
		.pfnWSEGL_CreatePixmapDrawable = reject_pixmap,
		.pfnWSEGL_CopyFromPBuffer = reject_copy,
	};
	return &functions;
}

#ifdef WSEGL_SELF_TEST
#include <assert.h>

int main(void)
{
	const WSEGL_FunctionTable *functions = WSEGL_GetFunctionTablePointer();
	const WSEGLCaps *caps;
	WSEGLConfig *available;
	WSEGLDisplayHandle handle;

	assert(functions->ui32WSEGLVersion == WSEGL_VERSION);
	assert(functions->pfnWSEGL_IsDisplayValid(1) == WSEGL_BAD_NATIVE_DISPLAY);
	assert(functions->pfnWSEGL_InitialiseDisplay(WSEGL_DEFAULT_DISPLAY, &handle,
						     &caps, &available) == WSEGL_SUCCESS);
	assert(caps[0].eCapsType == WSEGL_NO_CAPS);
	assert(available[0].ui32DrawableType == WSEGL_PBUFFER_ONLY);
	assert(available[1].ui32DrawableType == WSEGL_NO_DRAWABLE);
	assert(functions->pfnWSEGL_CloseDisplay(handle) == WSEGL_SUCCESS);
	return 0;
}
#endif

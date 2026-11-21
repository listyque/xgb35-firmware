#include "services.h"
#include "drveglplatform.h"
#include "wsegl.h"
#include "wsegl-stream.h"
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

typedef struct {
	int fd;
	struct fb_var_screeninfo initial;
	void *pixels;
	size_t length;
	unsigned width, height, stride, front, back;
	unsigned pending, pending_write;
	unsigned last_write[3];
	int async, pending_valid;
	PVRSRV_CONNECTION *connection;
	PVRSRV_DEV_DATA dev;
	IMG_HANDLE mem_context;
	PVRSRV_CLIENT_MEM_INFO *mem;
	PVRSRV_CLIENT_MEM_INFO *pages[3];
} FbWindow;

static const WSEGLCaps caps[] = {
	{ WSEGL_CAP_MIN_SWAP_INTERVAL, 1 }, { WSEGL_CAP_MAX_SWAP_INTERVAL, 1 },
	{ WSEGL_NO_CAPS, 0 },
};
static const WSEGLCaps async_caps[] = {
	{ WSEGL_CAP_MIN_SWAP_INTERVAL, 1 }, { WSEGL_CAP_MAX_SWAP_INTERVAL, 1 },
	{ WSEGL_CAP_WINDOWS_USE_HW_SYNC, 1 }, { WSEGL_NO_CAPS, 0 },
};
static int async_display;
static WSEGLConfig configs[] = {
	{ .ui32DrawableType = WSEGL_DRAWABLE_WINDOW, .ePixelFormat = WSEGL_PIXELFORMAT_RGB565,
	  .ulNativeRenderable = WSEGL_TRUE },
	{ .ui32DrawableType = WSEGL_NO_DRAWABLE },
};

static int pan(FbWindow *w, unsigned row)
{
	struct fb_var_screeninfo v = w->initial;
	v.yoffset = row;
	v.activate = FB_ACTIVATE_VBL;
	return ioctl(w->fd, FBIOPAN_DISPLAY, &v);
}

static WSEGLError destroy_window(WSEGLDrawableHandle handle)
{
	FbWindow *w = handle;
	if (!w) return WSEGL_BAD_DRAWABLE;
	if (w->fd >= 0 && w->front != w->initial.yoffset) pan(w, w->initial.yoffset);
	if (w->mem) {
		if (w->fd >= 0) PVRSRVUnwrapExtMemory(&w->dev, w->mem);
		else PVRSRVFreeDeviceMem(&w->dev, w->mem);
	}
	for (unsigned i=0;i<3;i++)
		if (w->pages[i]) PVRSRVUnwrapExtMemory(&w->dev,w->pages[i]);
	if (w->mem_context) PVRSRVDestroyDeviceMemContext(&w->dev, w->mem_context);
	if (w->connection) PVRSRVDisconnect(w->connection);
	if (w->pixels != MAP_FAILED) munmap(w->pixels, w->length);
	if (w->fd >= 0) close(w->fd);
	free(w);
	return WSEGL_SUCCESS;
}

static int map_pixels(FbWindow *w)
{
	PVRSRV_DEVICE_IDENTIFIER ids[PVRSRV_MAX_DEVICES];
	PVRSRV_HEAP_INFO heaps[PVRSRV_MAX_CLIENT_HEAPS];
	IMG_UINT32 count = 0, heap_count = 0;
	PVRSRV_ERROR error = PVRSRVConnect(&w->connection, 0);
	if (error != PVRSRV_OK) return error;
	error = PVRSRVEnumerateDevices(w->connection, &count, ids);
	if (error != PVRSRV_OK) return error;
	for (unsigned i = 0; i < count && i < PVRSRV_MAX_DEVICES; i++) {
		if (ids[i].eDeviceType != PVRSRV_DEVICE_TYPE_SGX) continue;
		error = PVRSRVAcquireDeviceData(w->connection, ids[i].ui32DeviceIndex,
					      &w->dev, PVRSRV_DEVICE_TYPE_SGX);
		if (error != PVRSRV_OK) return error;
		error = PVRSRVCreateDeviceMemContext(&w->dev, &w->mem_context, &heap_count, heaps);
		if (error != PVRSRV_OK) return error;
		if (w->fd < 0) {
			for (unsigned j = 0; j < heap_count && j < PVRSRV_MAX_CLIENT_HEAPS; j++)
				if (HEAP_IDX(heaps[j].ui32HeapID) == 0) /* SGX general heap */
					return PVRSRVAllocDeviceMem(&w->dev, heaps[j].hDevMemHeap,
						PVRSRV_MEM_READ | PVRSRV_MEM_WRITE | PVRSRV_MEM_WRITECOMBINE,
						w->length, 4096, &w->mem);
			return PVRSRV_ERROR_INVALID_DEVICE;
		}
		if (w->async) {
			size_t page=(size_t)w->initial.yres*w->stride;
			for (unsigned j=0;j<3;j++) {
				error=PVRSRVWrapExtMemory(&w->dev,w->mem_context,page,0,IMG_FALSE,NULL,
					(char *)w->pixels+j*page,PVRSRV_MEM_READ|PVRSRV_MEM_WRITE,&w->pages[j]);
				if (error!=PVRSRV_OK) return error;
				if (!w->pages[j]->psClientSyncInfo || !w->pages[j]->psClientSyncInfo->psSyncData)
					return PVRSRV_ERROR_INVALID_DEVICE;
				w->last_write[j]=w->pages[j]->psClientSyncInfo->psSyncData->ui32WriteOpsPending;
			}
			return PVRSRV_OK;
		}
		return PVRSRVWrapExtMemory(&w->dev, w->mem_context, w->length, 0, IMG_FALSE,
					   NULL, w->pixels, PVRSRV_MEM_READ | PVRSRV_MEM_WRITE, &w->mem);
	}
	return PVRSRV_ERROR_INVALID_DEVICE;
}

static WSEGLError validate_display(NativeDisplayType display)
{
	return display == WSEGL_DEFAULT_DISPLAY ? WSEGL_SUCCESS : WSEGL_BAD_NATIVE_DISPLAY;
}

static WSEGLError open_display(NativeDisplayType display, WSEGLDisplayHandle *handle,
			       const WSEGLCaps **available_caps, WSEGLConfig **available_configs)
{
	if (validate_display(display) != WSEGL_SUCCESS) return WSEGL_BAD_NATIVE_DISPLAY;
	*handle = configs;
	async_display=0;
	if (getenv("OG_SGX_ASYNC")) {
		int fd=open("/dev/fb1",O_RDONLY|O_CLOEXEC);
		struct fb_fix_screeninfo f;
		struct fb_var_screeninfo v;
		if (fd>=0 && !ioctl(fd,FBIOGET_FSCREENINFO,&f) && !ioctl(fd,FBIOGET_VSCREENINFO,&v) &&
		    v.yres==720 && v.yres_virtual>=3*v.yres && f.line_length==2560 &&
		    f.smem_len>=3*v.yres*f.line_length) async_display=1;
		if (fd>=0) close(fd);
	}
	*available_caps = async_display?async_caps:caps;
	*available_configs = configs;
	return WSEGL_SUCCESS;
}

static WSEGLError close_display(WSEGLDisplayHandle handle)
{
	return handle == configs ? WSEGL_SUCCESS : WSEGL_BAD_NATIVE_DISPLAY;
}

static WSEGLError create_window(WSEGLDisplayHandle display, WSEGLConfig *config,
				WSEGLDrawableHandle *drawable, NativeWindowType window,
				WSEGLRotationAngle *rotation)
{
	uintptr_t index = (uintptr_t)window;
	if (display != configs || !config || config->ePixelFormat != WSEGL_PIXELFORMAT_RGB565 ||
	    (index != 1 && index != 2 && index != 3))
		return WSEGL_BAD_NATIVE_WINDOW;
	FbWindow *w = calloc(1, sizeof(*w));
	if (!w) return WSEGL_OUT_OF_MEMORY;
	w->pixels = MAP_FAILED;
	w->fd = open(index == 1 ? "/dev/fb0" : "/dev/fb1", O_RDWR | O_CLOEXEC);
	struct fb_fix_screeninfo f;
	struct fb_var_screeninfo *v = &w->initial;
	if (w->fd < 0 || flock(w->fd, LOCK_EX | LOCK_NB) ||
	    ioctl(w->fd, FBIOGET_FSCREENINFO, &f) || ioctl(w->fd, FBIOGET_VSCREENINFO, v))
		goto fail;
	if (f.type != FB_TYPE_PACKED_PIXELS || f.visual != FB_VISUAL_TRUECOLOR ||
	    v->bits_per_pixel != 16 || v->red.offset != 11 || v->red.length != 5 ||
	    v->green.offset != 5 || v->green.length != 6 || v->blue.offset != 0 ||
	    v->blue.length != 5 || v->xoffset || v->nonstd || v->grayscale ||
	    !v->xres || v->xres > 2048 || !v->yres || v->yres > 2048 ||
	    f.line_length < v->xres * 2 || f.line_length % 64 ||
	    !f.ypanstep || v->yres % f.ypanstep ||
	    v->yres > v->yres_virtual / 2 || v->yoffset > v->yres_virtual - v->yres)
		goto fail;
	w->front = v->yoffset;
	w->back = w->front < v->yres ? v->yres : 0;
	/* The two scanout regions must be complete and disjoint. */
	if (w->front % v->yres || (size_t)(w->front + v->yres) > f.smem_len / f.line_length ||
	    (size_t)(w->back + v->yres) > f.smem_len / f.line_length)
		goto fail;
	w->stride = f.line_length;
	if (async_display) {
		if (index!=3 || v->yres!=720 || v->yres_virtual<3*v->yres ||
		    w->stride!=2560 || f.smem_len<3*v->yres*w->stride) goto fail;
		w->async=1;
	}
	/* Window 3 uses the top left of each HDMI page with a separate DE scaler. */
	w->width = index == 3 ? 640 : v->xres;
	w->height = index == 3 ? 480 : v->yres;
	if (w->width > v->xres || w->height > v->yres) goto fail;
	w->length = (size_t)(w->front > w->back ? w->front : w->back) * w->stride +
		    (size_t)v->yres * w->stride;
	if (w->async) w->length=3*(size_t)v->yres*w->stride;
	w->pixels = mmap(NULL, w->length, PROT_READ | PROT_WRITE, MAP_SHARED, w->fd, 0);
	if (w->pixels == MAP_FAILED) goto fail;
	int error = map_pixels(w);
	if (error != PVRSRV_OK) {
		fprintf(stderr, "WSEGL fbdev: cannot import scanout memory (%d)\n", error);
		goto fail;
	}
	*rotation = WSEGL_ROTATE_0;
	*drawable = w;
	return WSEGL_SUCCESS;
fail:
	destroy_window(w);
	return WSEGL_BAD_NATIVE_WINDOW;
}

static WSEGLError get_buffers(WSEGLDrawableHandle handle, WSEGLDrawableParams *source,
			       WSEGLDrawableParams *render)
{
	FbWindow *w = handle;
	if (!w) return WSEGL_BAD_DRAWABLE;
	if (w->fd < 0) {
		*source = *render = (WSEGLDrawableParams){
			.ui32Width = w->width, .ui32Height = w->height,
			.ui32Stride = w->stride / 4, .ePixelFormat = WSEGL_PIXELFORMAT_ABGR8888,
			.pvLinearAddress = w->mem->pvLinAddr,
			.ui32HWAddress = w->mem->sDevVAddr.uiAddr,
			.hPrivateData = w->mem, .eRotationAngle = WSEGL_ROTATE_0,
		};
		return WSEGL_SUCCESS;
	}
	const unsigned source_row=w->pending_valid?w->pending:w->front;
	PVRSRV_CLIENT_MEM_INFO *mem=w->async?w->pages[source_row/w->initial.yres]:w->mem;
	size_t offset = (size_t)source_row * w->stride;
	*source = (WSEGLDrawableParams){
		.ui32Width = w->width, .ui32Height = w->height,
		.ui32Stride = w->stride / 2, .ePixelFormat = WSEGL_PIXELFORMAT_RGB565,
		.pvLinearAddress = (char *)w->pixels + offset,
		.ui32HWAddress = mem->sDevVAddr.uiAddr + (w->async?0:offset),
		.hPrivateData = mem, .eRotationAngle = WSEGL_ROTATE_0,
	};
	offset = (size_t)w->back * w->stride;
	*render = *source;
	render->pvLinearAddress = (char *)w->pixels + offset;
	mem=w->async?w->pages[w->back/w->initial.yres]:w->mem;
	render->ui32HWAddress = mem->sDevVAddr.uiAddr + (w->async?0:offset);
	render->hPrivateData=mem;
	return WSEGL_SUCCESS;
}

static uint64_t now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}

static int wait_page(FbWindow *w)
{
	if (!w->pending_valid) return 1;
	PVRSRV_SYNC_DATA *sync=w->pages[w->pending/w->initial.yres]->psClientSyncInfo->psSyncData;
	const struct timespec pause={0,200000};
	const uint64_t deadline=now()+100000000ull;
	while ((int32_t)(sync->ui32WriteOpsComplete-w->pending_write)<0) {
		if (now()>=deadline) return 0;
		nanosleep(&pause,NULL);
	}
	return 1;
}

static WSEGLError swap_window(WSEGLDrawableHandle handle, unsigned long swap_count)
{
	FbWindow *w = handle;
	if (!w) return WSEGL_BAD_DRAWABLE;
	int sampled = swap_count == 9 && getenv("OG_SGX_PROFILE");
	uint64_t start = sampled ? now() : 0;
	if (w->async) {
		const unsigned page=w->back/w->initial.yres;
		PVRSRV_SYNC_DATA *sync=w->pages[page]->psClientSyncInfo->psSyncData;
		const unsigned submitted=w->back, fence=sync->ui32WriteOpsPending;
		if (fence==w->last_write[page]) return WSEGL_BAD_DRAWABLE;
		w->last_write[page]=fence;
		if (!wait_page(w)) {
			fprintf(stderr,"WSEGL fbdev: queued scanout write timed out\n");
			return WSEGL_BAD_DRAWABLE;
		}
		const uint64_t ready=sampled?now():0;
		if (w->pending_valid) {
			if (pan(w,w->pending)) return WSEGL_BAD_DRAWABLE;
			w->front=w->pending;
		}
		w->pending=submitted;
		w->pending_write=fence;
		w->pending_valid=1;
		for (unsigned i=0;i<3;i++) {
			unsigned row=i*w->initial.yres;
			if (row!=w->front && row!=w->pending) { w->back=row; break; }
		}
		if (sampled) fprintf(stderr,"WSEGL fbdev queued flip wait %.3f ms pan %.3f ms (write fence %u)\n",
				       (ready-start)/1e6,(now()-ready)/1e6,fence);
		return WSEGL_SUCCESS;
	}
	/* With WSEGL hardware sync unset, EGL waits for the SGX render before swap. */
	if (pan(w, w->back)) return WSEGL_BAD_DRAWABLE;
	if (sampled) fprintf(stderr, "WSEGL fbdev pan %.3f ms\n", (now() - start) / 1e6);
	unsigned old = w->front;
	w->front = w->back;
	w->back = old;
	return WSEGL_SUCCESS;
}

static WSEGLError connect_window(WSEGLDrawableHandle handle)
{
	return handle ? WSEGL_SUCCESS : WSEGL_BAD_DRAWABLE;
}

static WSEGLError disconnect_window(WSEGLDrawableHandle handle)
{
	FbWindow *w = handle;
	if (!w) return WSEGL_BAD_DRAWABLE;
	if (w->async && !wait_page(w)) return WSEGL_BAD_DRAWABLE;
	w->pending_valid=0;
	if (w->front != w->initial.yoffset && pan(w, w->initial.yoffset))
		return WSEGL_BAD_DRAWABLE;
	/* A surface can be made current again after disconnect. */
	if (w->front != w->initial.yoffset) {
		w->back = w->front;
		w->front = w->initial.yoffset;
	}
	return WSEGL_SUCCESS;
}

static WSEGLError swap_interval(WSEGLDrawableHandle handle, unsigned long interval)
{
	return !handle ? WSEGL_BAD_DRAWABLE : interval == 1 ? WSEGL_SUCCESS : WSEGL_BAD_MATCH;
}

static WSEGLError wait_native(WSEGLDrawableHandle handle, unsigned long engine)
{
	if (!handle) return WSEGL_BAD_DRAWABLE;
	return engine == WSEGL_DEFAULT_NATIVE_ENGINE ? WSEGL_SUCCESS : WSEGL_BAD_NATIVE_ENGINE;
}

static WSEGLError pixmap(WSEGLDisplayHandle display, WSEGLConfig *config,
			 WSEGLDrawableHandle *drawable, NativePixmapType native,
			 WSEGLRotationAngle *rotation)
{
	Xgb35StreamPixmap *p = native;
	if (display != configs || config || !p || p->width < 1 || p->width > 640 ||
	    p->height < 1 || p->height > 480) return WSEGL_BAD_NATIVE_PIXMAP;
	FbWindow *w = calloc(1, sizeof(*w));
	if (!w) return WSEGL_OUT_OF_MEMORY;
	w->fd = -1;
	w->pixels = MAP_FAILED;
	w->width = p->width;
	w->height = p->height;
	w->stride = (w->width * 4 + 63) & ~63u;
	w->length = (w->stride * w->height + 4095) & ~4095u;
	if (map_pixels(w) != PVRSRV_OK || !w->mem->pvLinAddr) {
		destroy_window(w);
		return WSEGL_OUT_OF_MEMORY;
	}
	memset(w->mem->pvLinAddr, 0, w->length);
	p->pixels = w->mem->pvLinAddr;
	p->pitch = w->stride;
	*rotation = WSEGL_ROTATE_0;
	*drawable = w;
	return WSEGL_SUCCESS;
}

static WSEGLError copy_drawable(WSEGLDrawableHandle handle, NativePixmapType target)
{
	return WSEGL_BAD_NATIVE_PIXMAP;
}

static WSEGLError copy_pbuffer(void *pixels, unsigned long width, unsigned long height,
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
		.pfnWSEGL_CreateWindowDrawable = create_window,
		.pfnWSEGL_CreatePixmapDrawable = pixmap,
		.pfnWSEGL_DeleteDrawable = destroy_window,
		.pfnWSEGL_SwapDrawable = swap_window,
		.pfnWSEGL_SwapControlInterval = swap_interval,
		.pfnWSEGL_WaitNative = wait_native,
		.pfnWSEGL_CopyFromDrawable = copy_drawable,
		.pfnWSEGL_CopyFromPBuffer = copy_pbuffer,
		.pfnWSEGL_GetDrawableParameters = get_buffers,
		.pfnWSEGL_ConnectDrawable = connect_window,
		.pfnWSEGL_DisconnectDrawable = disconnect_window,
	};
	return &functions;
}

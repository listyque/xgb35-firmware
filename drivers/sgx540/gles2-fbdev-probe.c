#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "gles2-test.h"

int main(int argc, char **argv)
{
	if (argc != 2 || (strcmp(argv[1], "0") && strcmp(argv[1], "1"))) {
		fprintf(stderr, "usage: %s 0|1\n", argv[0]);
		return 2;
	}
	int index = argv[1][0] - '0';
	int fd = open(index ? "/dev/fb1" : "/dev/fb0", O_RDONLY);
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	if (fd < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) || ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
		perror("fbdev probe");
		return 1;
	}
	const uint8_t *fb = mmap(NULL, fix.smem_len, PROT_READ, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap framebuffer");
		return 1;
	}
	printf("fb%d: %ux%u virtual=%ux%u pitch=%u ypan=%u offset=%u\n", index,
	       var.xres,var.yres,var.xres_virtual,var.yres_virtual,fix.line_length,fix.ypanstep,var.yoffset);
	unsigned original_offset=var.yoffset;
	const EGLint cfg[] = { EGL_SURFACE_TYPE,EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE,5, EGL_GREEN_SIZE,6, EGL_BLUE_SIZE,5, EGL_DEPTH_SIZE,16, EGL_NONE };
	const EGLint ver[] = { EGL_CONTEXT_CLIENT_VERSION,2, EGL_NONE };
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLConfig config;
	EGLint count, w, h;
	require_egl(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL), "eglInitialize");
	require_egl(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
	require_egl(eglChooseConfig(display,cfg,&config,1,&count) && count, "window config");
	EGLSurface surface = eglCreateWindowSurface(display,config,(EGLNativeWindowType)(uintptr_t)(index+1),NULL);
	require_egl(surface != EGL_NO_SURFACE, "eglCreateWindowSurface");
	EGLContext context = eglCreateContext(display,config,EGL_NO_CONTEXT,ver);
	require_egl(context != EGL_NO_CONTEXT && eglMakeCurrent(display,surface,surface,context), "eglMakeCurrent");
	require_egl(eglQuerySurface(display,surface,EGL_WIDTH,&w) && eglQuerySurface(display,surface,EGL_HEIGHT,&h), "eglQuerySurface");
	require_egl(eglSwapInterval(display,1), "eglSwapInterval");
	GLuint program=glCreateProgram();
	glAttachShader(program,compile_shader(GL_VERTEX_SHADER,"attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}"));
	glAttachShader(program,compile_shader(GL_FRAGMENT_SHADER,"precision mediump float;uniform vec4 c;void main(){gl_FragColor=c;}"));
	glBindAttribLocation(program,0,"p");
	glLinkProgram(program);
	GLint linked;
	glGetProgramiv(program,GL_LINK_STATUS,&linked);
	if (!linked) return 1;
	glUseProgram(program);
	GLint color=glGetUniformLocation(program,"c");
	const GLfloat triangle[]={-.8f,-.8f,.8f,-.8f,0,.8f};
	glViewport(0,0,w,h);
	glDisable(GL_DITHER);
	glEnable(GL_DEPTH_TEST);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,triangle);
	int ok=1;
	for (int i=0;i<5;i++) {
		unsigned previous=var.yoffset;
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		glUniform4f(color,i%2,1-i%2,0,1);
		glDrawArrays(GL_TRIANGLES,0,3);
		require_egl(eglSwapBuffers(display,surface), "eglSwapBuffers");
		if (ioctl(fd,FBIOGET_VSCREENINFO,&var)) { ok=0; break; }
		size_t offset=(size_t)var.yoffset*fix.line_length;
		if (offset >= fix.smem_len || (size_t)h*fix.line_length > fix.smem_len-offset) {
			ok=0;
			break;
		}
		uint16_t center=*(const volatile uint16_t *)(fb+offset+(h/2)*fix.line_length+w);
		uint16_t corner=*(const volatile uint16_t *)(fb+offset);
		printf("frame=%d page=%u center=%04x corner=%04x\n",i,var.yoffset,center,corner);
		ok &= previous!=var.yoffset && center==(i%2?0xf800:0x07e0) && corner==0 && glGetError()==GL_NO_ERROR;
	}
	eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
	eglDestroyContext(display,context);
	eglDestroySurface(display,surface);
	eglTerminate(display);
	ok &= ioctl(fd,FBIOGET_VSCREENINFO,&var)==0 && var.yoffset==original_offset;
	munmap((void *)fb,fix.smem_len);
	close(fd);
	return !ok;
}

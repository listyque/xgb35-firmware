#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLES2/gl2.h>
#include "drvegl1_4.h"

#include "gles2-test.h"

int main(int argc, char **argv)
{
	if (argc > 2) {
		fprintf(stderr, "usage: %s [triangle.ppm]\n", argv[0]);
		return 1;
	}
	const int size = argc == 2 ? 256 : 32;
	const EGLint config_attributes[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE,
	};
	const EGLint surface_attributes[] = { EGL_WIDTH, size, EGL_HEIGHT, size, EGL_NONE };
	const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	const GLfloat vertices[] = { -0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f };
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLConfig config;
	EGLint count;
	EGLSurface surface;
	EGLContext context;
	GLuint program, vertex, fragment;
	GLint linked = GL_FALSE;
	GLchar log[512] = { 0 };
	const GLubyte *renderer;
	GLubyte *pixels, *center, *corner;
	int ok;
	GLenum error;

	setvbuf(stdout, NULL, _IONBF, 0);
	require_egl(display != EGL_NO_DISPLAY, "eglGetDisplay");
	require_egl(eglInitialize(display, NULL, NULL), "eglInitialize");
	require_egl(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
	require_egl(eglChooseConfig(display, config_attributes, &config, 1, &count), "eglChooseConfig");
	if (!count) {
		fprintf(stderr, "no EGL config for a GLES2 pbuffer\n");
		return 1;
	}
	surface = eglCreatePbufferSurface(display, config, surface_attributes);
	require_egl(surface != EGL_NO_SURFACE, "eglCreatePbufferSurface");
	context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
	require_egl(context != EGL_NO_CONTEXT, "eglCreateContext");
	require_egl(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");

	renderer = glGetString(GL_RENDERER);
	if (!renderer) {
		fprintf(stderr, "glGetString(GL_RENDERER) failed\n");
		return 1;
	}
	printf("GL_RENDERER=%s\n", renderer);
	if (!strstr((const char *)renderer, "PowerVR SGX 540"))
		return 1;
	vertex = compile_shader(GL_VERTEX_SHADER, "attribute vec2 pos; void main() { gl_Position = vec4(pos, 0.0, 1.0); }");
	fragment = compile_shader(GL_FRAGMENT_SHADER, "precision mediump float; void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }");
	program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glBindAttribLocation(program, 0, "pos");
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		fprintf(stderr, "shader link failed: %s\n", log);
		return 1;
	}
	glUseProgram(program);
	glViewport(0, 0, size, size);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	pixels = calloc(size * size, 4);
	if (!pixels)
		return 1;
	glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	center = pixels + 4 * ((size / 2) * size + size / 2);
	corner = pixels + 4 * (size - 1) * size;
	error = glGetError();
	printf("center=%u,%u,%u corner=%u,%u,%u GL error=0x%x\n",
	       center[0], center[1], center[2], corner[0], corner[1], corner[2], error);
	ok = error == GL_NO_ERROR && center[0] <= 8 && center[1] >= 248 && center[2] <= 8 &&
	     corner[0] <= 8 && corner[1] <= 8 && corner[2] <= 8;
	if (ok && argc == 2)
		ok = save_ppm(argv[1], pixels, size, size);
	free(pixels);

	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(display, context);
	eglDestroySurface(display, surface);
	eglTerminate(display);
	return !ok;
}

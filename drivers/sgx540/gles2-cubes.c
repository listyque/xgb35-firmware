#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "gles2-test.h"

enum { WIDTH = 480, HEIGHT = 320, SECONDS = 5, ROUNDS = 3 };

static const char vertex_shader[] =
	"attribute vec3 pos, normal; attribute vec2 uv; uniform vec3 offset, paint;"
	"uniform vec2 rot; varying mediump vec3 color; varying mediump vec2 tex;"
	"vec3 turn(vec3 v){vec2 xz=mat2(rot.x,rot.y,-rot.y,rot.x)*v.xz;"
	"return vec3(xz.x,0.94*v.y-0.342*xz.y,0.342*v.y+0.94*xz.y);}"
	"void main(){vec3 p=turn(pos)*0.54+offset; float d=7.0-p.z;"
	"gl_Position=vec4(1.70*p.x,2.55*p.y,d-1.6,d);"
	"color=paint*(0.30+0.70*max(dot(turn(normal),vec3(-0.36,0.48,0.80)),0.0)); tex=uv;}";
static const char fragment_shader[] =
	"precision mediump float; varying vec3 color; varying vec2 tex;"
	"void main(){vec2 tile=floor(tex*4.0); float k=mod(tile.x+tile.y,2.0);"
	"gl_FragColor=vec4(color*(0.75+0.25*k),1.0);}";

static double now(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t)) {
		perror("clock_gettime");
		exit(1);
	}
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static GLint offset_location, rotation_location, paint_location;

static void setup_scene(void)
{
	/* Four corners per face carry that face's normal and texture coordinates. */
	static const int faces[6][4] = {
		{4,5,7,6}, {1,0,2,3}, {5,1,3,7}, {0,4,6,2}, {6,7,3,2}, {0,1,5,4}
	};
	static const int normals[6][3] = {
		{0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
	};
	static const int corners[] = {0,1,2,0,2,3};
	GLfloat vertices[36][8];
	GLuint buffer, program = glCreateProgram();
	GLint linked;
	glAttachShader(program, compile_shader(GL_VERTEX_SHADER, vertex_shader));
	glAttachShader(program, compile_shader(GL_FRAGMENT_SHADER, fragment_shader));
	glBindAttribLocation(program, 0, "pos");
	glBindAttribLocation(program, 1, "normal");
	glBindAttribLocation(program, 2, "uv");
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		char log[512] = {0};
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		fprintf(stderr, "shader link: %s\n", log);
		exit(1);
	}
	glUseProgram(program);
	offset_location = glGetUniformLocation(program, "offset");
	rotation_location = glGetUniformLocation(program, "rot");
	paint_location = glGetUniformLocation(program, "paint");

	for (int face = 0; face < 6; face++) {
		for (int i = 0; i < 6; i++) {
			int corner = corners[i], point = faces[face][corner];
			GLfloat *v = vertices[6 * face + i];
			for (int axis = 0; axis < 3; axis++) {
				v[axis] = (point & (1 << axis)) ? 1.f : -1.f;
				v[3 + axis] = normals[face][axis];
			}
			v[6] = corner == 1 || corner == 2;
			v[7] = corner >= 2;
		}
	}
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]), (void *)(3 * sizeof(GLfloat)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertices[0]), (void *)(6 * sizeof(GLfloat)));
	for (int i = 0; i < 3; i++)
		glEnableVertexAttribArray(i);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_DITHER);
	glViewport(0, 0, WIDTH, HEIGHT);
	glClearColor(0.035f, 0.055f, 0.09f, 1.f);
}

static void draw(unsigned frame)
{
	static const GLfloat paints[][3] = {
		{0.12f,0.85f,0.90f}, {1.f,0.53f,0.12f}, {0.32f,0.58f,1.f}
	};
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 5; x++) {
			float angle = (frame % 120) * (6.2831853f / 120.f) + 0.43f*x + 0.67f*y;
			glUniform2f(rotation_location, cosf(angle), sinf(angle));
			glUniform3f(offset_location, (x-2)*1.48f, (y-1)*1.48f, (y-1)*0.25f);
			glUniform3fv(paint_location, 1, paints[(x+y)%3]);
			glDrawArrays(GL_TRIANGLES, 0, 36);
		}
	}
}

static void completed_frame(unsigned frame)
{
	GLubyte center[4];
	draw(frame);
	/* Synchronous readback forces the tile renderer to complete this pbuffer. */
	glReadPixels(WIDTH/2, HEIGHT/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
}

static int validate_scene(const char *capture)
{
	static GLubyte first[WIDTH * HEIGHT * 4], second[sizeof(first)];
	unsigned covered = 0, changed = 0;
	draw(0);
	glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, first);
	draw(12);
	glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, second);
	for (size_t i = 0; i < sizeof(first); i += 4) {
		covered += memcmp(first + i, first, 3) != 0;
		changed += memcmp(first + i, second + i, 3) != 0;
	}
	printf("coverage=%u changed=%u\n", covered, changed);
	return glGetError() == GL_NO_ERROR && covered > WIDTH*HEIGHT/8 &&
	       covered < WIDTH*HEIGHT*3/4 && changed > WIDTH*HEIGHT/12 &&
	       (!capture || save_ppm(capture, first, WIDTH, HEIGHT));
}

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s 'expected renderer' [scene.ppm]\n", argv[0]);
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint count, color = 0, depth = 0, stencil = 0, samples = 0;
	EGLConfig configs[128], config = NULL;
	const EGLint attributes[] = {
		EGL_SURFACE_TYPE,EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE,8, EGL_GREEN_SIZE,8, EGL_BLUE_SIZE,8, EGL_ALPHA_SIZE,8,
		EGL_DEPTH_SIZE,24, EGL_STENCIL_SIZE,8, EGL_NONE
	};
	const EGLint size[] = {EGL_WIDTH,WIDTH,EGL_HEIGHT,HEIGHT,EGL_NONE};
	const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
	require_egl(eglInitialize(display, NULL, NULL), "eglInitialize");
	require_egl(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
	require_egl(eglChooseConfig(display, attributes, configs, 128, &count), "eglChooseConfig");
	for (int i = 0; i < count; i++) {
		require_egl(eglGetConfigAttrib(display, configs[i], EGL_BUFFER_SIZE, &color), "color");
		require_egl(eglGetConfigAttrib(display, configs[i], EGL_DEPTH_SIZE, &depth), "depth");
		require_egl(eglGetConfigAttrib(display, configs[i], EGL_STENCIL_SIZE, &stencil), "stencil");
		require_egl(eglGetConfigAttrib(display, configs[i], EGL_SAMPLES, &samples), "samples");
		if (color == 32 && depth == 24 && stencil == 8 && samples == 0) {
			config = configs[i];
			break;
		}
	}
	if (!config) {
		fprintf(stderr, "no single-sample RGBA8/D24S8 pbuffer\n");
		return 1;
	}
	EGLSurface surface = eglCreatePbufferSurface(display, config, size);
	require_egl(surface != EGL_NO_SURFACE, "eglCreatePbufferSurface");
	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
	require_egl(context != EGL_NO_CONTEXT, "eglCreateContext");
	require_egl(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	printf("renderer=%s size=%dx%d rgba=8888 depth=%d stencil=%d samples=%d\n",
	       renderer ? renderer : "?", WIDTH, HEIGHT, depth, stencil, samples);
	if (!renderer || !strstr(renderer, argv[1]))
		return 1;
	setup_scene();
	if (!validate_scene(argc == 3 ? argv[2] : NULL))
		return 1;
	double start = now();
	for (unsigned i = 0; now() - start < 1.; i++)
		completed_frame(i);
	for (int run = 1; run <= ROUNDS; run++) {
		unsigned frames = 0;
		double elapsed;
		start = now();
		do {
			completed_frame(frames++);
			elapsed = now() - start;
		} while (elapsed < SECONDS);
		if (glGetError() != GL_NO_ERROR)
			return 1;
		printf("run=%d frames=%u seconds=%.4f fps=%.3f\n", run, frames, elapsed, frames / elapsed);
	}
	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(display, context);
	eglDestroySurface(display, surface);
	eglTerminate(display);
	return 0;
}

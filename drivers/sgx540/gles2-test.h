#ifndef XGB35_GLES2_TEST_H
#define XGB35_GLES2_TEST_H

static void require_egl(EGLBoolean result, const char *step)
{
	if (!result) {
		fprintf(stderr, "%s failed with EGL error 0x%x\n", step, eglGetError());
		exit(1);
	}
}

static GLuint compile_shader(GLenum type, const GLchar *source)
{
	GLuint shader = glCreateShader(type);
	GLint ok = GL_FALSE;
	GLchar log[512] = { 0 };

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		fprintf(stderr, "shader compilation failed (0x%x): %s\n", type, log);
		exit(1);
	}
	return shader;
}

static int save_ppm(const char *path, const GLubyte *pixels, int width, int height)
{
	FILE *file = fopen(path, "wb");
	if (!file) {
		perror(path);
		return 0;
	}
	int ok = fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
	/* OpenGL counts rows from the bottom. PPM counts from the top. */
	for (int y = height - 1; ok && y >= 0; --y) {
		for (int x = 0; ok && x < width; ++x)
			ok = fwrite(pixels + 4 * (y * width + x), 1, 3, file) == 3;
	}
	return fclose(file) == 0 && ok;
}

#endif

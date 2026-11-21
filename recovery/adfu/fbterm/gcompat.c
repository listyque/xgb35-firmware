/* Minimal glibc-to-musl ABI compatibility shim (libc.so.6) for ARMv7hf Libretro cores
 * (pcsx_rearmed_libretro.so, snes9x2005_libretro.so, fbalpha2012_libretro.so).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Linux ARM EABI kernel stat64 (104 bytes) matches glibc ARMv7 struct stat64 byte-for-byte. */
int __xstat64(int ver, const char *path, void *buf)
{
    (void)ver;
    return (int)syscall(__NR_stat64, path, buf);
}

int __lxstat64(int ver, const char *path, void *buf)
{
    (void)ver;
    return (int)syscall(__NR_lstat64, path, buf);
}

int __fxstat64(int ver, int fd, void *buf)
{
    (void)ver;
    return (int)syscall(__NR_fstat64, fd, buf);
}

char *__strdup(const char *s) { return strdup(s); }
FILE *fopen64(const char *p, const char *m) { return fopen(p, m); }
int fseeko64(FILE *f, long long off, int w) { return fseeko(f, off, w); }
long long ftello64(FILE *f) { return ftello(f); }
int ftruncate64(int fd, long long len) { return ftruncate(fd, len); }
long long lseek64(int fd, long long off, int w) { return lseek(fd, off, w); }
void *mmap64(void *addr, size_t len, int prot, int flags, int fd, long long off)
{
    return mmap(addr, len, prot, flags, fd, off);
}
int open64(const char *p, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return open(p, flags, mode);
}
struct dirent *readdir64(DIR *d) { return readdir(d); }

double __exp_finite(double x) { return exp(x); }
double __log_finite(double x) { return log(x); }
double __pow_finite(double x, double y) { return pow(x, y); }
float __powf_finite(float x, float y) { return powf(x, y); }

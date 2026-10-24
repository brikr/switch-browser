/* C library imports, forwarded to the host's AArch64 glibc.
 *
 * The engine was built against Nintendo's musl fork ("nnmusl"). On AArch64 Linux the musl and
 * glibc calling conventions, errno values and the common struct layouts (stat, FILE* as opaque
 * handle, jmp_buf storage) are compatible, so most functions bind directly. Exceptions are
 * handled here (setjmp, strerror_r) or in path_shims.c (functions taking "mount:/" paths).
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "../host_symbols_c.h"

/* Path-translating wrappers (path_shims.c). */
FILE* nxb_fopen(const char* path, const char* mode);
int nxb_open(const char* path, int flags, ...);
int nxb_access(const char* path, int mode);
int nxb_mkdir(const char* path, mode_t mode);
int nxb_rmdir(const char* path);
int nxb_remove(const char* path);
int nxb_rename(const char* from, const char* to);
int nxb_unlink(const char* path);

/* musl's strerror_r is the XSI version (returns int); glibc's default is the GNU one. */
int __xpg_strerror_r(int errnum, char* buf, size_t buflen);
/* glibc's non-sigmask-saving setjmp; musl's setjmp doesn't save the signal mask either. */
int _setjmp(struct __jmp_buf_tag env[1]);

#define F(name) {#name, (const void*)&name}
#define AS(name, impl) {#name, (const void*)&impl}

const struct host_sym libc_symbols[] = {
    /* stdio */
    F(clearerr), F(fclose), F(ferror), F(fflush), F(fgets), F(fprintf), F(fputc), F(fputs), F(fread),
    F(fseek), F(ftell), F(fwrite), F(perror), F(printf), F(puts), F(setvbuf), F(snprintf), F(sprintf),
    F(sscanf), F(vfprintf), F(vsnprintf), F(stdin), F(stdout), F(stderr), AS(fopen, nxb_fopen), F(fdopen),
    F(feof),
    /* stdlib */
    F(abort), F(aligned_alloc), F(atof), F(atoi), F(bsearch), F(exit), F(free), F(getenv), F(malloc),
    F(qsort), F(rand), F(realloc), F(srand), F(strtol), F(strtoul), F(atexit), F(div), F(posix_memalign),
    F(strtod), F(strtoll),
    /* string */
    F(memchr), F(memcmp), F(memcpy), F(memmove), F(memset), F(strcasecmp), F(strcat), F(strchr), F(strcmp),
    F(strcpy), F(strcspn), F(strdup), F(strerror), F(strlen), F(strncasecmp), F(strncat), F(strncmp),
    F(strncpy), F(strpbrk), F(strrchr), F(strspn), F(strstr), AS(strerror_r, __xpg_strerror_r), F(memmem),
    F(strnlen), F(wcslen),
    /* ctype / locale / iconv */
    F(isalnum), F(isxdigit), F(tolower), F(toupper), F(localeconv), F(iconv), F(iconv_close), F(iconv_open),
    F(nl_langinfo), F(setlocale),
    /* errno, setjmp */
    F(__errno_location), AS(setjmp, _setjmp), F(longjmp),
    /* POSIX I/O */
    F(close), F(fstat), F(ftruncate), F(lseek), F(read), F(write), AS(open, nxb_open), AS(access, nxb_access),
    AS(mkdir, nxb_mkdir), AS(rmdir, nxb_rmdir), AS(remove, nxb_remove), AS(rename, nxb_rename),
    AS(unlink, nxb_unlink),
    /* process / time / threads */
    F(getpid), F(gettimeofday), F(pthread_self), F(sched_yield), F(sleep), F(time), F(clock_gettime),
    F(nanosleep), F(gmtime), F(gmtime_r), F(strftime),
    /* math */
    F(__fpclassify), F(acos), F(acosf), F(acosh), F(asin), F(asinf), F(asinh), F(atan), F(atan2), F(atan2f),
    F(atanh), F(cbrt), F(cos), F(cosf), F(cosh), F(exp), F(exp2), F(exp2f), F(expf), F(expm1), F(fmod),
    F(fmodf), F(frexp), F(hypot), F(llroundf), F(log), F(log10), F(log10f), F(log1p), F(log2), F(log2f),
    F(logf), F(lrint), F(lround), F(lroundf), F(modf), F(nextafter), F(nextafterf), F(pow), F(powf),
    F(remainder), F(sin), F(sinf), F(sinh), F(sqrt), F(sqrtf), F(tan), F(tanf), F(tanh), F(atanf), F(cbrtf),
    F(hypotf), F(ldexp), F(ldexpf), F(remainderf),
};

const unsigned long libc_symbol_count = sizeof libc_symbols / sizeof libc_symbols[0];

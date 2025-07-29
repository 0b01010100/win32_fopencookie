#ifdef _WIN32
// https://www.man7.org/linux/man-pages/man3/fopencookie.3.html
#include <stdio.h>
#include <stdint.h>

extern int fprintf_override(FILE *stream, const char *format, ...);
#define fprintf fprintf_override

typedef size_t  (*cookie_read_function_t) (void* cookie, char* buf, size_t size);
typedef size_t  (*cookie_write_function_t)(void* cookie, const char* buf, size_t size);
typedef int    (*cookie_seek_function_t) (void* cookie, long* offset, int whence);
typedef int    (*cookie_close_function_t)(void* cookie);

typedef struct cookie_io_functions_t  {
    cookie_read_function_t  read;
    cookie_write_function_t write;
    cookie_seek_function_t  seek;
    cookie_close_function_t close;
} cookie_io_functions_t;

FILE* win_fopencookie
(
    void* restrict    cookie_data,

    const char* restrict mode,

    cookie_io_functions_t io_funcs
);

unsigned int get_mode
(
    FILE* _stream
);

FILE *fmemopen
(
    void* restrict buffer, 
    size_t size, 
    const char* restrict mode
);
#endif
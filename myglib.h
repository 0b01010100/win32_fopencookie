#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef size_t (*vc_read) (void* cookie, char* buf, size_t size);
typedef size_t (*vc_write)(void* cookie, const char* buf, size_t size);
typedef int    (*vc_seek) (void* cookie, long offset, int whence);
typedef int    (*vc_close)(void* cookie);

FILE* win_fopencookie(
    void*     cookie,
    vc_read   read_cb,
    vc_write  write_cb,
    vc_seek   seek_cb,
    vc_close  close_cb
);
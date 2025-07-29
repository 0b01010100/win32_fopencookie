#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <myglib.h>


typedef struct {
    char *buffer;
    size_t size;
    size_t pos;
    size_t length;
    int owns_buffer;
    int can_grow;
    char mode;
    int binary;
} fmemopen_cookie_t;

static size_t fmemopen_read(void *cookie, char *buf, size_t size);
static size_t fmemopen_write(void* cookie, const char* buf, size_t size);
static int fmemopen_seek(void *cookie, long *offset, int whence);
static int fmemopen_close(void *cookie);

static size_t fmemopen_read(void *cookie, char *buf, size_t size) {
    fmemopen_cookie_t *c = (fmemopen_cookie_t *)cookie;
    
    if (!c || !buf || size == 0) {
        return 0;
    }
    
    if (c->mode == 'w') {
        errno = EBADF;
        return -1;
    }
    
    size_t available = (c->pos < c->length) ? (c->length - c->pos) : 0;
    size_t to_read = (size < available) ? size : available;
    
    if (to_read > 0) {
        memcpy(buf, c->buffer + c->pos, to_read);
        c->pos += to_read;
    }
    
    return (size_t)to_read;
}

static size_t fmemopen_write(void *cookie, const char* buf, size_t size) {
    fmemopen_cookie_t *c = (fmemopen_cookie_t *)cookie;
    
    if (!c || !buf || size == 0) {
        return 0;
    }
    
    if (c->mode == 'r') {
        errno = EBADF;
        return -1;
    }
    
    size_t needed = c->pos + size;
    if (needed > c->size) {
        if (c->can_grow && c->owns_buffer) {
            size_t new_size = c->size * 2;
            if (new_size < needed) {
                new_size = needed;
            }
            
            char *new_buffer = realloc(c->buffer, new_size);
            if (!new_buffer) {
                errno = ENOMEM;
                return -1;
            }
            
            memset(new_buffer + c->size, 0, new_size - c->size);
            c->buffer = new_buffer;
            c->size = new_size;
        } else {
            if (c->pos >= c->size) {
                return 0;
            }
            size = c->size - c->pos;
            needed = c->pos + size;
        }
    }
    
    if (size > 0) {
        memcpy(c->buffer + c->pos, buf, size);
        c->pos += size;
        
        if (c->pos > c->length) {
            c->length = c->pos;
            
            if (!c->binary && c->length < c->size) {
                c->buffer[c->length] = '\0';
            }
        }
    }
    
    return (size_t)size;
}

static int fmemopen_seek(void *cookie, long *offset, int whence) {
    fmemopen_cookie_t *c = (fmemopen_cookie_t *)cookie;
    
    if (!c || !offset) {
        errno = EINVAL;
        return -1;
    }
    
    long new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = *offset;
            break;
        case SEEK_CUR:
            new_pos = (long)c->pos + *offset;
            break;
        case SEEK_END:
            new_pos = (long)c->length + *offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    
    if (new_pos < 0) {
        errno = EINVAL;
        return -1;
    }
    
    if (c->mode == 'r' && (size_t)new_pos > c->length) {
        new_pos = (long)c->length;
    }
    
    c->pos = (size_t)new_pos;
    *offset = new_pos;
    
    return 0;
}

static int fmemopen_close(void *cookie) {
    fmemopen_cookie_t *c = (fmemopen_cookie_t *)cookie;
    
    if (!c) {
        return 0;
    }
    
    if (c->owns_buffer && c->buffer) {
        free(c->buffer);
    }
    
    free(c);
    
    return 0;
}

static int parse_fmemopen_mode(const char *mode, char *main_mode, int *binary, int *plus) {
    if (!mode || !mode[0]) {
        return -1;
    }
    
    *main_mode = mode[0];
    *binary = 0;
    *plus = 0;
    
    if (*main_mode != 'r' && *main_mode != 'w' && *main_mode != 'a') {
        return -1;
    }
    
    for (int i = 1; mode[i]; i++) {
        switch (mode[i]) {
            case '+':
                *plus = 1;
                break;
            case 'b':
                *binary = 1;
                break;
            default:
                return -1;
        }
    }
    
    return 0;
}

FILE *fmemopen(void *buffer, size_t size, const char *mode) {
    if (!mode || size == 0) {
        errno = EINVAL;
        return NULL;
    }
    
    char main_mode;
    int binary, plus;
    if (parse_fmemopen_mode(mode, &main_mode, &binary, &plus) != 0) {
        errno = EINVAL;
        return NULL;
    }
    
    fmemopen_cookie_t *cookie = malloc(sizeof(fmemopen_cookie_t));
    if (!cookie) {
        errno = ENOMEM;
        return NULL;
    }
    
    cookie->size = size;
    cookie->pos = 0;
    cookie->length = 0;
    cookie->mode = plus ? 'a' : main_mode;
    cookie->binary = binary;
    cookie->can_grow = 0;
    cookie->owns_buffer = 0;
    
    if (buffer == NULL) {

        cookie->buffer = calloc(1, size);
        if (!cookie->buffer) {
            free(cookie);
            errno = ENOMEM;
            return NULL;
        }
        cookie->owns_buffer = 1;
        cookie->can_grow = 1;
        cookie->length = 0;
    } else {
        cookie->buffer = (char *)buffer;
        cookie->owns_buffer = 0;
        
        if (main_mode == 'r' || plus) {
            if (binary) {
                cookie->length = size;
            } else {
                size_t len = strnlen((char *)buffer, size);
                cookie->length = len;
            }
        }
        
        if (main_mode == 'w') {
            if (size > 0) {
                cookie->buffer[0] = '\0';
            }
            cookie->length = 0;
        }
        
        if (main_mode == 'a') {
            cookie->pos = cookie->length;
        }
    }
    
    cookie_io_functions_t io_funcs = {
        .read = fmemopen_read,
        .write = fmemopen_write,
        .seek = fmemopen_seek,
        .close = fmemopen_close
    };
    
    FILE *fp = win_fopencookie(cookie, mode, io_funcs);
    if (!fp) {
        if (cookie->owns_buffer) {
            free(cookie->buffer);
        }
        free(cookie);
        return NULL;
    }
    
    return fp;
}
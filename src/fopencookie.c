#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <Windows.h>
#include <winternl.h>
#include <MinHook.h>
#include <myglib.h>
#include <fcntl.h>
#include <io.h>

typedef struct cookie_t 
{
    void *cookie;
    cookie_io_functions_t  funcs;
    long position;
    unsigned int flags;
    int eof_flag;
    int error_flag;
} cookie_t;

#define MAX_HANDLES 256
static FILE* trackedHandles[MAX_HANDLES] = {0};
static cookie_t* cookieData[MAX_HANDLES] = {0};
static int cookieCount = 0;
static int hooks_initialized = 0;


static CRITICAL_SECTION cookie_mutex;
static int mutex_initialized = 0;

void init_mutex() {
    if (!mutex_initialized) {
        InitializeCriticalSection(&cookie_mutex);
        mutex_initialized = 1;
    }
}

void register_cookie(cookie_t* cd, FILE* file) {
    init_mutex();
    EnterCriticalSection(&cookie_mutex);
    
    if (cookieCount < MAX_HANDLES) {
        trackedHandles[cookieCount] = file;
        cookieData[cookieCount] = cd;
        cookieCount++;
    }
    
    LeaveCriticalSection(&cookie_mutex);
}

cookie_t* find_cookie(FILE* f) {
    init_mutex();
    EnterCriticalSection(&cookie_mutex);
    
    for (int i = 0; i < cookieCount; ++i) {
        if (trackedHandles[i] == f) {
            LeaveCriticalSection(&cookie_mutex);
            return cookieData[i];
        }
    }
    
    LeaveCriticalSection(&cookie_mutex);
    return NULL;
}

void remove_cookie(FILE* f) {
    init_mutex();
    EnterCriticalSection(&cookie_mutex);
    
    for (int i = 0; i < cookieCount; ++i) {
        if (trackedHandles[i] == f) {
            // Shift remaining entries down
            for (int j = i; j < cookieCount - 1; ++j) {
                trackedHandles[j] = trackedHandles[j + 1];
                cookieData[j] = cookieData[j + 1];
            }
            cookieCount--;
            break;
        }
    }
    
    LeaveCriticalSection(&cookie_mutex);
}

// Original function pointers
 
static size_t (*original_fread)(void *, size_t, size_t, FILE *) = NULL;
static size_t (*original_fwrite)(const void *, size_t, size_t, FILE *) = NULL;
static int (*original_fseek)(FILE *, long, int) = NULL;
static long (*original_ftell)(FILE *) = NULL;
static int (*original_fclose)(FILE *) = NULL;
static int (*original_fgetc)(FILE *) = NULL;
static int (*original_fputc)(int, FILE *) = NULL;
static int (*original_fputs)(const char *, FILE *) = NULL;
static char *(*original_fgets)(char *, int, FILE *) = NULL;
static int (*original_fscanf)(FILE *, const char *, ...) = NULL;
static int (*original_fflush)(FILE *) = NULL;
static int (*original_feof)(FILE *) = NULL;
static int (*original_ferror)(FILE *) = NULL;
static void (*original_clearerr)(FILE *) = NULL;
static void (*original_rewind)(FILE *) = NULL;
static int (*original_fsetpos)(FILE *, const fpos_t *) = NULL;
static int (*original_fgetpos)(FILE *, fpos_t *) = NULL;
static int (*original_ungetc)(int, FILE *) = NULL;


// stdio overrides

size_t fread_override(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fread);
        size_t result = fread(ptr, size, nmemb, stream);
        MH_EnableHook(&fread);
        return result;
    }
    
    if (cookie->eof_flag || cookie->error_flag) {
        return 0;
    }
    
    size_t total_size = size * nmemb;
    if (total_size / size != nmemb) {
        errno = EOVERFLOW;
        cookie->error_flag = 1;
        return 0;
    }
    
    size_t bytes_read = cookie->funcs.read(cookie->cookie, (char*)ptr, total_size);
    
    cookie->position += bytes_read;
    return (size_t)bytes_read / size;
}

size_t fwrite_override(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fwrite);
        size_t result = fwrite(ptr, size, nmemb, stream);
        MH_EnableHook(&fwrite);
        return result;
    }
    
    if (cookie->error_flag) {
        return 0;
    }
    
    size_t total_size = size * nmemb;
    if (total_size / size != nmemb) { // Check for overflow
        errno = EOVERFLOW;
        cookie->error_flag = 1;
        return 0;
    }
    
    size_t bytes_written = 0;
    if (cookie->funcs.write) {
        bytes_written = cookie->funcs.write(cookie->cookie, (const char*)ptr, total_size);
        if (bytes_written < 0) {
            cookie->error_flag = 1;
            return 0;
        }
        cookie->position += bytes_written;
    }
    
    return (size_t)bytes_written / size;
}

int fseek_override(FILE *stream, long offset, int whence) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fseek);
        int result = fseek(stream, offset, whence);
        MH_EnableHook(&fseek);
        return result;
    }
    
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }

    long new_offset = offset;
    int result = cookie->funcs.seek(cookie->cookie, &new_offset, whence);
    
    if (result == 0) {
        cookie->position = new_offset;
        cookie->eof_flag = 0;
    }
    
    return result;
}

long ftell_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&ftell);
        long result = ftell(stream);
        MH_EnableHook(&ftell);
        return result;
    }
    
    return cookie->position;
}

int fclose_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fclose);
        int result = fclose(stream);
        MH_EnableHook(&fclose);
        return result;
    }
    
    int result = cookie->funcs.close(cookie->cookie);
    remove_cookie(stream);
    free(cookie);
    
    return result;
}

int fgetc_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fgetc);
        int result = fgetc(stream);
        MH_EnableHook(&fgetc);
        return result;
    }
    
    if (cookie->eof_flag || cookie->error_flag) {
        return EOF;
    }
    
    char c;
    size_t bytes_read = cookie->funcs.read(cookie->cookie, &c, 1);
    
    if (bytes_read <= 0) {
        cookie->eof_flag = 1;
        return EOF;
    }
    
    cookie->position++;
    return (int)(unsigned char)c;
}

int fputc_override(int c, FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fputc);
        int result = fputc(c, stream);
        MH_EnableHook(&fputc);
        return result;
    }
    
    if (cookie->error_flag) {
        return EOF;
    }
    
    char ch = (char)c;
    size_t bytes_written = cookie->funcs.write(cookie->cookie, &ch, 1);
    
    if (bytes_written != 1) {
        cookie->error_flag = 1;
        return EOF;
    }
    
    cookie->position++;
    return c;
}

char* fgets_override(char *s, int size, FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fgets);
        char* result = fgets(s, size, stream);
        MH_EnableHook(&fgets);
        return result;
    }
    
    if (size <= 0 || cookie->eof_flag || cookie->error_flag) {
        return NULL;
    }
    
    int i = 0;
    while (i < size - 1) {
        char c;
        size_t bytes_read = cookie->funcs.read(cookie->cookie, &c, 1);
        
        if (bytes_read <= 0) {
            cookie->eof_flag = 1;
            break;
        }
        
        s[i++] = c;
        cookie->position++;
        
        if (c == '\n') {
            break;
        }
    }
    
    if (i == 0) {
        return NULL;
    }
    
    s[i] = '\0';
    return s;
}

int fputs_override(const char *s, FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fputs);
        int result = fputs(s, stream);
        MH_EnableHook(&fputs);
        return result;
    }
    
    if (cookie->error_flag) {
        return EOF;
    }
    
    size_t len = strlen(s);
    size_t bytes_written = cookie->funcs.write(cookie->cookie, s, len);
    
    if (bytes_written != (size_t)len) {
        cookie->error_flag = 1;
        return EOF;
    }
    
    cookie->position += bytes_written;
    return (int)len;
}

#define TEMP_BUFFER_SIZE 4096

int fprintf_override(FILE *stream, const char *format, ...) {
    cookie_t* cookie = find_cookie(stream);
    
    va_list args;
    va_start(args, format);
    
    if (!cookie) {
        MH_DisableHook(&fprintf);
        int result = vfprintf(stream, format, args);
        MH_EnableHook(&fprintf);
        va_end(args);
        return result;
    }
    
    if (cookie->error_flag) {
        va_end(args);
        return -1;
    }
    
    char *buffer = (char*)malloc(TEMP_BUFFER_SIZE);
    if (!buffer) {
        va_end(args);
        return -1;
    }
    
    int len = vsnprintf(buffer, TEMP_BUFFER_SIZE, format, args);
    va_end(args);
    
    if (len < 0) {
        free(buffer);
        return -1;
    }
    
    if (len >= TEMP_BUFFER_SIZE) {
        free(buffer);
        buffer = (char*)malloc(len + 1);
        if (!buffer) {
            return -1;
        }
        
        va_start(args, format);
        len = vsnprintf(buffer, len + 1, format, args);
        va_end(args);
        
        if (len < 0) {
            free(buffer);
            return -1;
        }
    }
    
    size_t bytes_written = cookie->funcs.write(cookie->cookie, buffer, len);
    free(buffer);
    
    if (bytes_written != len) {
        cookie->error_flag = 1;
        return -1;
    }
    
    cookie->position += bytes_written;
    return len;
}

int fscanf_override(FILE *stream, const char *format, ...) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        va_list args;
        va_start(args, format);
        MH_DisableHook(&fscanf);
        int result = vfscanf(stream, format, args);
        MH_EnableHook(&fscanf);
        va_end(args);
        return result;
    }
    
    char *buffer = (char*)malloc(TEMP_BUFFER_SIZE);
    if (!buffer) {
        return EOF;
    }
    
    size_t total_read = 0;
    size_t buffer_size = TEMP_BUFFER_SIZE;
    
    while (1) {
        size_t bytes_read = cookie->funcs.read(cookie->cookie, 
                                               buffer + total_read, 
                                               buffer_size - total_read - 1);
        
        if (bytes_read <= 0) {
            break;
        }
        
        total_read += (size_t)bytes_read;
        
        if (total_read >= buffer_size - 1) {
            buffer_size *= 2;
            char *new_buffer = (char*)realloc(buffer, buffer_size);
            if (!new_buffer) {
                free(buffer);
                return EOF;
            }
            buffer = new_buffer;
        }
    }
    
    buffer[total_read] = '\0';
    
    va_list args;
    va_start(args, format);
    int result = vsscanf(buffer, format, args);
    va_end(args);
    
    free(buffer);
    return result;
}

int fflush_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fflush);
        int result = fflush(stream);
        MH_EnableHook(&fflush);
        return result;
    }
    // no op 
    return 0;
}

int feof_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&feof);
        int result = feof(stream);
        MH_EnableHook(&feof);
        return result;
    }
    
    return cookie->eof_flag;
}

int ferror_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&ferror);
        int result = ferror(stream);
        MH_EnableHook(&ferror);
        return result;
    }
    
    return cookie->error_flag;
}

void clearerr_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&clearerr);
        clearerr(stream);
        MH_EnableHook(&clearerr);
        return;
    }
    
    cookie->eof_flag = 0;
    cookie->error_flag = 0;
}

void rewind_override(FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&rewind);
        rewind(stream);
        MH_EnableHook(&rewind);
        return;
    }
    
    long offset = 0;
    if (cookie->funcs.seek(cookie->cookie, &offset, SEEK_SET) == 0) {
        cookie->position = 0;
        cookie->eof_flag = 0;
        cookie->error_flag = 0;
    }
}

int fsetpos_override(FILE *stream, const fpos_t *pos) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fsetpos);
        int result = fsetpos(stream, pos);
        MH_EnableHook(&fsetpos);
        return result;
    }
    
    long offset = (long)*pos;
    int result = cookie->funcs.seek(cookie->cookie, &offset, SEEK_SET);
    
    if (result == 0) {
        cookie->position = offset;
        cookie->eof_flag = 0;
    }
    
    return result;
}

int fgetpos_override(FILE *stream, fpos_t *pos) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fgetpos);
        int result = fgetpos(stream, pos);
        MH_EnableHook(&fgetpos);
        return result;
    }
    
    *pos = (fpos_t)cookie->position;
    return 0;
}

int ungetc_override(int c, FILE *stream) {
    cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&ungetc);
        int result = ungetc(c, stream);
        MH_EnableHook(&ungetc);
        return result;
    }
    
    if (cookie->position > 0) {
        long offset = cookie->position - 1;
        if (cookie->funcs.seek(cookie->cookie, &offset, SEEK_SET) == 0) {
            cookie->position--;
            cookie->eof_flag = 0;
            return c;
        }
    }
    
    return EOF;
}

int setup_hooks(void) {
    if (hooks_initialized) {
        return 0;
    }
    
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        return -1;
    }
    
    #define CREATE_HOOK(func) \
        if (MH_CreateHook(&func, func##_override, (LPVOID*)&original_##func) != MH_OK) { \
            goto cleanup_hooks; \
        }
    
    CREATE_HOOK(fread);
    CREATE_HOOK(fwrite);
    CREATE_HOOK(fseek);
    CREATE_HOOK(ftell);
    CREATE_HOOK(fclose);
    CREATE_HOOK(fgetc);
    CREATE_HOOK(fputc);
    CREATE_HOOK(fgets);
    CREATE_HOOK(fputs);
    CREATE_HOOK(fscanf);
    CREATE_HOOK(fflush);
    CREATE_HOOK(feof);
    CREATE_HOOK(ferror);
    CREATE_HOOK(clearerr);
    CREATE_HOOK(rewind);
    CREATE_HOOK(fsetpos);
    CREATE_HOOK(fgetpos);
    CREATE_HOOK(ungetc);
    
    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        goto cleanup_hooks;
    }
    
    hooks_initialized = 1;
    return 0;
    
cleanup_hooks:
    MH_Uninitialize();
    return -1;
}

unsigned int get_mode(FILE* stream) {
    cookie_t* cookie = find_cookie(stream);
    if (!cookie) {
        errno = EBADF;
        return (unsigned int)-1;
    }
    return cookie->flags;
}

FILE* win_fopencookie
(
    void* restrict        cookie_data,
    
    const char* restrict mode,

    cookie_io_functions_t io_funcs
)
{
    setup_hooks();
    
    if (!mode) {
        errno = EINVAL;
        return NULL;
    }

    cookie_t* cookie = malloc(sizeof(cookie_t));
    if (!cookie) {
        errno = ENOMEM;
        return NULL;
    }
    
    cookie->cookie = cookie_data;
    cookie->funcs.read = io_funcs.read;
    cookie->funcs.write = io_funcs.write;
    cookie->funcs.seek = io_funcs.seek;
    cookie->funcs.close = io_funcs.close;
    cookie->eof_flag = 0;
    cookie->error_flag = 0;
    cookie->position = 0;
    

    switch (mode[0]) 
    {
        case 'r':
            cookie->flags = (mode[1] == '+') ? O_RDWR : O_RDONLY;
            break;
        case 'w':
            cookie->flags = (mode[1] == '+') ? (O_RDWR | O_CREAT | O_TRUNC) : (O_WRONLY | O_CREAT | O_TRUNC);
            break;
        case 'a':
            cookie->flags = (mode[1] == '+') ? (O_RDWR | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_APPEND);
            break;
        default:
            free(cookie);
            errno = EINVAL;
            return NULL;
    }

    if ((cookie->flags & O_RDONLY) && !cookie->funcs.read) {
        free(cookie);
        errno = EBADF;
        return NULL;
    }
    if ((cookie->flags & O_WRONLY) && !cookie->funcs.write) {
        free(cookie);
        errno = EBADF;
        return NULL;
    }
    if ((cookie->flags & O_RDWR) &&
        (!cookie->funcs.read || !cookie->funcs.write)) {
        free(cookie);
        errno = EBADF;
        return NULL;
    }

    if ((cookie->flags & O_APPEND) && !cookie->funcs.seek) {
        free(cookie);
        errno = ESPIPE;
        return NULL;
    }

    FILE* f = (FILE*)cookie;
    

    if (cookie->flags & O_APPEND) {
        long end_pos = 0;
        cookie->funcs.seek(cookie->cookie, &end_pos, SEEK_END);
        cookie->position = end_pos;
    }

    register_cookie(cookie, f);
    return f;
}

void cleanup_stdio_hooks(void) {
    if (hooks_initialized) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        hooks_initialized = 0;
    }
    
    if (mutex_initialized) {
        DeleteCriticalSection(&cookie_mutex);
        mutex_initialized = 0;
    }
    
    for (int i = 0; i < cookieCount; i++) {
        if (cookieData[i]) {
            cookieData[i]->funcs.close(cookieData[i]->cookie);
            free(cookieData[i]);
        }
    }
    cookieCount = 0;
}
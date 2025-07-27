#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <Windows.h>
#include <winternl.h>
#include <MinHook.h>
#include <myglib.h>

typedef struct _IO_cookie_t {
    void *cookie;
    cookie_io_functions_t  funcs;
    int eof_flag;
    int error_flag;
    long position;
} _IO_cookie_t;

#define MAX_HANDLES 256
static FILE* trackedHandles[MAX_HANDLES] = {0};
static _IO_cookie_t* cookieData[MAX_HANDLES] = {0};
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

void register_cookie(_IO_cookie_t* cd, FILE* file) {
    init_mutex();
    EnterCriticalSection(&cookie_mutex);
    
    if (cookieCount < MAX_HANDLES) {
        trackedHandles[cookieCount] = file;
        cookieData[cookieCount] = cd;
        cookieCount++;
    }
    
    LeaveCriticalSection(&cookie_mutex);
}

_IO_cookie_t* find_cookie(FILE* f) {
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
typeof(&fread) originalfread = NULL;
typeof(&fwrite) originalfwrite = NULL;
typeof(&fseek) originalfseek = NULL;
typeof(&ftell) originalftell = NULL;
typeof(&fclose) originalfclose = NULL;
typeof(&fgetc) originalfgetc = NULL;
typeof(&fputc) originalfputc = NULL;
typeof(&fputs) originalfputs = NULL;
typeof(&fgets) originalfgets = NULL;
//typeof(&fprintf) originalfprintf = NULL; 
typeof(&fscanf) originalfscanf = NULL;
typeof(&fflush) originalfflush = NULL;
typeof(&feof) originalfeof = NULL;
typeof(&ferror) originalferror = NULL;
typeof(&clearerr) originalclearerr = NULL;
typeof(&rewind) originalrewind = NULL;
typeof(&fsetpos) originalfsetpos = NULL;
typeof(&fgetpos) originalfgetpos = NULL;
typeof(&ungetc) originalungetc = NULL;

// stdio overrides

size_t fread_override(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    size_t bytes_read = cookie->funcs.read(cookie->cookie, ptr, total_size);
    
    if (bytes_read == 0) {
        cookie->eof_flag = 1;
    } else if (bytes_read < total_size) {
        cookie->eof_flag = 1;
    }
    
    cookie->position += bytes_read;
    return bytes_read / size;
}

size_t fwrite_override(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    size_t bytes_written = cookie->funcs.write(cookie->cookie, ptr, total_size);
    
    // if (bytes_written < total_size) {
    //     cookie->error_flag = 1;
    // }
    
    cookie->position += bytes_written;
    return bytes_written / size;
}

int fseek_override(FILE *stream, long offset, int whence) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fseek);
        int result = fseek(stream, offset, whence);
        MH_EnableHook(&fseek);
        return result;
    }
    
    long new_offset = offset;
    int result = cookie->funcs.seek(cookie->cookie, new_offset, whence);
    
    if (result == 0) {
        cookie->position = new_offset;
        cookie->eof_flag = 0;
    }
    
    return result;
}

long ftell_override(FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&ftell);
        long result = ftell(stream);
        MH_EnableHook(&ftell);
        return result;
    }
    
    return cookie->position;
}

int fclose_override(FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fgetc);
        int result = fgetc(stream);
        MH_EnableHook(&fgetc);
        return result;
    }
    
    if (cookie->eof_flag || cookie->error_flag) {
        return EOF;
    }
    
    unsigned char c;
    size_t bytes_read = cookie->funcs.read(cookie->cookie, &c, 1);
    
    if (bytes_read == 0) {
        cookie->eof_flag = 1;
        return EOF;
    }
    
    cookie->position++;
    return (int)c;
}

int fputc_override(int c, FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fputc);
        int result = fputc(c, stream);
        MH_EnableHook(&fputc);
        return result;
    }
    
    if (cookie->error_flag) {
        return EOF;
    }
    
    unsigned char ch = (unsigned char)c;
    size_t bytes_written = cookie->funcs.write(cookie->cookie, &ch, 1);
    
    if (bytes_written != 1) {
        cookie->error_flag = 1;
        return EOF;
    }
    
    cookie->position++;
    return c;
}

char* fgets_override(char *s, int size, FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
        unsigned char c;
        size_t bytes_read = cookie->funcs.read(cookie->cookie, &c, 1);
        
        if (bytes_read == 0) {
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
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    
    if (bytes_written != len) {
        cookie->error_flag = 1;
        return EOF;
    }
    
    cookie->position += bytes_written;
    return (int)len;
}

#define TEMP_BUFFER_SIZE 4096

int fprintf_override(FILE *stream, const char *format, ...) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    
    char *buffer = malloc(TEMP_BUFFER_SIZE);
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
        buffer = malloc(len + 1);
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
    
    if (bytes_written != (size_t)len) {
        cookie->error_flag = 1;
        return -1;
    }
    
    cookie->position += bytes_written;
    return len;
}

int fscanf_override(FILE *stream, const char *format, ...) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        va_list args;
        va_start(args, format);
        MH_DisableHook(&fscanf);
        int result = vfscanf(stream, format, args);
        MH_EnableHook(&fscanf);
        va_end(args);
        return result;
    }
    
    char *buffer = malloc(TEMP_BUFFER_SIZE);
    if (!buffer) {
        return EOF;
    }
    
    size_t total_read = 0;
    size_t buffer_size = TEMP_BUFFER_SIZE;
    
    while (1) {
        size_t bytes_read = cookie->funcs.read(cookie->cookie, 
                                             buffer + total_read, 
                                             buffer_size - total_read - 1);
        
        if (bytes_read == 0) {
            break;
        }
        
        total_read += bytes_read;
        
        if (total_read >= buffer_size - 1) {
            buffer_size *= 2;
            char *new_buffer = realloc(buffer, buffer_size);
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
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&feof);
        int result = feof(stream);
        MH_EnableHook(&feof);
        return result;
    }
    
    return cookie->eof_flag;
}

int ferror_override(FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&ferror);
        int result = ferror(stream);
        MH_EnableHook(&ferror);
        return result;
    }
    
    return cookie->error_flag;
}

void clearerr_override(FILE *stream) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&rewind);
        rewind(stream);
        MH_EnableHook(&rewind);
        return;
    }
    
    long offset = 0;
    if (cookie->funcs.seek(cookie->cookie, offset, SEEK_SET) == 0) {
        cookie->position = 0;
        cookie->eof_flag = 0;
        cookie->error_flag = 0;
    }
}

int fsetpos_override(FILE *stream, const fpos_t *pos) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&fsetpos);
        int result = fsetpos(stream, pos);
        MH_EnableHook(&fsetpos);
        return result;
    }
    
    long offset = (long)*pos;
    int result = cookie->funcs.seek(cookie->cookie, offset, SEEK_SET);
    
    if (result == 0) {
        cookie->position = offset;
        cookie->eof_flag = 0;
    }
    
    return result;
}

int fgetpos_override(FILE *stream, fpos_t *pos) {
    _IO_cookie_t* cookie = find_cookie(stream);
    
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
    _IO_cookie_t* cookie = find_cookie(stream);
    
    if (!cookie) {
        MH_DisableHook(&ungetc);
        int result = ungetc(c, stream);
        MH_EnableHook(&ungetc);
        return result;
    }
    
    if (cookie->position > 0) {
        long offset = cookie->position - 1;
        if (cookie->funcs.seek(cookie->cookie, offset, SEEK_SET) == 0) {
            cookie->position--;
            cookie->eof_flag = 0;
            return c;
        }
    }
    
    return EOF;
}

void setup_hooks(void) {
    if (hooks_initialized) {
        return;
    }
    
    MH_Initialize();
    
    MH_CreateHook(&fread, fread_override, (LPVOID*)&originalfread);
    MH_CreateHook(&fwrite, fwrite_override, (LPVOID*)&originalfwrite);
    MH_CreateHook(&fseek, fseek_override, (LPVOID*)&originalfseek);
    MH_CreateHook(&ftell, ftell_override, (LPVOID*)&originalftell);
    MH_CreateHook(&fclose, fclose_override, (LPVOID*)&originalfclose);
    MH_CreateHook(&fgetc, fgetc_override, (LPVOID*)&originalfgetc);
    MH_CreateHook(&fputc, fputc_override, (LPVOID*)&originalfputc);
    MH_CreateHook(&fgets, fgets_override, (LPVOID*)&originalfgets);
    MH_CreateHook(&fputs, fputs_override, (LPVOID*)&originalfputs);
    //MH_CreateHook(&fprintf, fprintf_override, (LPVOID*)&originalfprintf); -> does not work, so macros instead.
    MH_CreateHook(&fscanf, fscanf_override, (LPVOID*)&originalfscanf);
    MH_CreateHook(&fflush, fflush_override, (LPVOID*)&originalfflush);
    MH_CreateHook(&feof, feof_override, (LPVOID*)&originalfeof);
    MH_CreateHook(&ferror, ferror_override, (LPVOID*)&originalferror);
    MH_CreateHook(&clearerr, clearerr_override, (LPVOID*)&originalclearerr);
    MH_CreateHook(&rewind, rewind_override, (LPVOID*)&originalrewind);
    MH_CreateHook(&fsetpos, fsetpos_override, (LPVOID*)&originalfsetpos);
    MH_CreateHook(&fgetpos, fgetpos_override, (LPVOID*)&originalfgetpos);
    MH_CreateHook(&ungetc, ungetc_override, (LPVOID*)&originalungetc);
    
    MH_EnableHook(&fread);
    MH_EnableHook(&fwrite);
    MH_EnableHook(&fseek);
    MH_EnableHook(&ftell);
    MH_EnableHook(&fclose);
    MH_EnableHook(&fgetc);
    MH_EnableHook(&fputc);
    MH_EnableHook(&fgets);
    MH_EnableHook(&fputs);
    //MH_EnableHook(&fprintf); -> does not work, so macros instead.
    MH_EnableHook(&fscanf);
    MH_EnableHook(&fflush);
    MH_EnableHook(&feof);
    MH_EnableHook(&ferror);
    MH_EnableHook(&clearerr);
    MH_EnableHook(&rewind);
    MH_EnableHook(&fsetpos);
    MH_EnableHook(&fgetpos);
    MH_EnableHook(&ungetc);
    
    hooks_initialized = 1;
}

FILE* win_fopencookie
(
    void*             cookie_data,
    
    //const char *restrict mode, TODO

    cookie_io_functions_t io_funcs
)
{
    setup_hooks();
    
    _IO_cookie_t* cookie = malloc(sizeof(_IO_cookie_t));
    if (!cookie) {
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
    
    FILE* f = (FILE*)cookie;
    
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
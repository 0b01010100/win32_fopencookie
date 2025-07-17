#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <Windows.h>
#include <winternl.h>
#include <MinHook.h>
#include "myglib.h"
typedef struct EXTERNAL_FILE {
    void *_Placeholder;   // For alignment or compatibility, can be NULL
    char *_ptr;           // Current position in buffer
    int   _cnt;           // Characters left in buffer
    char *_base;          // Buffer base
    int   _flag;          // Flags (read/write, error, eof)
    int   _file;          // File descriptor (use -1 for "no fd")
    int   _charbuf;       // For single-char buffering (optional)
    int   _bufsiz;        // Buffer size
    char *_tmpfname;      // Temp file name (usually NULL)
} EXTERNAL_FILE;

typedef struct _IO_cookie_functions_t {
    vc_read  read_cb;
    vc_write write_cb;
    vc_seek  seek_cb;
    vc_close close_cb;
} _IO_cookie_functions_t;

typedef struct _IO_cookie_t {
    union {
        EXTERNAL_FILE file;
        FILE real_file;
    };
    
    _IO_cookie_functions_t funcs;
    void *cookie;
} _IO_cookie_t;

// User Virtual Protect to overload functions
void _overload(const char* func_name, void* hooked_func, void** old_func) {
    LPVOID imageBase = GetModuleHandleA(NULL);
	PIMAGE_DOS_HEADER dosHeaders = (PIMAGE_DOS_HEADER)imageBase;
	PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((DWORD_PTR)imageBase + dosHeaders->e_lfanew);

	PIMAGE_IMPORT_DESCRIPTOR importDescriptor = NULL;
	IMAGE_DATA_DIRECTORY importsDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	importDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)(importsDirectory.VirtualAddress + (DWORD_PTR)imageBase);
	LPCSTR libraryName = NULL;
	HMODULE library = NULL;
	PIMAGE_IMPORT_BY_NAME functionName = NULL; 

	while (importDescriptor->Name != NULL)
	{
		libraryName = (LPCSTR)importDescriptor->Name + ((DWORD_PTR)imageBase);
		library = LoadLibraryA(libraryName);

		if (library)
		{
			PIMAGE_THUNK_DATA originalFirstThunk = NULL, firstThunk = NULL;
			originalFirstThunk = (PIMAGE_THUNK_DATA)((DWORD_PTR)imageBase + importDescriptor->OriginalFirstThunk);
			firstThunk = (PIMAGE_THUNK_DATA)((DWORD_PTR)imageBase + importDescriptor->FirstThunk);

			while (originalFirstThunk->u1.AddressOfData != NULL)
			{
				functionName = (PIMAGE_IMPORT_BY_NAME)((DWORD_PTR)imageBase + originalFirstThunk->u1.AddressOfData);
					
				// find MessageBoxA address
				if (strcmpi(functionName->Name, func_name) == 0)
				{
					SIZE_T bytesWritten = 0;
					DWORD oldProtect = 0, temp = 0;
					VirtualProtect((LPVOID)(&firstThunk->u1.Function),  
					sizeof(void*), PAGE_READWRITE, &oldProtect);
					
					*old_func = (void*)(firstThunk->u1.Function);

					// swap MessageBoxA address with address of hookedMessageBox
					firstThunk->u1.Function = (DWORD_PTR)hooked_func;

					VirtualProtect((LPVOID)(&firstThunk->u1.Function),  
					sizeof(void*), oldProtect, &temp);
				}
				++originalFirstThunk;
				++firstThunk;
			}
		}

		importDescriptor++;
	}
}

#define overload(func_name, hooked_func, old_func) _overload(#func_name, hooked_func, old_func)





#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void* cookie;
    vc_read read_cb;
    vc_write write_cb;
    vc_seek seek_cb;
    vc_close close_cb;
} CookieData;

#define MAX_HANDLES 256
static FILE* trackedHandles[MAX_HANDLES] = {0};
static CookieData* cookieData[MAX_HANDLES] = {0};
static int cookieCount = 0;

void register_cookie(CookieData* cd, FILE* file) {
    trackedHandles[cookieCount] = file;
    cookieData[cookieCount++] = cd;
}

CookieData* find_cookie(FILE* f) {
    for (int i = 0; i < cookieCount; ++i) {
        if (trackedHandles[i] == f) return cookieData[i];
    }
    return NULL;
}


#define fread_func fread
#define fwrite_func fwrite
#define fclose_func fclose
#define fprintf_func __mingw_vfprintf
#define fseek_func fseek

typeof(&fread_func) originalfread = NULL;
typeof(&fwrite_func) originalfwrite = NULL;
typeof(&fclose_func) originalfclose = NULL;
typeof(&fprintf_func) originalfprintf = NULL;
typeof(&fseek_func) originalfseek = NULL;


size_t vc_fwrite(const void *buf, size_t size, size_t nmemb, FILE *f) {
    CookieData* cookie = find_cookie(f);
    int i;

    // If not our FILE*, fallback to the original function
    if (!cookie) {
        MH_DisableHook(&fprintf_func);
        i = fwrite(buf, size, nmemb, f);
        MH_EnableHook(&fprintf_func);
        return i;
    }
    return cookie->write_cb(cookie, buf, size * nmemb);
}

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define TEMP_BUFFER_SIZE 1024

int vc__vmingw_fprintf(FILE * __restrict__ f, const char * __restrict__ fmt, va_list VA) {
    CookieData* cookie = find_cookie(f);
    int i;

    // If not our FILE*, fallback to the original function
    if (!cookie) {
        MH_DisableHook(&fprintf_func);
        i = __mingw_vfprintf(f, fmt, VA);
        MH_EnableHook(&fprintf_func);
        return i;
    }

    char buffer[TEMP_BUFFER_SIZE];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, VA);

    if (len < 0) return -1; // vsnprintf failed

    // Ensure we don't overflow the buffer when writing
    size_t to_write = (len < TEMP_BUFFER_SIZE) ? (size_t)len : TEMP_BUFFER_SIZE;

    // Call the custom fwrite or equivalent (your system)
    size_t written = cookie->write_cb(cookie->cookie, buffer, to_write);

    return (int)written;
}

int vc_fseek(FILE *f, long offset, int whence) {
    CookieData* cookie = find_cookie(f);
    int i;

    // If not our FILE*, fallback to the original function
    if (!cookie) {
        MH_DisableHook(&fseek_func);
        i = fseek(f, offset, whence);
        MH_EnableHook(&fprintf_func);
        return i;
    }
    return cookie->seek_cb(cookie, offset, whence);
}

int vc_fclose(FILE *f) {
    CookieData* cookie = find_cookie(f);
    int i;
    if (!cookie) {
        MH_DisableHook(&fclose_func);
        i = fclose(f);
        MH_EnableHook(&fclose_func);
        return i;
    }
    return cookie->close_cb(cookie);
}

size_t vc_fread(void * __restrict__ _DstBuf,size_t _ElementSize,size_t _Count,FILE * __restrict__ f)
{
    CookieData* cookie = find_cookie(f);
    size_t i;
    if (!cookie) {
        MH_DisableHook(&fread_func);
        i = fread(_DstBuf, _ElementSize, _Count, f);
        MH_EnableHook(&fread_func);
        return i;
    }
    return cookie->read_cb(cookie, _DstBuf, _ElementSize * _Count);
}

void hack(void)
{
    MH_Initialize();

    MH_CreateHook(&fread_func, vc_fread, (LPVOID*)&originalfread);
    MH_CreateHook(&fwrite_func, vc_fwrite, (LPVOID*)&originalfwrite);
    MH_CreateHook(&fclose_func, vc_fclose, (LPVOID*)&originalfclose);
    MH_CreateHook(&fprintf_func, vc__vmingw_fprintf, (LPVOID*)&originalfprintf);
    MH_CreateHook(&fseek_func, vc_fseek, (LPVOID*)&originalfseek);

    MH_EnableHook(&fread_func);
    MH_EnableHook(&fwrite_func);
    MH_EnableHook(&fclose_func);
    MH_EnableHook(&fprintf_func);
    MH_EnableHook(&fseek_func);
}

FILE* win_fopencookie(
    void*     cookie,
    vc_read   read_cb,
    vc_write  write_cb,
    vc_seek   seek_cb,
    vc_close  close_cb
) {
    printf("hacking...\n");
    hack();
    printf("hacked\n");

    // Allocate and store the cookie data
    CookieData* cd = malloc(sizeof(CookieData));
    cd->cookie = cookie;
    cd->read_cb = read_cb;
    cd->write_cb = write_cb;
    cd->seek_cb = seek_cb;
    cd->close_cb = close_cb;

    FILE* f = tmpfile();
  
    register_cookie(cd, f);
    return f;
}

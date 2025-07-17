#include <windows.h>
#include "myglib.h"

size_t myread(void *cookie, char *buf, size_t size)
{
    printf("Called: myread\n");
    return 0;
}

size_t mywrite(void *cookie, const char *buf, size_t size)
{
    printf("Called: mywrite\n");
    return 0;
}
int myseek(void *cookie, long offset, int whence)
{
    printf("Called: myseek\n");
    return 0;
}

int myclose(void *cookie)
{
    printf("Called: myclose\n");
    return 0;
}

int main() 
{
    FILE* f = win_fopencookie(NULL, myread, mywrite, myseek, myclose);
    fread("dd", 1, 3, f);
    fread("dd", 1, 3, f);
    fprintf(f, "D"); // And yes fprintf calls fwrite
    fseek(f, SEEK_END, 0);
    long tell = ftell(f);
    printf("Tell is %ld\n", tell);
    fwrite("hack", sizeof(char), 4, f);
    fclose(f);
    return 0;
}

#include <myglib.h>
#include <stdio.h>
#include <stdarg.h>

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
    FILE* f = win_fopencookie(NULL, (cookie_io_functions_t)
    {
        .read = myread, 
        .write = mywrite, 
        .seek = myseek, 
        .close = myclose
    }
);
    fread("dd", 1, 3, f);
    fwrite("Hack", sizeof(char), 4, f);
    
    const char * d = "I love C";
    fprintf(f, "%s", d);
    fprintf(f, "%s", d);
    long l = ftell(f);
    printf("tell is %ld", l);
    FILE* file = fopen("d.txt", "w+");
    if (!file) {
        perror("Failed to open file");
        return 1; 
    }
    fprintf(file, "%s", d);
    fclose(file);

    fclose(f);
    return 0;
}

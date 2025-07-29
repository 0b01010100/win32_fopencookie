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
    return size;
}

int myseek(void *cookie, long *offset, int whence)
{
    printf("Called: myseek\n");
    return 0;
}

int myclose(void *cookie)
{
    printf("Called: myclose\n");
    return 0;
}

int main(void)
{
    FILE *f = win_fopencookie(NULL, "r+",
        (cookie_io_functions_t){
            .read = myread,
            .write = mywrite,
            .seek = myseek,
            .close = myclose
        }
    );

    if (!f) {
        perror("win_fopencookie failed");
        return 1;
    }

    const char *text = "I love C";

    fwrite("Hack", 1, 4, f);
    fprintf(f, "%s", text);

    char buffer[256] = {0};
    fread(buffer, 1, sizeof(buffer) - 1, f);

    long pos = ftell(f);
    printf("File position: %ld\n", pos);

    fclose(f);

    FILE *file = fopen("d.txt", "w+");
    if (!file) {
        perror("Failed to open d.txt");
        return 1;
    }
    fprintf(file, "%s", text);
    fclose(file);

    return 0;
}

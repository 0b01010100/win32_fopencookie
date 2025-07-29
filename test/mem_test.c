#include <myglib.h>
#include <string.h>
void test_fmemopen_write() {
    printf("Testing fmemopen write mode...\n");
    
    char buffer[100];
    FILE *fp = fmemopen(buffer, sizeof(buffer), "w");
    if (!fp) {
        perror("fmemopen");
        return;
    }
    
    fprintf(fp, "Hello, World! %d", 42);
    fclose(fp);
    
    printf("Buffer contents: '%s'\n", buffer);
}

void test_fmemopen_read() {
    printf("Testing fmemopen read mode...\n");
    
    char buffer[] = "This is a test string for reading";
    FILE *fp = fmemopen(buffer, strlen(buffer), "r");
    if (!fp) {
        perror("fmemopen");
        return;
    }
    
    char read_buffer[50];
    if (fgets(read_buffer, sizeof(read_buffer), fp)) {
        printf("Read: '%s'\n", read_buffer);
    }
    
    fclose(fp);
}

void test_fmemopen_append() {
    printf("Testing fmemopen append mode...\n");
    
    char buffer[100] = "Initial content";
    FILE *fp = fmemopen(buffer, sizeof(buffer), "a");
    if (!fp) {
        perror("fmemopen");
        return;
    }
    
    fprintf(fp, " - appended text");
    fclose(fp);
    
    printf("Buffer contents: '%s'\n", buffer);
}

void test_fmemopen_dynamic() {
    printf("Testing fmemopen with dynamic buffer...\n");
    
    FILE *fp = fmemopen(NULL, 50, "w+");
    if (!fp) {
        perror("fmemopen");
        return;
    }
    
    fprintf(fp, "Dynamic buffer test with a longer string");
    
    // Rewind and read back
    rewind(fp);
    char read_buffer[100];
    if (fgets(read_buffer, sizeof(read_buffer), fp)) {
        printf("Read back: '%s'\n", read_buffer);
    }

    fseek(fp, 0, SEEK_END);
    long l = ftell(fp);
    printf("tell is: %ld", l);

    fclose(fp);
}

int main() {
    printf("fmemopen test suite\n");
    printf("===================\n\n");
    
    test_fmemopen_write();
    printf("\n");
    
    test_fmemopen_read();
    printf("\n");
    
    test_fmemopen_append();
    printf("\n");
    
    test_fmemopen_dynamic();
    printf("\n");
    
    printf("All tests completed.\n");
    return 0;
}

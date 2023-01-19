#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int i; 

    if (argc < 2) {
        fprintf(2, "Usage: mkdir files...\n");
        exit(1);
    }

    for (i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0) < 0) {
            fprintf(2, "Usage: mkdir files...\n");
            break;
        }
    }

    exit(0);
}


#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char* argv[])
{
    char *argv_array[MAXARG];

    int i;

    for (i = 1; i < argc; ++i) {
        argv_array[i - 1] = argv[i];
    }

    char buf[512];
    char ch;
    int j = 0;

    while(read(0, &ch, 1) == 1) {
        if (ch == '\n') {
            buf[j] = '\0';
            argv_array[i - 1] = buf;
            argv_array[i] = 0;
            j = 0;
            int pid = fork();
            if (pid == 0) {
                exec(argv[1], argv_array);
            }
            else {
                //i--;
                wait(0);
            }
        } else {
            buf[j++] = ch;
        }
    }

    exit(0);
}
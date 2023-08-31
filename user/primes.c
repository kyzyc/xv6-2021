#include "kernel/types.h"
#include "user/user.h"

void recur_pipe(int base, int cnt);

int p[35][2];

int main(int argc, char* argv[])
{
    int cnt = 0;
    printf("prime 2\n");
    pipe(p[cnt]);
    int flag = 0;
    for (int i = 3; i <= 35; ++i) {
        if (i % 2 != 0 && flag == 0) {
            flag    = 1;
            int pid = fork();
            if (pid == 0) {
                close(p[cnt][1]);
                recur_pipe(i, cnt + 1);
            }
            else {
                close(p[cnt][0]);
                printf("prime %d\n", i);
                write(p[cnt][1], &i, sizeof(int));
            }
        }
        else if (i % 2 != 0) {
            write(p[cnt][1], &i, sizeof(int));
        }
    }
    close(p[cnt][1]);
    wait(0);
    exit(0);
}

void recur_pipe(int base, int cnt)
{
    int tmp;
    int flag = 0;
    pipe(p[cnt]);
    while ((read(p[cnt - 1][0], &tmp, sizeof(int)) > 0)) {
        if (tmp % base != 0 && flag == 0) {
            flag    = 1;
            int pid = fork();
            if (pid == 0) {
                close(p[cnt][1]);
                recur_pipe(tmp, cnt + 1);
            }
            else {
                close(p[cnt][0]);
                printf("prime %d\n", tmp);
                write(p[cnt][1], &tmp, sizeof(int));
            }
        }
        else if (tmp % base != 0) {
            write(p[cnt][1], &tmp, sizeof(int));
        }
    }
    close(p[cnt - 1][0]);
    close(p[cnt][1]);
    wait(0);
}
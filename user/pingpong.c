#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int p[2];
  pipe(p);

  int pid = fork();
  if (pid == 0) {
    char tmp;
    read(p[0], &tmp, 1);
    printf("%d: received ping\n", getpid());
    write(p[1], &tmp, 1);
    close(p[0]);
    close(p[1]);
  } else if (pid > 0) {
    char tmp;
    write(p[1], "a", 1);
    read(p[0], &tmp, 1);
    printf("%d: received pong\n", getpid());
    close(p[0]);
    close(p[1]);
  } else {
    fprintf(2, "fork failed!\n");
  }

  exit(0);
}
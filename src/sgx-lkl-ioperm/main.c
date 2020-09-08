#include <stdio.h>
#include <sys/io.h>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s command args...", argv[0]);
    return 1;
  }
  if (ioperm(0x80, 1, 1) != 0) {
    perror("ioperm");
  }

  execv(argv[1], &argv[1]);

  return 0;
}

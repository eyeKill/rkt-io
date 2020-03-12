#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

char buf[1 << 16];

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "%s read|write <dotted-address>\n", argv[0]);
    exit(EXIT_FAILURE);
  }


  int read_socket = strcmp(argv[1], "read") == 0;

  in_addr_t addr = inet_addr(argv[2]);
  if (addr == INADDR_NONE) {
    fprintf(stderr, "Invalid address\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in servaddr = {};
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = addr;
  servaddr.sin_port = htons(8888);

  memset(buf, 'a', sizeof(buf));

  for (;;) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    for (;;) {
      int ret = connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
      if (ret != -1) break;
      perror("connect");
      usleep(2000000);
    }

    for (;;) {
      int ret;
      if (read_socket) {
        ret = read(fd, buf, sizeof(buf));
        if (ret <= 0) {
          perror("read");
          break;
        }
      } else {
        ret = write(fd, buf, sizeof(buf));
        if (ret == -1) {
          perror("write");
          break;
        }
      }
    }
    close(fd);
  }

  return 0;
}

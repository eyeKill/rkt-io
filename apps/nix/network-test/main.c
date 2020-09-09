#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

//char buf[1 << 16];
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

int main(int argc, char** argv) {
  if (argc != 5) {
    fprintf(stderr, "%s read|write <dotted-address> num_bytes batch_size\n", argv[0]);
    exit(EXIT_FAILURE);
  }


  int read_socket = strcmp(argv[1], "read") == 0;

  in_addr_t addr = inet_addr(argv[2]);
  if (addr == INADDR_NONE) {
    fprintf(stderr, "Invalid address\n");
    exit(EXIT_FAILURE);
  }

  long num_bytes = strtol(argv[3], NULL, 10);
  long batch_size = strtol(argv[4], NULL, 10);

  size_t total_sent = 0;
  clock_t start;

  struct sockaddr_in servaddr = {};
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = addr;
  servaddr.sin_port = htons(8888);

  char *buf = (char*) malloc(batch_size*sizeof(char));
  memset(buf, 'a', batch_size);

  for (;;) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    for (;;) {
      int ret = connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
      if (ret != -1) break;
      perror("connect");
      usleep(2000000);
    }

    start = clock();

    while(total_sent < num_bytes) {
      int to_send = MIN(batch_size, num_bytes - total_sent);
      int ret;
      if (read_socket) {
        ret = read(fd, buf, to_send);
        if (ret <= 0) {
          perror("read");
          break;
        }
      } else {
        ret = write(fd, buf, to_send);
        if (ret == -1) {
          perror("write");
          break;
        }
      }

      total_sent += to_send;
    }

    printf("{\"bytes: %ld\", \"time\": %lf}\n", total_sent, ((double)clock() - start)/CLOCKS_PER_SEC);
    close(fd);

    break;
  }

  free(buf);

  return 0;
}

#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static inline int64_t rdtsc_s(void) {
  unsigned a, d;
  asm volatile("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
  asm volatile("rdtsc" : "=a"(a), "=d"(d));
  return ((unsigned long)a) | (((unsigned long)d) << 32);
}

static inline int64_t rdtsc_e(void) {
  unsigned a, d;
  asm volatile("rdtscp" : "=a"(a), "=d"(d));
  asm volatile("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)a) | (((unsigned long)d) << 32);
}

struct conn {
  int fd;
  char *send_buf;
  char *recv_buf;
};
#define BUF_SIZE 4096

void *send_data(void *data) {
  volatile struct conn *c = (struct conn*) data;

  for (;;) {
    int ret;
    //int64_t tsc = rdtsc_s();
    //memcpy(c->send_buf, &tsc, sizeof(int64_t));
    clock_t begin = clock();
    memcpy(c->send_buf, &begin, sizeof(clock_t));

    ret = write(c->fd, c->send_buf, BUF_SIZE);

    if (ret == -1) {
      perror("write");
      return NULL;
    }
  }
}

void *receive_data(void *data) {
  volatile struct conn *c = (struct conn *)data;
  uint64_t trace_counter = 0;
  uint64_t trace_freq = 1000;

  int size = 0;

  for (;;) {
    size = read(c->fd, c->recv_buf, BUF_SIZE);
    trace_counter++;

    if (trace_counter % trace_freq == 0) {
      //int64_t tsc = *(int64_t *)c->recv_buf;
      //printf("forward latency: %ld cycles\n", rdtsc_e() - tsc);
      clock_t tsc = *(clock_t *)c->recv_buf;
      if (tsc) {
        printf("forward latency: %lf s\n", ((double)clock() - tsc)/CLOCKS_PER_SEC);
      }
    }

    if (size <= 0) {
      perror("read");
      return NULL;
    }
  }
}


int client(in_addr_t addr) {
  pthread_t send_thread, receive_thread;
  struct sockaddr_in servaddr = {};
  struct conn connection = {};
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = addr;
  servaddr.sin_port = htons(8888);

  connection.send_buf = calloc(BUF_SIZE, sizeof(char));
  if (!connection.send_buf) {
    perror("malloc");
    return 1;
  }

  connection.recv_buf = calloc(BUF_SIZE, sizeof(char));
  if (!connection.recv_buf) {
    perror("malloc");
    return 1;
  }

  for (;;) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
      perror("socket");
      return 1;
    }
    connection.fd = fd;
    int printf(const char* f,...); printf("%s() at %s:%d\n", __func__, __FILE__, __LINE__);
    for (;;) {
      int ret = connect(fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
      if (ret != -1)
        break;
      perror("connect");
      usleep(2000000);
    }
    int printf(const char* f,...); printf("%s() at %s:%d\n", __func__, __FILE__, __LINE__);

    pthread_create(&send_thread, NULL, send_data, &connection);
    pthread_create(&receive_thread, NULL, receive_data, &connection);


    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);
    close(fd);
  }
}

int main(int argc, char** argv) {
  in_addr_t addr;
  if (argc != 2) {
    fprintf(stderr, "%s <dotted-address>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  addr = inet_addr(argv[1]);
  if (addr == INADDR_NONE) {
    fprintf(stderr, "Invalid address\n");
    exit(EXIT_FAILURE);
  }

  return client(addr);
}

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

struct thread_ctx {
  pthread_t id;
  int socket;
  int packets;
  int size;
  struct sockaddr_in addr;
};
char buf[1472];

void *send_thread(void *_args) {
  struct thread_ctx *ctx = (struct thread_ctx*)_args;
  for (unsigned i = 0; i < ctx->packets; i++) {
    int res = sendto(ctx->socket, buf, ctx->size, 0, (struct sockaddr *) &ctx->addr, sizeof(ctx->addr));
    if (res == -1) {
      perror("sendto()");
      break;
    }
  }
  return NULL;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s host packets", argv[0]);
    return 1;
  }
  #define PTHREAD_NUM 8
  struct thread_ctx contexts[PTHREAD_NUM] = {};
  char *host = argv[1];
  int packets = atoi(argv[2]);

  contexts[0].addr.sin_family = AF_INET;
  contexts[0].addr.sin_port = htons(1);

  if (inet_aton(host, &contexts[0].addr.sin_addr) == 0) {
    fprintf(stderr, "inet_aton() failed\n");
    return 1;
  }
  contexts[0].socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (contexts[0].socket == -1) {
    perror("socket");
    return 1;
  }
  for (int i = 1; i < PTHREAD_NUM; i *= 2) {
    memcpy(&contexts[i], &contexts[0], sizeof(contexts));
    contexts[i].socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (contexts[i].socket == -1) {
      perror("socket");
      return 1;
    }
  }

  unsigned sizes[6] = { 32, 128, 512, 1024, 1472, 0 };
  printf("<results>\n");
  for (unsigned *size = sizes; *size; size++) {
    for (unsigned threads = 1; threads <= PTHREAD_NUM; threads *= 2) {
      clock_t start = clock();
      for (unsigned i = 0; i < threads; i++) {
        struct thread_ctx *ctx = &contexts[i];
        ctx->packets = packets / threads;
        ctx->size = *size;
        pthread_create(&ctx->id, NULL, send_thread, ctx);
      }

      for (unsigned i = 0; i < threads; i++) {
        struct thread_ctx *ctx = &contexts[i];
        pthread_join(ctx->id, NULL);
      }

    double total_time = ((double)clock() - start)/CLOCKS_PER_SEC;
    printf("{\"total_time\": %lf, \"data_size\": %d, \"time_per_syscall\": %lf, \"threads\": %u, \"packets_per_thread\": %u}\n",
           total_time,
           *size,
           total_time / packets,
           threads,
           packets / threads);
    }
  }
  printf("</results>\n");
  fflush(stdout);

  for (int i = 1; i < PTHREAD_NUM; i *= 2) {
    close(contexts[i].socket);
  }


  return 0;
}

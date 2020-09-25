#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

struct thread_ctx {
  int socket;
  int packets;
  struct sockaddr_in addr;
};
char buf[100];

void *send_thread(void *_args) {
  struct thread_ctx *ctx = (struct thread_ctx*)_args;
  for (unsigned i = 0; i < ctx->packets; i++) {
    int res = sendto(ctx->socket, buf, sizeof(buf), 0, (struct sockaddr *) &ctx->addr, sizeof(ctx->addr));
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
  pthread_t ids[PTHREAD_NUM];
  struct thread_ctx ctx = {};
  char *host = argv[1];
  ctx.packets = atoi(argv[2]);

  ctx.addr.sin_family = AF_INET;
  ctx.addr.sin_port = htons(1);

  if (inet_aton(host, &ctx.addr.sin_addr) == 0) {
    fprintf(stderr, "inet_aton() failed\n");
    return 1;
  }
  ctx.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (ctx.socket == -1) {
    perror("socket");
    return 1;
  }

  clock_t start = clock();

  for (unsigned i = 0; i < PTHREAD_NUM; i++) {
    pthread_create(&ids[i], NULL, send_thread, &ctx);
  }

  for (unsigned i = 0; i < PTHREAD_NUM; i++) {
    pthread_join(ids[i], NULL);
  }

  printf("{\"time\": %lf}\n", ((double)clock() - start)/CLOCKS_PER_SEC);
  close(ctx.socket);

  return 0;
}

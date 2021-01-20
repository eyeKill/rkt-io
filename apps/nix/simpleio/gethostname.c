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
  int calls;
};
char buf[4096 * 128] = {};

void *hostname_thread(void *_args) {
  struct thread_ctx *ctx = (struct thread_ctx*)_args;
  FILE *f = tmpfile();
  for (unsigned i = 0; i < ctx->calls; i++) {
    pwrite(fileno(f), buf, sizeof(buf), 0);
  }
  fclose(f);
  return NULL;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s calls", argv[0]);
    return 1;
  }
  #define PTHREAD_NUM 8
  struct thread_ctx contexts[PTHREAD_NUM] = {};
  int calls = atoi(argv[1]);

  printf("<results>\n");
  for (unsigned threads = 1; threads <= PTHREAD_NUM; threads++) {
    clock_t start = clock();
    for (unsigned i = 0; i < threads; i++) {
      struct thread_ctx *ctx = &contexts[i];
      contexts[i].calls = calls / threads;
      pthread_create(&ctx->id, NULL, hostname_thread, ctx);
    }

    for (unsigned i = 0; i < threads; i++) {

      struct thread_ctx *ctx = &contexts[i];
      pthread_join(ctx->id, NULL);
    }

    double total_time = ((double)clock() - start)/CLOCKS_PER_SEC;
    printf("{\"total_time\": %lf, \"calls\": %d, \"time_per_syscall\": %lf, \"threads\": %u}\n",
           total_time, calls, total_time / calls, threads);
  }
  printf("</results>\n");
  fflush(stdout);

  return 0;
}

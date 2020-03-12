#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

void *socket_test(void *unused) {
  char hostname[256];
  socket(AF_INET, SOCK_STREAM, 0);

  fprintf(stderr, "%s(): socket established: %s\n", __func__, hostname);
  return NULL;
}

int main() {
  int i;
  pthread_t ids[3];

  socket(AF_INET, SOCK_STREAM, 0);

  for (i = 0; i < 1; i++) {
    pthread_create(&ids[i], NULL, socket_test, NULL);
  }

  for (i = 0; i < 1; i++) {
    pthread_join(ids[i], NULL);
  }

  fprintf(stderr, "finished\n");
  return 0;
}

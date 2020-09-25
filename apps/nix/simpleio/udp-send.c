#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s host packets", argv[0]);
    return 1;
  }
  char *host = argv[1];
  int packets = atoi(argv[2]);
  struct sockaddr_in si_other;

  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = htons(1);

  if (inet_aton(host, &si_other.sin_addr) == 0) {
    fprintf(stderr, "inet_aton() failed\n");
    return 1;
  }
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == -1) {
    perror("socket");
    return 1;
  }

  char buf[256];

  clock_t start = clock();
  for (unsigned i = 0; i < packets; i++) {
    int res = sendto(s, buf, sizeof(buf), 0 , (struct sockaddr *) &si_other, sizeof(si_other));
    if (res == -1) {
      perror("sendto()");
      return 1;
    }
  }
  printf("{\"time\": %lf}\n", ((double)clock() - start)/CLOCKS_PER_SEC);

  return 0;
}

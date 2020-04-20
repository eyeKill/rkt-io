#define _GNU_SOURCE
#define _ISOC99_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/mman.h>


#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define PAGE_MASK (~(getpagesize()-1))
#define PAGE_ALIGN_DOWN(x) (((size_t)(x)) & PAGE_MASK)
#define BUF_SIZE (getpagesize() * 1024U)

int main(int argc, char** argv) {
  clock_t start;
  size_t bytes;
  size_t total_written = 0;
  int direct_io = 0;
  void *buf = NULL;
  int flags = O_WRONLY|O_TRUNC|O_CREAT;
  int fd = 0;

  if (argc < 4) {
    fprintf(stderr, "USAGE: %s file bytes direct_io\n", argv[0]);
    return 1;
  }

  bytes = PAGE_ALIGN_DOWN(atoll(argv[2]));

  direct_io = argv[3][0] == '1';

  if (direct_io) {
    int buf_fd = open("/tmp/foo", O_CREAT|O_TRUNC|O_RDWR, 0700);
    if (buf_fd < 0) {
      perror("open");
      return 1;
    }

    if (ftruncate(buf_fd, BUF_SIZE) < 0) {
      perror("ftruncate");
      return 1;
    }

    buf = mmap(NULL, BUF_SIZE, PROT_WRITE|PROT_READ, MAP_PRIVATE, buf_fd, 0);
    close(buf_fd);
    if (buf == MAP_FAILED) {
      perror("mmap");
      return 1;
    }
  } else {
    buf = malloc(BUF_SIZE);
    if (!buf) {
      perror("malloc");
      return 1;
    }
  }

  if (direct_io) {
    flags |= O_DIRECT;
  }
  fd = open(argv[1], flags, 0755);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  start = clock();
  memset(buf, 'a', BUF_SIZE);
  unsigned i = 0;
  while (total_written < bytes) {
    int to_write = MIN(BUF_SIZE, bytes - total_written);
    ssize_t written = write(fd, buf, to_write);
    if (written == -1) {
      perror("write");
      if (direct_io) {
        munmap(buf, BUF_SIZE);
      } else {
        free(buf);
      }
      return 1;
    }
    i++;
    if (i % 100 == 0) {
      clock_t current = clock();
      printf("Throughput: %lf MiB / s\n",
             (total_written / 1024 / 1024) / (((double) (current - start)) / CLOCKS_PER_SEC));
      total_written = 0;
      start = current;
    }
    total_written += written;
  }
  fsync(fd);
  if (direct_io) {
    munmap(buf, BUF_SIZE);
  } else {
    free(buf);
  }
  printf("Throughput: %lf MiB / s\n",
         (total_written / 1024 / 1024) / (((double) (clock() - start)) / CLOCKS_PER_SEC));
  return 0;
}

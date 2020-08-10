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
#include <errno.h>


#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define PAGE_MASK (~(getpagesize()-1))
#define PAGE_ALIGN_DOWN(x) (((size_t)(x)) & PAGE_MASK)
#define BUF_SIZE (getpagesize() * 128U)

int main(int argc, char** argv) {
  clock_t start, last_print;
  size_t bytes;
  size_t total_written = 0, written_since_print = 0;
  int direct_io = 0;
  void *buf = NULL;
  int flags = O_TRUNC|O_CREAT;
  int fd = 0;
  unsigned i = 0;
  unsigned do_read = 0;

  if (argc < 5) {
    fprintf(stderr, "USAGE: %s file bytes direct_io read\n", argv[0]);
    return 1;
  }

  bytes = PAGE_ALIGN_DOWN(atoll(argv[2]));

  direct_io = argv[3][0] == '1';

  do_read = argv[4][0] == '1';

  fprintf(stderr, "%s: %s %zu bytes\n", argv[0], do_read ? "read" : "write", bytes);

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
  if (do_read) {
    flags |= O_RDWR;
  } else {
    flags |= O_WRONLY;
  }
  fd = open(argv[1], flags, 0755);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  if (do_read) {
    if (ftruncate(fd, bytes) < 0) {
      perror("ftruncate");
      return 1;
    }
  }
  memset(buf, 'a', BUF_SIZE);
  start = clock();
  last_print = start;
  while (total_written < bytes) {
    int to_write = MIN(BUF_SIZE, bytes - total_written);
    ssize_t written;
    if (do_read) {
      written = read(fd, buf, to_write);
    } else {
      written = write(fd, buf, to_write);
    }

    if (written == -1) {
      perror("write");
      if (direct_io) {
        munmap(buf, BUF_SIZE);
      } else {
        free(buf);
      }
      close(fd);
      return 1;
    } else if (do_read && written == 0) { // EOF
      break;
    }
    i++;
    if (i % 100 == 0) {
      clock_t current = clock();
      fprintf(stderr, "[%.2lf%%] Throughput: %lf MiB / s\n",
             ((double)total_written/bytes * 100), (written_since_print / 1024 / 1024) / (((double) (current - last_print)) / CLOCKS_PER_SEC));
      written_since_print = 0;
      last_print = current;
    }
    total_written += written;
    written_since_print += written;
  }
  fprintf(stderr, "fsync()\n");
  fsync(fd);
  if (direct_io) {
    munmap(buf, BUF_SIZE);
  } else {
    free(buf);
  }

  fprintf(stderr, "Throughput: %lf MiB / s\n",
         (total_written / 1024 / 1024) / (((double) (clock() - start)) / CLOCKS_PER_SEC));
  printf("<result>\n");
  printf("{\"bytes\": %ld, \"time\": %lf}\n", total_written, ((double)clock() - start)/CLOCKS_PER_SEC);
  printf("</result>\n");
  fflush(stdout);
  return 0;
}

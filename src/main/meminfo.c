#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* Parse the contents of /proc/meminfo (in buf), return value of "name"
 * (example: MemTotal) */
static long get_entry(const char* name, const char* buf) {
    char* hit = strstr(buf, name);
    if (hit == NULL) {
      return -1;
    }

    errno = 0;
    long val = strtol(hit + strlen(name), NULL, 10);
    if (errno != 0) {
      perror("get_entry: strtol() failed");
      return -1;
    }
    return val;
}

int parse_hugetbl_size(size_t *hugetbl_size) {
    // we cannot get the size upfront. Let's hope this enough
    char buf[4096];
    int r = 0;
    int fd = 0;
    struct stat stat_buf;

    r = open("/proc/meminfo", O_RDONLY);
    if (r < 0) {
      perror("parse_hugetbl_size(): open(\"/proc/meminfo\") failed");
      goto cleanup;
    }
    fd = r;

    r = read(fd, buf, sizeof(buf));
    if (r < 0) {
      perror("parse_hugetbl_size(): read(\"/proc/meminfo\") failed");
      goto cleanup;
    }

    long size = get_entry("Hugetlb:", buf);
    if (size < 0) {
      fprintf(stderr, "%s() at %s:%d\n", __func__, __FILE__, __LINE__);
      return -ENOMEM;
    }
    // convert kb to bytes.
    *hugetbl_size = size * 1024;

    return 0;

 cleanup:
    if (fd) {
        close(fd);
    }
    return r;
}

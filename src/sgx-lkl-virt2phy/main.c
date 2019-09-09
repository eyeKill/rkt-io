#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define PFN_MASK_SIZE	8

void *virt2phy(int fd, const void *virtaddr) {
  int page_size = getpagesize();
  unsigned long virt_pfn = (unsigned long)virtaddr / page_size;
  off_t offset = sizeof(uint64_t) * virt_pfn;
  if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
    fprintf(stderr, "%s(): seek error in /proc/self/pagemap: %s\n",
            __func__, strerror(errno));
    return NULL;
  }

	uint64_t page;
	int retval = read(fd, &page, PFN_MASK_SIZE);
	if (retval < 0) {
		fprintf(stderr, "%s(): cannot read /proc/self/pagemap: %s\n",
				__func__, strerror(errno));
		return NULL;
	} else if (retval != PFN_MASK_SIZE) {
		fprintf(stderr, "%s(): read %d bytes from /proc/self/pagemap "
				"but expected %d:\n",
				__func__, retval, PFN_MASK_SIZE);
		return NULL;
	}

	/*
	 * the pfn (page frame number) are bits 0-54 (see
	 * pagemap.txt in linux Documentation)
	 */
	if ((page & 0x7fffffffffffffULL) == 0)
		return NULL;

	uint64_t physaddr = ((page & 0x7fffffffffffffULL) * page_size)
		+ ((unsigned long)virtaddr % page_size);
  return (void*)physaddr;
}

int main(int argc, char** argv) {
	int fd;
	int page_size;

  if (argc < 2) {
    fprintf(stderr, "USAGE: %s PID\n", argv[0]);
    return 1;
  }

  unsigned long pid = strtoul(argv[1], NULL, 0);

  char buf[256];
  int n = snprintf(buf, sizeof(buf), "/proc/%lu/pagemap", pid);

  if (n > sizeof(buf)) {
		fprintf(stderr, "%s(): buf too small\n", __func__);
    return 1;
  }

	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s(): cannot open %s: %s\n", __func__, buf, strerror(errno));
		return 1;
	}

  char * line = NULL;
  size_t len = 0;
  ssize_t read;

  while ((read = getline(&line, &len, stdin)) != -1) {
    const void *virtaddr = (void *)strtoul(line, NULL, 0);
    void *phyaddr = virt2phy(fd, virtaddr);
    if (!phyaddr) {
      close(fd);
      return 1;
    }
    printf("%ld\n", (uint64_t)phyaddr);
    fflush(stdout);
  }

  return 0;
}

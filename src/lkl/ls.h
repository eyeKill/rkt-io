#pragma once

// Debug helper function to list file system content

#include <stdio.h>
#include <stdlib.h>
#include <lkl_host.h>

static void listdir(const char* path) {
    int err;
    struct lkl_dir *dir = lkl_opendir(path, &err);

    fprintf(stderr, "listdir(\"%s\")\n", path);
    if (dir == NULL || err != 0) {
        fprintf(stderr, "Error: unable to opendir(%s)\n", dir);
        exit(err == 0 ? 1 : err);
    }

    struct lkl_linux_dirent64 *dirent = NULL;
    while ((dirent = lkl_readdir(dir)) != NULL) {
        fprintf(stderr, "%s\n", dirent->d_name);
    }
    err = lkl_errdir(dir);
    if (err != 0) {
        fprintf(stderr, "Error: lkl_readdir(%s) = %d\n", dir, err);
        exit(err);
    }

    lkl_closedir(dir);
}

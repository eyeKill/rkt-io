#include "linux/limits.h"
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>

extern char **environ;

#define DPDK_MP_SOCKET "/var/run/dpdk/spdk0/mp_socket"

int create_pipe(int fds[2], char parent_fd) {
    int r = pipe(fds);
    if (r < 0) {
        int saved_errno = errno;
        fprintf(stderr, "[userpci] pipe failed: %s\n", strerror(saved_errno));
        return -saved_errno;
    }
    if (fcntl(fds[parent_fd], F_SETFD, FD_CLOEXEC) == -1) {
        int saved_errno = errno;
        fprintf(stderr, "[userpci] failed to mark pipe fd as cloexec: %s", strerror(saved_errno));
        return -saved_errno;
    }
    return 0;
}

int spawn_lkl_userpci(int *pipe_fd)
{

    const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *fallback = "/tmp";
    const char *directory = NULL;
    char path_buf[PATH_MAX];

    /* try XDG path first, fall back to /tmp */
    if (xdg_runtime_dir != NULL) {
        directory = xdg_runtime_dir;
    } else {
        directory = fallback;
    }
    sprintf(path_buf, "%s/%s", directory, "spdk0/mp_socket");

    assert(pipe_fd);
    *pipe_fd = -1;
    int ready_fds[2], finished_fds[2];

    int r = create_pipe(ready_fds, 0);
    if (r < 0) {
      return r;
    }

    r = create_pipe(finished_fds, 1);
    if (r < 0) {
      return r;
    }

    // the idea is to unlink mp_socket and wait for dpdk-setuid-helper to re-bind it.
    r = unlink(path_buf);
    if (r != 0 && errno != ENOENT) {
        int saved_errno = errno;
        fprintf(stderr, "[userpci] Failed to remove %s: %s! Please remove it manually\n", path_buf, strerror(saved_errno));
        return -saved_errno;
    }

    char* prog = "sgx-lkl-userpci";
    char* argv[] = {prog, NULL /* ready_fd */, NULL /* finished_fd */, NULL /* uid */, NULL};
    char ready_fd_arg[255], finished_fd_arg[255], uid_arg[255];
    snprintf(ready_fd_arg, sizeof(ready_fd_arg), "%d", ready_fds[1]);
    argv[1] = ready_fd_arg;

    snprintf(finished_fd_arg, sizeof(finished_fd_arg), "%d", finished_fds[0]);
    argv[2] = finished_fd_arg;

    snprintf(uid_arg, sizeof(uid_arg), "%d", getuid());
    argv[3] = uid_arg;

    pid_t pid;
    r = posix_spawnp(&pid, "sgx-lkl-userpci", NULL, NULL, argv, environ);
    if (r != 0) {
        fprintf(stderr, "[userpci] failed to spawn dpdk-setuid-helper\n");
        return -r;
    }
    close(finished_fds[0]);
    close(ready_fds[1]);

    char ready_msg[3];
    r = read(ready_fds[0], ready_msg, sizeof(ready_msg));

    if (r != 3 && strcmp(ready_msg, "OK") != 0) {
      fprintf(stderr, "[userpci] sgx-lkl-userpci failed, returned: %s\n", ready_msg);
      while (1) {
        int status;
        pid_t wpid = waitpid(pid, &status, WNOHANG);
        if (wpid == -1) {
            fprintf(stderr, "[userpci] failed to wait for sgx-lkl-userpci\n");
            return -EPIPE;
        }
        if (wpid != pid) {
            continue;
        }

        if (WIFEXITED(status)) {
            fprintf(stderr, "[userpci] sgx-lkl-userpci exited with %d\n", WEXITSTATUS(status));
            return -EPIPE;
        }

        if (WIFSIGNALED(status)) {
            fprintf(stderr, "[userpci] sgx-lkl-userpci was killed by signal: %d\n", WTERMSIG(status));
            return -EPIPE;
        }
        usleep(100);
      }
    };
    return 0;
};

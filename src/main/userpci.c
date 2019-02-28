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

int test_socket(const char* path)
{
   int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
   if (sock < 0) {
       return sock;
   }

   struct sockaddr_un name;
   name.sun_family = AF_UNIX;
   strcpy(name.sun_path, path);

   /* Send message. */
   if (sendto(sock, "a", sizeof("a"), 0, (struct sockaddr *)&name,
              sizeof(struct sockaddr_un)) < 0) {
       close(sock);
       return 1;
   }
   close(sock);
   sleep(1);
   return 0;
}

#define DPDK_MP_SOCKET "/var/run/dpdk/spdk0/mp_socket"

int spawn_lkl_userpci(int *pipe_fd)
{
    // DPDK uses XDG_RUNTIME_DIR for unprivileged processes.
    // Since we want to use the same socket for both root and us,
    // we simply change our XDG_RUNTIME_DIR.
    assert(pipe_fd);
    *pipe_fd = -1;
    int pipefds[2];
    int r = pipe(pipefds);
    if (r < 0) {
        int saved_errno = errno;
        fprintf(stderr, "[userpci] pipe failed: %s\n", strerror(saved_errno));
        return -saved_errno;
    }
    if (fcntl(pipefds[1], F_SETFD, FD_CLOEXEC) == -1) {
        int saved_errno = errno;
        fprintf(stderr, "[userpci] failed to mark pipe fd as cloexec: %s", strerror(saved_errno));
        return -saved_errno;
    }

    if (putenv("XDG_RUNTIME_DIR=/var/run") != 0) {
        int saved_errno = errno;
        fprintf(stderr, "[userpci] putenv failed: %s\n", strerror(saved_errno));
        return -saved_errno;
    }

    // the idea is to unlink mp_socket and wait for dpdk-setuid-helper to re-bind it.
    r = unlink(DPDK_MP_SOCKET);
    if (r != 0 && errno != ENOENT) {
        int saved_errno = errno;
        fprintf(stderr, "[userpci] Failed to remove " DPDK_MP_SOCKET ": %s! Please remove it manually\n", strerror(saved_errno));
        return -saved_errno;
    }

    char* prog = "sgx-lkl-userpci";
    char* argv[] = {prog, NULL /* pipefd */, NULL /* uid */, "1" /* port-num */, NULL};
    char pipe_arg[255];
    snprintf(pipe_arg, sizeof(pipefds), "%d", pipefds[0]);
    argv[1] = pipe_arg;

    char uid_arg[255];
    snprintf(uid_arg, sizeof(uid_arg), "%d", getuid());
    argv[2] = uid_arg;

    pid_t pid;
    r = posix_spawnp(&pid, "sgx-lkl-userpci", NULL, NULL, argv, environ);
    if (r != 0) {
        fprintf(stderr, "[userpci] failed to spawn dpdk-setuid-helper\n");
        return -r;
    }

    while (1) {
        int r = test_socket(DPDK_MP_SOCKET);
        if (r == 0) {
            fprintf(stderr, "[userpci] " DPDK_MP_SOCKET " is ready\n");
            *pipe_fd = pipefds[1];
            return 0;
        } else if (r < 0) {
            fprintf(stderr, "[userpci] failed to test socket " DPDK_MP_SOCKET ": %s\n", strerror(-r));
            return r;
        }

        int status = 0;
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

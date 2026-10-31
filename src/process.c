#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

long long process_monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

int process_capture(char *const arguments[], unsigned int timeout_seconds, char *output, size_t output_size)
{
    int pipe_fds[2];
    pid_t child;
    size_t used = 0;
    int status = 0;
    int child_done = 0;
    int output_done = 0;
    long long deadline = process_monotonic_ms() + (long long)timeout_seconds * 1000LL;

    if (output_size > 0) output[0] = '\0';
    if (pipe(pipe_fds) != 0) return -1;
    child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        if (null_fd >= 0) dup2(null_fd, STDERR_FILENO);
        close(pipe_fds[1]);
        if (null_fd >= 0) close(null_fd);
        execvp(arguments[0], arguments);
        _exit(127);
    }

    close(pipe_fds[1]);
    fcntl(pipe_fds[0], F_SETFL, fcntl(pipe_fds[0], F_GETFL) | O_NONBLOCK);
    while (!child_done || !output_done) {
        struct pollfd descriptor = { .fd = pipe_fds[0], .events = POLLIN | POLLHUP };
        int wait_ms = 200;
        int polled;
        if (!child_done && process_monotonic_ms() >= deadline) {
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            child_done = 1;
            output_done = 1;
            break;
        }
        polled = poll(&descriptor, 1, wait_ms);
        if (polled > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[2048];
            ssize_t count;
            while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
                size_t copy = (size_t)count;
                if (output_size > 0 && used < output_size - 1) {
                    if (copy > output_size - 1 - used) copy = output_size - 1 - used;
                    memcpy(output + used, buffer, copy);
                    used += copy;
                    output[used] = '\0';
                }
            }
            if (count == 0) output_done = 1;
            else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) output_done = 1;
        }
        if (!child_done) {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) child_done = 1;
            else if (waited < 0 && errno != EINTR) {
                close(pipe_fds[0]);
                return -1;
            }
        }
        if (child_done && polled == 0) {
            char buffer[2048];
            ssize_t count;
            while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0 && output_size > 0 && used < output_size - 1) {
                size_t copy = (size_t)count;
                if (copy > output_size - 1 - used) copy = output_size - 1 - used;
                memcpy(output + used, buffer, copy);
                used += copy;
                output[used] = '\0';
            }
            if (count == 0) output_done = 1;
        }
    }
    close(pipe_fds[0]);
    if (!child_done) waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int process_tcp_probe(const char *host, int port, unsigned int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current;
    char port_text[16];
    int connected = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", port);
    if (getaddrinfo(host, port_text, &hints, &addresses) != 0) return 0;
    for (current = addresses; current != NULL && !connected; current = current->ai_next) {
        int descriptor = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        int flags;
        if (descriptor < 0) continue;
        flags = fcntl(descriptor, F_GETFL, 0);
        fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
        if (connect(descriptor, current->ai_addr, current->ai_addrlen) == 0) {
            connected = 1;
        } else if (errno == EINPROGRESS) {
            struct pollfd wait = { .fd = descriptor, .events = POLLOUT };
            if (poll(&wait, 1, (int)timeout_ms) > 0) {
                int socket_error = 0;
                socklen_t error_size = sizeof(socket_error);
                if (getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) == 0 && socket_error == 0) {
                    connected = 1;
                }
            }
        }
        close(descriptor);
    }
    freeaddrinfo(addresses);
    return connected;
}
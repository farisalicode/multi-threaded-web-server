#include "server.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define REQUEST_BUFFER_SIZE 8192
#define RESPONSE_BUFFER_SIZE 4096

static volatile sig_atomic_t stop_requested = 0;
static int listening_socket = -1;
static char document_root[PATH_MAX];

typedef struct {
    int client_fd;
} client_job_t;

static void handle_signal(int signo) {
    (void)signo;
    stop_requested = 1;

    if (listening_socket >= 0) {
        shutdown(listening_socket, SHUT_RDWR);
        close(listening_socket);
        listening_socket = -1;
    }
}

static const char *content_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)
        return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0)
        return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".txt") == 0)
        return "text/plain; charset=utf-8";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".gif") == 0)
        return "image/gif";

    return "application/octet-stream";
}

static int send_all(int fd, const void *buffer, size_t length) {
    const char *data = (const char *)buffer;
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }

    return 0;
}

static void send_error(int fd, int code, const char *reason, const char *message) {
    char body[1024];
    int body_len = snprintf(
        body, sizeof(body),
        "<!doctype html><html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1><p>%s</p></body></html>\n",
        code, reason, code, reason, message
    );

    char header[RESPONSE_BUFFER_SIZE];
    int header_len = snprintf(
        header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        code, reason, body_len
    );

    send_all(fd, header, (size_t)header_len);
    send_all(fd, body, (size_t)body_len);
}

static int safe_path(const char *request_path, char *output, size_t output_size) {
    if (!request_path || request_path[0] != '/') {
        return -1;
    }

    if (strstr(request_path, "..") != NULL) {
        return -1;
    }

    const char *relative = request_path + 1;
    if (*relative == '\0') {
        relative = "index.html";
    }

    int written = snprintf(output, output_size, "%s/%s", document_root, relative);
    if (written < 0 || (size_t)written >= output_size) {
        return -1;
    }

    return 0;
}

static void serve_file(int fd, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        send_error(fd, 404, "Not Found", "The requested resource was not found.");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        send_error(fd, 403, "Forbidden", "The requested resource is not a regular file.");
        return;
    }

    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        send_error(fd, 403, "Forbidden", "The requested resource could not be opened.");
        return;
    }

    char header[RESPONSE_BUFFER_SIZE];
    int header_len = snprintf(
        header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n\r\n",
        content_type(path), (long long)st.st_size
    );

    if (send_all(fd, header, (size_t)header_len) == 0) {
        char buffer[8192];
        ssize_t n;

        while ((n = read(file_fd, buffer, sizeof(buffer))) > 0) {
            if (send_all(fd, buffer, (size_t)n) != 0) {
                break;
            }
        }
    }

    close(file_fd);
}

static void process_client(void *arg) {
    client_job_t *job = (client_job_t *)arg;
    int client_fd = job->client_fd;
    free(job);

    char request[REQUEST_BUFFER_SIZE];
    ssize_t received = recv(client_fd, request, sizeof(request) - 1, 0);

    if (received <= 0) {
        close(client_fd);
        return;
    }

    request[received] = '\0';

    char method[16];
    char path[PATH_MAX];
    char version[16];

    if (sscanf(request, "%15s %4095s %15s", method, path, version) != 3) {
        send_error(client_fd, 400, "Bad Request", "Could not parse the HTTP request.");
        close(client_fd);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        send_error(client_fd, 405, "Method Not Allowed", "Only GET is supported.");
        close(client_fd);
        return;
    }

    char filesystem_path[PATH_MAX];
    if (safe_path(path, filesystem_path, sizeof(filesystem_path)) != 0) {
        send_error(client_fd, 403, "Forbidden", "Invalid request path.");
        close(client_fd);
        return;
    }

    serve_file(client_fd, filesystem_path);
    close(client_fd);
}

int run_server(const server_config_t *config) {
    if (!config || config->port < 1 || config->port > 65535 ||
        config->worker_count <= 0 || config->queue_size <= 0 ||
        !config->document_root) {
        return -1;
    }

    if (realpath(config->document_root, document_root) == NULL) {
        perror("realpath");
        return -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket < 0) {
        perror("socket");
        return -1;
    }

    int reuse = 1;
    setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)config->port);

    if (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(listening_socket);
        listening_socket = -1;
        return -1;
    }

    if (listen(listening_socket, config->queue_size) < 0) {
        perror("listen");
        close(listening_socket);
        listening_socket = -1;
        return -1;
    }

    thread_pool_t pool;
    if (thread_pool_init(&pool, config->worker_count, config->queue_size) != 0) {
        fprintf(stderr, "Failed to initialize thread pool.\n");
        close(listening_socket);
        listening_socket = -1;
        return -1;
    }

    printf("Server listening on http://localhost:%d\n", config->port);
    printf("Document root: %s\n", document_root);
    printf("Workers: %d\n", config->worker_count);
    printf("Press Ctrl+C to stop.\n");

    while (!stop_requested) {
        struct sockaddr_in client_address;
        socklen_t client_length = sizeof(client_address);

        int client_fd = accept(
            listening_socket,
            (struct sockaddr *)&client_address,
            &client_length
        );

        if (client_fd < 0) {
            if (stop_requested) break;
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        client_job_t *job = malloc(sizeof(*job));
        if (!job) {
            close(client_fd);
            continue;
        }

        job->client_fd = client_fd;

        if (thread_pool_submit(&pool, process_client, job) != 0) {
            free(job);
            close(client_fd);
            break;
        }
    }

    thread_pool_shutdown(&pool);
    thread_pool_destroy(&pool);

    if (listening_socket >= 0) {
        close(listening_socket);
        listening_socket = -1;
    }

    return 0;
}

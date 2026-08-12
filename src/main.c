#include "server.h"

#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *program) {
    fprintf(stderr,
        "Usage: %s [port] [worker_count] [document_root]\n"
        "Defaults: 8080 4 ./www\n",
        program);
}

int main(int argc, char **argv) {
    server_config_t config = {
        .port = 8080,
        .worker_count = 4,
        .queue_size = 32,
        .document_root = "./www"
    };

    if (argc > 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2) {
        config.port = atoi(argv[1]);
    }

    if (argc >= 3) {
        config.worker_count = atoi(argv[2]);
    }

    if (argc >= 4) {
        config.document_root = argv[3];
    }

    if (config.port < 1 || config.port > 65535 ||
        config.worker_count < 1) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return run_server(&config) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

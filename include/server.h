#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>

typedef struct {
    int port;
    int worker_count;
    int queue_size;
    const char *document_root;
} server_config_t;

int run_server(const server_config_t *config);

#endif

#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <semaphore.h>

#include "common.h"

#define MAX_THREADS 10

struct thread_context {
    struct rdma_cm_id *id;
    sem_t established;
    int ctx_index;
};

static struct thread_context g_ctxs[MAX_THREADS];

void *handle_request(void *arg) {
    struct thread_context *ctx = arg;

    struct connection *nc = NULL;
    IF_NULL_DIE(nc = setup_connection(ctx->id));

    struct rdma_conn_param conn_parm = {0};
    conn_parm.retry_count = 3;
    conn_parm.rnr_retry_count = 7;  // try infinity
    // Accept new connection
    IF_NZERO_DIE(rdma_accept(ctx->id, &conn_parm));

    // connection enter established state
    LOG("wait for connection to be established");
    sem_wait(&ctx->established);

    /*
    for (int i = 0; i < 10; i++) {
        sprintf(nc->send_buff, "hello %02d", i);
        LOGF("send: %s", nc->send_buff);
        if (rdma_post_send(nc->qp, nc->send_buff, strlen(nc->send_buff) + 1,
                           nc->send_mr, 0, NULL)) {
        }
    }
    */

    rdma_disconnect(ctx->id);
    rdma_destroy_id(ctx->id);

    g_ctxs[ctx->ctx_index].id = NULL;
    sem_destroy(&g_ctxs[ctx->ctx_index].established);

    return NULL;
}

void cm_event_loop(struct rdma_event_channel *ec, struct rdma_cm_id *listener) {
    int i, should_break;
    pthread_t client_thread;
    struct rdma_cm_event *event = NULL;

    while (1) {
        should_break = 0;
        IF_NZERO_DIE(rdma_get_cm_event(ec, &event));

        switch (event->event) {
            case RDMA_CM_EVENT_CONNECT_REQUEST:
                LOG("event: CONNECT REQUEST");
                for (i = 0; i < MAX_THREADS; i++) {
                    if (g_ctxs[i].id == NULL) break;
                }
                if (i == MAX_THREADS) {
                    LOG("Reached maximum number of clients");
                    rdma_reject(event->id, NULL, 0);
                    break;
                }
                g_ctxs[i].id = event->id;
                g_ctxs[i].ctx_index = i;
                sem_init(&g_ctxs[i].established, 0, 0);
                if (pthread_create(&client_thread, NULL, handle_request,
                                   &g_ctxs[i])) {
                    LOG("Failed to create thread");
                    rdma_reject(event->id, NULL, 0);
                    break;
                }
                break;
            case RDMA_CM_EVENT_ESTABLISHED:
                LOG("event: ESTABLISHED");
                for (i = 0; i < 10; i++) {
                    if (g_ctxs[i].id == event->id) {
                        sem_post(&g_ctxs[i].established);
                        break;
                    }
                }
                break;
            case RDMA_CM_EVENT_DISCONNECTED:
                LOG("event: DISCONNECTED");
                break;
            default:
                LOGF("event: %s", rdma_event_str(event->event));
                should_break = 1;
                break;
        }

        rdma_ack_cm_event(event);

        if (should_break) {
            break;
        }
    }
}

int main() {
    struct rdma_event_channel *ec = NULL;
    LOG("create event channel");
    IF_NULL_DIE(ec = rdma_create_event_channel());

    struct rdma_cm_id *listener = NULL;
    IF_NZERO_DIE(rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP));

    struct addrinfo *ai;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
    };

    IF_NZERO_DIE(getaddrinfo(NULL, PORT, &hints, &ai));
    IF_NZERO_DIE(rdma_bind_addr(listener, ai->ai_addr));
    LOG("listen begin");
    IF_NZERO_DIE(rdma_listen(listener, 10));
    freeaddrinfo(ai);

    for (int i = 0; i < 10; i++) {
        g_ctxs[i].id = NULL;
    }
    cm_event_loop(ec, listener);

    // cleanup
    rdma_destroy_id(listener);
    rdma_destroy_event_channel(ec);

    return 0;
}

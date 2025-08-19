#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/epoll.h>

#include "common.h"

static volatile int keep_running = 1;
static int epoll_fd = 0;

static void sigint_handle(int s) {
    (void)s;
    keep_running = 0;
}

#define MAX_EVENTS 16

enum conn_state {
    ACCEPTING,
    ESTABLISHED,
    DISCONNECTED,
};

struct conn_context {
    struct rdma_cm_id *id;
    struct ibv_comp_channel *cc;
    struct connection *conn;
    enum conn_state state;
};

void handle_new_request(void *arg) {
    struct conn_context *cctx = arg;

    struct connection *nc = NULL;
    IF_NULL_DIE(nc = setup_connection(cctx->id));
    cctx->conn = nc;

    // register cq event fd
    struct epoll_event ev;
    int comp_fd = nc->cc->fd;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLERR;
    ev.data.ptr = nc->cc;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, comp_fd, &ev))
        die("Failed to register cq event fd");

    struct rdma_conn_param conn_parm = {0};
    conn_parm.retry_count = 3;
    conn_parm.rnr_retry_count = 7;  // try infinity
    // Accept new connection
    IF_NZERO_DIE(rdma_accept(cctx->id, &conn_parm));

    cctx->state = ACCEPTING;
}

int handle_cm_event(struct rdma_event_channel *ec) {
    struct rdma_cm_event new_event, *event = NULL;

    if (rdma_get_cm_event(ec, &event) != 0) {
        LOG("rdma_get_cm_event failed");
        return -1;
    }

    new_event = *event;
    rdma_ack_cm_event(event);

    struct conn_context *cctx = NULL;

    switch (new_event.event) {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            LOG("event: CONNECT REQUEST");
            cctx = malloc(sizeof(*cctx));
            if (cctx == NULL) {
                LOG("Failed to alloc conn_context");
                rdma_reject(new_event.id, NULL, 0);
                break;
            }
            cctx->id = new_event.id;
            new_event.id->context = cctx;
            handle_new_request(cctx);
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            LOG("event: ESTABLISHED");
            cctx = new_event.id->context;
            cctx->state = ESTABLISHED;
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            LOG("event: DISCONNECTED");
            cctx = new_event.id->context;
            if (cctx == NULL) break;
            cctx->state = DISCONNECTED;
            rdma_disconnect(cctx->id);
            rdma_destroy_id(cctx->id);

            struct connection *nc = cctx->conn;
            if (nc->cc) ibv_destroy_comp_channel(nc->cc);
            if (nc->qp) ibv_destroy_qp(nc->qp);
            if (nc->cq) ibv_destroy_cq(nc->cq);
            if (nc->send_mr) ibv_dereg_mr(nc->send_mr);
            if (nc->recv_mr) ibv_dereg_mr(nc->recv_mr);
            if (nc->send_buff) free(nc->send_buff);
            if (nc->recv_buff) free(nc->recv_buff);
            if (nc->pd) ibv_dealloc_pd(nc->pd);
            free(nc);

            new_event.id->context = NULL;
            free(cctx);
            break;

        default:
            LOGF("event: %s", rdma_event_str(event->event));
            break;
    }
    return 0;
}

int handle_cq_event(struct ibv_comp_channel *cc) {
    // LOG("handle_cq_event");

    struct ibv_cq *cq = NULL;
    void *cq_ctx = NULL;

    if (ibv_get_cq_event(cc, &cq, &cq_ctx)) {
        LOG("ibv_get_cq_event failed");
        return -1;
    }

    ibv_ack_cq_events(cq, 1);
    if (ibv_req_notify_cq(cq, 0)) {
        LOG("ibv_get_cq_event failed");
        return -1;
    }

    // poll completions
    struct ibv_wc wcs[16];
    int ne = 0;
    do {
        ne = ibv_poll_cq(cq, 16, wcs);
        if (ne < 0) {
            LOG("ibv_poll_cq failed");
            break;
        } else if (ne == 0) {
            break;
        }

        struct ibv_recv_wr *bad_rwr = NULL;
        struct ibv_send_wr *bad_swr = NULL;
        for (int i = 0; i < ne; i++) {
            struct ibv_wc *wc = &wcs[i];
            if (wc->status != IBV_WC_SUCCESS) {
                LOGF("WC error %s opcode=%d wr_id=%lu\n",
                     ibv_wc_status_str(wc->status), wc->opcode, wc->wr_id);
            }

            struct connection *nc = NULL;
            IF_NULL_DIE(nc = (struct connection *)(wc->wr_id));

            switch (wc->opcode) {
                case IBV_WC_SEND:
                    // send completed
                    break;
                case IBV_WC_RECV:
                    LOGF("Recevied: %s\n", nc->recv_buff);

                    strcpy(nc->send_buff, nc->recv_buff);

                    // post recv wr after we handled the received message.
                    IF_NZERO_DIE(ibv_post_recv(nc->qp, &nc->recv_wr, &bad_rwr));

                    IF_NZERO_DIE(ibv_post_send(nc->qp, &nc->send_wr, &bad_swr));
                    break;
                default:
                    LOGF("Unknown opcode: %s", wc_opcode_str(wc->opcode));
                    break;
            }
        }
    } while (ne > 0);

    return 0;
}

int main() {
    signal(SIGINT, sigint_handle);

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
    freeaddrinfo(ai);

    LOG("listen begin");
    IF_NZERO_DIE(rdma_listen(listener, 10));

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) die("Failed to create epoll fd");

    // register cm event fd
    struct epoll_event ev;
    int listen_fd = ec->fd;
    ev.events = EPOLLIN;
    ev.data.ptr = ec;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev))
        die("Failed to register listen fd");

    // main loop
    struct epoll_event events[MAX_EVENTS];
    while (keep_running) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        } else if (n == 0) {
            // LOG("epoll no event");
            //  timeout
            continue;
        }

        // LOGF("epoll got %d events\n", n);
        for (int i = 0; i < n; i++) {
            void *ptr = events[i].data.ptr;
            if (ptr == ec) {
                handle_cm_event(ec);
            } else {
                handle_cq_event(ptr);
            }
        }
    }

    // cleanup
    rdma_destroy_id(listener);
    rdma_destroy_event_channel(ec);
    if (epoll_fd > 0) close(epoll_fd);

    return 0;
}

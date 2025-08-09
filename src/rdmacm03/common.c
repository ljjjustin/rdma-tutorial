#include "common.h"

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

void die(const char *reason) {
    perror(reason);
    exit(EXIT_FAILURE);
}

void timestr(char *buffer) {
    char time_buf[64];
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm *tm_info;
    tm_info = localtime(&tv.tv_sec);

    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    sprintf(buffer, "%s.%06ld", time_buf, tv.tv_usec);
}

void LOG(const char *msg) {
    char buffer[64];
    timestr(buffer);
    fprintf(stdout, "%s %s\n", buffer, msg);
}

void LOGF(const char *format, ...) {
    char buffer[64];
    timestr(buffer);
    fprintf(stdout, "%s ", buffer);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

char *get_event_string(enum rdma_cm_event_type type) {
    switch (type) {
        case RDMA_CM_EVENT_ESTABLISHED:
            return "RDMA_CM_EVENT_ESTABLISHED";
        case RDMA_CM_EVENT_DISCONNECTED:
            return "RDMA_CM_EVENT_DISCONNECTED";
        case RDMA_CM_EVENT_DEVICE_REMOVAL:
            return "RDMA_CM_EVENT_DEVICE_REMOVAL";
        case RDMA_CM_EVENT_CONNECT_ERROR:
            return "RDMA_CM_EVENT_CONNECT_ERROR";
        case RDMA_CM_EVENT_REJECTED:
            return "RDMA_CM_EVENT_REJECTED";
        case RDMA_CM_EVENT_UNREACHABLE:
            return "RDMA_CM_EVENT_UNREACHABLE";
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            return "RDMA_CM_EVENT_CONNECT_REQUEST";
        default:
            return "Unknown event";
    }
}

void check_cm_event(struct rdma_cm_event *event, enum rdma_cm_event_type exp) {
    if (event->event != exp) {
        LOGF("Unexpected event: %s, expect event is: %s",
             get_event_string(event->event), get_event_string(exp));
        die("");
    }
}

struct connection *setup_connection(struct rdma_cm_id *cm_id) {
    struct connection *nc = NULL;

    IF_NULL_DIE(nc = (struct connection *)malloc(sizeof(*nc)));
    nc->ctx = cm_id->verbs;
    IF_NULL_DIE(nc->pd = ibv_alloc_pd(cm_id->verbs));
    IF_NULL_DIE(nc->cc = ibv_create_comp_channel(cm_id->verbs));
    IF_NULL_DIE(nc->cq = ibv_create_cq(cm_id->verbs, 10, NULL, nc->cc, 0));
    IF_NZERO_DIE(ibv_req_notify_cq(nc->cq, 0));

    // create qp
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.send_cq = nc->cq;
    qp_attr.recv_cq = nc->cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    IF_NZERO_DIE(rdma_create_qp(cm_id, nc->pd, &qp_attr));
    nc->qp = cm_id->qp;

    // alloc and register mr
    IF_NULL_DIE(nc->recv_buff = malloc(BUFFER_SIZE));
    IF_NULL_DIE(nc->send_buff = malloc(BUFFER_SIZE));
    IF_NULL_DIE(nc->recv_mr = ibv_reg_mr(
                    nc->pd, nc->recv_buff, BUFFER_SIZE,
                    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    IF_NULL_DIE(nc->send_mr = ibv_reg_mr(
                    nc->pd, nc->send_buff, BUFFER_SIZE,
                    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    // setup wr
    nc->recv_sge.addr = (uintptr_t)nc->recv_buff;
    nc->recv_sge.length = BUFFER_SIZE;
    nc->recv_sge.lkey = nc->recv_mr->lkey;
    nc->recv_wr.sg_list = &nc->recv_sge;
    nc->recv_wr.num_sge = 1;
    nc->recv_wr.next = NULL;

    nc->send_sge.addr = (uintptr_t)nc->send_buff;
    nc->send_sge.length = BUFFER_SIZE;
    nc->send_sge.lkey = nc->send_mr->lkey;
    nc->send_wr.opcode = IBV_WR_SEND;
    nc->send_wr.send_flags = IBV_SEND_SIGNALED;
    nc->send_wr.sg_list = &nc->send_sge;
    nc->send_wr.num_sge = 1;
    nc->send_wr.next = NULL;

    struct ibv_recv_wr *bad_rwr = NULL;
    IF_NZERO_DIE(ibv_post_recv(nc->qp, &nc->recv_wr, &bad_rwr));

    return nc;
}

#include "common.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <server_ip>\n", argv[0]);
        exit(1);
    }

    struct rdma_event_channel *ec = NULL;
    IF_NULL_DIE(ec = rdma_create_event_channel());

    struct rdma_cm_id *conn = NULL;
    IF_NZERO_DIE(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
    LOG("created id");

    struct addrinfo *ai;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    IF_NZERO_DIE(getaddrinfo(argv[1], PORT, &hints, &ai));

    // resolve ip addr
    IF_NZERO_DIE(rdma_resolve_addr(conn, NULL, ai->ai_addr, 2000));
    freeaddrinfo(ai);
    struct rdma_cm_event *event;
    IF_NZERO_DIE(rdma_get_cm_event(ec, &event));
    check_cm_event(event, RDMA_CM_EVENT_ADDR_RESOLVED);
    rdma_ack_cm_event(event);
    LOG("addr resolved");

    // resolve route
    IF_NZERO_DIE(rdma_resolve_route(conn, 2000));
    IF_NZERO_DIE(rdma_get_cm_event(ec, &event));
    check_cm_event(event, RDMA_CM_EVENT_ROUTE_RESOLVED);
    rdma_ack_cm_event(event);
    LOG("route resolved");

    // allocate resources
    struct connection *nc = NULL;
    IF_NULL_DIE(nc = setup_connection(conn));

    // connect server
    LOG("connect to server");
    struct rdma_conn_param conn_param = {0};
    conn_param.retry_count = 3;
    conn_param.rnr_retry_count = 7;  // try infinity
    IF_NZERO_DIE(rdma_connect(conn, &conn_param));
    IF_NZERO_DIE(rdma_get_cm_event(ec, &event));
    check_cm_event(event, RDMA_CM_EVENT_ESTABLISHED);
    rdma_ack_cm_event(event);

    LOG("enter ESTABLISHED");

    // send & recv msg
    int i, ret;
    struct ibv_wc wc;
    struct ibv_recv_wr *bad_rwr = NULL;
    struct ibv_send_wr *bad_swr = NULL;

    for (i = 0; i < 10; i++) {
        sprintf(nc->send_buff, "msg-%02d: hello", i);
        IF_NZERO_DIE(ibv_post_send(nc->qp, &nc->send_wr, &bad_swr));

        do {
            ret = ibv_poll_cq(nc->cq, 1, &wc);
        } while (ret == 0);
        if (ret < 0) {
            die("ibv_poll_cq");
        }
        if (wc.status != IBV_WC_SUCCESS) {
            LOGF("WC error: %s\n", ibv_wc_status_str(wc.status));
            break;
        }
        if (wc.opcode == IBV_WC_SEND) {
            LOGF("sent: %s\n", nc->send_buff);
        } else {
            die("Unexpected opcode");
        }
        ret = ibv_req_notify_cq(nc->cq, 0);
        if (ret) break;

        do {
            ret = ibv_poll_cq(nc->cq, 1, &wc);
        } while (ret == 0);
        if (ret < 0) {
            die("ibv_poll_cq");
        }
        if (wc.status != IBV_WC_SUCCESS) {
            LOGF("WC error: %s\n", ibv_wc_status_str(wc.status));
            break;
        }
        if (wc.opcode == IBV_WC_RECV) {
            LOGF("received: %s\n", nc->recv_buff);
            // post recv wr in order to receive next message
            IF_NZERO_DIE(ibv_post_recv(nc->qp, &nc->recv_wr, &bad_rwr));
        } else {
            die("Unexpected opcode");
        }
        ret = ibv_req_notify_cq(nc->cq, 0);
        if (ret) break;
    }
    // cleanup
    rdma_disconnect(conn);
    rdma_destroy_id(conn);
    rdma_destroy_event_channel(ec);

    return 0;
}

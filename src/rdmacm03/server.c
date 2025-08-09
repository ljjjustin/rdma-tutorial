#include "common.h"

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

    struct rdma_cm_event *event = NULL;
    IF_NZERO_DIE(rdma_get_cm_event(ec, &event));
    check_cm_event(event, RDMA_CM_EVENT_CONNECT_REQUEST);
    LOG("event: CONNECT REQUEST");
    struct rdma_cm_id *client_id = event->id;
    rdma_ack_cm_event(event);

    struct connection *nc = NULL;
    IF_NULL_DIE(nc = setup_connection(client_id));

    struct rdma_conn_param conn_parm = {0};
    conn_parm.retry_count = 3;
    conn_parm.rnr_retry_count = 7;  // try infinity
    // Accept new connection
    IF_NZERO_DIE(rdma_accept(client_id, &conn_parm));

    // connection enter established state
    IF_NZERO_DIE(rdma_get_cm_event(ec, &event));
    check_cm_event(event, RDMA_CM_EVENT_ESTABLISHED);
    LOG("event: CONNECT ESTABLISHED");
    rdma_ack_cm_event(event);

    LOG("Client connected");

    int ret, i = 0;
    struct ibv_wc wc;
    struct ibv_recv_wr *bad_rwr = NULL;
    struct ibv_send_wr *bad_swr = NULL;

    while (i < 10) {
        do {
            // poll recv queue
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
            strcpy(nc->send_buff, nc->recv_buff);
            // post recv wr after we handled the received message.
            IF_NZERO_DIE(ibv_post_recv(nc->qp, &nc->recv_wr, &bad_rwr));

            IF_NZERO_DIE(ibv_post_send(nc->qp, &nc->send_wr, &bad_swr));
        } else if (wc.opcode == IBV_WC_SEND) {
            LOGF("sent: %s\n", nc->send_buff);
            i++;
        } else {
            LOG("Unexpected opcode");
        }
        ret = ibv_req_notify_cq(nc->cq, 0);
        if (ret) break;
    }

    // cleanup
    rdma_disconnect(client_id);
    rdma_destroy_id(client_id);
    rdma_destroy_id(listener);
    rdma_destroy_event_channel(ec);

    return 0;
}

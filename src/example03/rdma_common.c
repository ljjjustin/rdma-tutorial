#include "rdma_common.h"

// 这个文件包含了 server.c 和 client.c 共享的通用函数

int build_rdma_resources(struct rdma_context *res) {
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev = NULL;

    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) die("ibv_get_device_list failed");

    ib_dev = dev_list[0];
    if (!ib_dev) die("No IB devices found");

    res->ctx = ibv_open_device(ib_dev);
    if (!res->ctx) die("ibv_open_device failed");

    ibv_free_device_list(dev_list);

    if (ibv_query_port(res->ctx, res->ib_port, &res->port_attr)) {
        die("ibv_query_port failed");
    }

    if (ibv_query_gid(res->ctx, res->ib_port, 0, &res->gid)) {
        die("ibv_query_gid failed");
    }

    res->pd = ibv_alloc_pd(res->ctx);
    if (!res->pd) die("ibv_alloc_pd failed");

    res->buf = (char *)malloc(RDMA_BUFFER_SIZE);
    if (!res->buf) die("malloc failed");
    memset(res->buf, 0, RDMA_BUFFER_SIZE);

    res->mr = ibv_reg_mr(res->pd, res->buf, RDMA_BUFFER_SIZE,
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!res->mr) die("ibv_reg_mr failed");

    res->cq = ibv_create_cq(res->ctx, 10, NULL, NULL, 0);
    if (!res->cq) die("ibv_create_cq failed");

    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = res->cq,
        .recv_cq = res->cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 10,
            .max_recv_wr = 10,
            .max_send_sge = 1,
            .max_recv_sge = 1,
        }
    };
    res->qp = ibv_create_qp(res->pd, &qp_init_attr);
    if (!res->qp) die("ibv_create_qp failed");

    return 0;
}

int setup_qp_state(struct rdma_context *res, struct qp_conn_info *remote_info) {
    // 1. RESET -> INIT
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_INIT,
        .pkey_index = 0,
        .port_num = res->ib_port,
        .qp_access_flags = IBV_ACCESS_REMOTE_WRITE
    };
    if (ibv_modify_qp(res->qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        die("Failed to modify QP to INIT");
    }

    // 2. INIT -> RTR (Ready to Receive)
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = res->port_attr.active_mtu;
    attr.dest_qp_num = remote_info->qp_num;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.grh.dgid = *(union ibv_gid*)remote_info->gid;
    attr.ah_attr.grh.flow_label = 0;
    attr.ah_attr.grh.sgid_index = 0;
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.dlid = remote_info->lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = res->ib_port;

    if (ibv_modify_qp(res->qp, &attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
        die("Failed to modify QP to RTR");
    }

    // 3. RTR -> RTS (Ready to Send)
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 0;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.max_rd_atomic = 1;

    if (ibv_modify_qp(res->qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC)) {
        die("Failed to modify QP to RTS");
    }

    return 0;
}

int post_receive(struct rdma_context *res) {
    struct ibv_recv_wr rr;
    struct ibv_sge sge;
    struct ibv_recv_wr *bad_wr;

    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)res->buf;
    sge.length = RDMA_BUFFER_SIZE;
    sge.lkey = res->mr->lkey;

    memset(&rr, 0, sizeof(rr));
    rr.wr_id = 0; // Can be used to identify this work request
    rr.sg_list = &sge;
    rr.num_sge = 1;

    return ibv_post_recv(res->qp, &rr, &bad_wr);
}

int post_send(struct rdma_context *res, int len) {
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr;

    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)res->buf;
    sge.length = len;
    sge.lkey = res->mr->lkey;

    memset(&sr, 0, sizeof(sr));
    sr.wr_id = 1; // Can be used to identify this work request
    sr.opcode = IBV_WR_SEND;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.send_flags = IBV_SEND_SIGNALED;

    return ibv_post_send(res->qp, &sr, &bad_wr);
}

void cleanup_resources(struct rdma_context *res) {
    if (res->qp) ibv_destroy_qp(res->qp);
    if (res->cq) ibv_destroy_cq(res->cq);
    if (res->mr) ibv_dereg_mr(res->mr);
    if (res->pd) ibv_dealloc_pd(res->pd);
    if (res->ctx) ibv_close_device(res->ctx);
    if (res->buf) free(res->buf);
}

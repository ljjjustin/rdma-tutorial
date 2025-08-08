#include "rdma_common.h"

int main() {
    struct rdma_context res = { .ib_port = 1 };
    struct qp_conn_info local_info, remote_info;

    printf("Starting server...\n");

    // 1. 创建RDMA资源
    if (build_rdma_resources(&res)) {
        die("Failed to build RDMA resources");
    }

    // 2. 设置TCP服务器用于连接信息交换
    int sock_fd = -1, client_fd = -1;
    struct sockaddr_in server_addr;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) die("socket creation failed");

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        die("bind failed");
    }
    if (listen(sock_fd, 1) < 0) die("listen failed");

    printf("Waiting for a client to connect on TCP port %d...\n", TCP_PORT);
    client_fd = accept(sock_fd, NULL, NULL);
    if (client_fd < 0) die("accept failed");
    printf("Client connected.\n");

    // 3. 交换QP信息
    local_info.qp_num = htonl(res.qp->qp_num);
    local_info.lid = htons(res.port_attr.lid);
    memcpy(local_info.gid, &res.gid, 16);

    printf("Local QP info: QPN=0x%x, LID=0x%x\n", res.qp->qp_num, res.port_attr.lid);

    if (write(client_fd, &local_info, sizeof(local_info)) != sizeof(local_info)) {
        die("Failed to send local QP info");
    }

    if (read(client_fd, &remote_info, sizeof(remote_info)) != sizeof(remote_info)) {
        die("Failed to receive remote QP info");
    }

    remote_info.qp_num = ntohl(remote_info.qp_num);
    remote_info.lid = ntohs(remote_info.lid);

    printf("Remote QP info: QPN=0x%x, LID=0x%x\n", remote_info.qp_num, remote_info.lid);

    // 4. 设置QP状态
    if (setup_qp_state(&res, &remote_info)) {
        die("Failed to set up QP state");
    }
    printf("QP state is now RTS (Ready to Send).\n");

    // 关闭TCP连接，不再需要
    close(client_fd);
    close(sock_fd);

    // 5. Echo循环
    printf("Starting echo loop...\n");
    while (1) {
        // 首先投递一个接收请求
        if (post_receive(&res)) {
            die("post_receive failed");
        }

        struct ibv_wc wc;
        int n;
        // 等待接收完成
        do {
            n = ibv_poll_cq(res.cq, 1, &wc);
        } while (n == 0);

        if (n < 0 || wc.status != IBV_WC_SUCCESS) {
            die("poll_cq failed on receive");
        }

        if (wc.opcode == IBV_WC_RECV) {
            printf("Received message: '%s' (length: %d)\n", res.buf, wc.byte_len);

            // 回显消息
            printf("Echoing message back...\n");
            if (post_send(&res, wc.byte_len)) {
                die("post_send failed");
            }

            // 等待发送完成
            do {
                n = ibv_poll_cq(res.cq, 1, &wc);
            } while (n == 0);

            if (n < 0 || wc.status != IBV_WC_SUCCESS) {
                die("poll_cq failed on send");
            }
            printf("Echo sent successfully.\n\n");
        }
    }

    // 6. 清理资源
    cleanup_resources(&res);
    return 0;
}

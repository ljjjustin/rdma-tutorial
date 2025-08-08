#include "rdma_common.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
        return 1;
    }

    struct rdma_context res = { .ib_port = 1 };
    struct qp_conn_info local_info, remote_info;
    char *server_ip = argv[1];

    printf("Starting client...\n");

    // 1. 创建RDMA资源
    if (build_rdma_resources(&res)) {
        die("Failed to build RDMA resources");
    }

    // 2. 设置TCP客户端
    int sock_fd = -1;
    struct sockaddr_in server_addr;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) die("socket creation failed");

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        die("inet_pton failed");
    }

    printf("Connecting to server at %s on TCP port %d...\n", server_ip, TCP_PORT);
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        die("connect failed");
    }
    printf("Connected to server.\n");

    // 3. 交换QP信息
    if (read(sock_fd, &remote_info, sizeof(remote_info)) != sizeof(remote_info)) {
        die("Failed to receive remote QP info");
    }

    remote_info.qp_num = ntohl(remote_info.qp_num);
    remote_info.lid = ntohs(remote_info.lid);
    printf("Remote QP info: QPN=0x%x, LID=0x%x\n", remote_info.qp_num, remote_info.lid);

    local_info.qp_num = htonl(res.qp->qp_num);
    local_info.lid = htons(res.port_attr.lid);
    memcpy(local_info.gid, &res.gid, 16);
    printf("Local QP info: QPN=0x%x, LID=0x%x\n", res.qp->qp_num, res.port_attr.lid);

    if (write(sock_fd, &local_info, sizeof(local_info)) != sizeof(local_info)) {
        die("Failed to send local QP info");
    }

    // 4. 设置QP状态
    if (setup_qp_state(&res, &remote_info)) {
        die("Failed to set up QP state");
    }
    printf("QP state is now RTS (Ready to Send).\n");

    // 关闭TCP连接
    close(sock_fd);

    // 5. 发送-接收循环
    int msg_count = 0;
    while (1) {
        // 准备要发送的消息
        int len = snprintf(res.buf, RDMA_BUFFER_SIZE, "Message from client #%d", msg_count);
        printf("\nSending message: '%s'\n", res.buf);

        // 必须先投递接收请求，以准备接收服务器的回显
        if (post_receive(&res)) {
            die("post_receive failed");
        }

        // 发送消息
        if (post_send(&res, len + 1)) { // +1 for null terminator
            die("post_send failed");
        }

        // 等待发送和接收都完成 (需要2个完成事件)
        int completions = 0;
        while (completions < 2) {
            struct ibv_wc wc[2];
            int n = ibv_poll_cq(res.cq, 2, wc);
            if (n < 0) {
                die("poll_cq failed");
            }
            for (int i = 0; i < n; i++) {
                if (wc[i].status != IBV_WC_SUCCESS) {
                    die("Work completion status is not success");
                }
                if (wc[i].wr_id == 0) { // 0 is for receive
                    printf("Received echo: '%s'\n", res.buf);
                }
            }
            completions += n;
        }

        msg_count++;
        sleep(1);
    }

    // 6. 清理资源
    cleanup_resources(&res);
    return 0;
}

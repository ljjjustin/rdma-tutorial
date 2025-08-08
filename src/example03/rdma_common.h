#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <netdb.h>

// 如果系统没有定义 be64toh, 自己实现一个
#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return __bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return __bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

#define TCP_PORT (19875)
#define RDMA_BUFFER_SIZE (1024)

// 用于保存所有RDMA相关的上下文
struct rdma_context {
    struct ibv_context      *ctx;
    struct ibv_pd           *pd;
    struct ibv_mr           *mr;
    struct ibv_cq           *cq;
    struct ibv_qp           *qp;
    struct ibv_port_attr    port_attr;
    union ibv_gid           gid;
    char                    *buf;
    int                     ib_port;
};

// 用于通过TCP交换的QP信息
struct qp_conn_info {
    uint32_t qp_num;
    uint16_t lid;
    uint8_t  gid[16];
} __attribute__((packed));


// 帮助函数，用于处理错误
static void die(const char *reason) {
    fprintf(stderr, "Error: %s\n", reason);
    exit(EXIT_FAILURE);
}

// 在 rdma_common.c 中实现的公共函数
int build_rdma_resources(struct rdma_context *res);
int setup_qp_state(struct rdma_context *res, struct qp_conn_info *remote_info);
int post_receive(struct rdma_context *res);
int post_send(struct rdma_context *res, int len);
void cleanup_resources(struct rdma_context *res);

#endif // RDMA_COMMON_H

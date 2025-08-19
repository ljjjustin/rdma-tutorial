#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <errno.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define PORT "20079"

#define IF_NZERO_DIE(x)                                         \
    do {                                                        \
        if ((x)) {                                              \
            die("ERROR: " #x " failed, return value not zero"); \
        }                                                       \
    } while (0)

#define IF_NULL_DIE(x)                                         \
    do {                                                       \
        if (!(x)) {                                            \
            die("ERROR: " #x " failed, return value is NULL"); \
        }                                                      \
    } while (0)

struct connection {
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_comp_channel *cc;
    struct ibv_cq *cq;
    struct ibv_qp *qp;

    char *recv_buff;
    char *send_buff;
    struct ibv_mr *recv_mr;
    struct ibv_mr *send_mr;
    struct ibv_sge recv_sge;
    struct ibv_sge send_sge;
    struct ibv_recv_wr recv_wr;
    struct ibv_send_wr send_wr;
};

void die(const char *reason);
void LOG(const char *msg);
void LOGF(const char *format, ...);
void check_cm_event(struct rdma_cm_event *, enum rdma_cm_event_type);
const char *wc_opcode_str(enum ibv_wc_opcode);
struct connection *setup_connection(struct rdma_cm_id *cm_id);

#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <infiniband/verbs.h>

int main(int argc, char *argv[]) {
    int i, ret, port, ib_dev_num;
    char *ib_dev_name;
    const char *name;
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev;
    struct ibv_context *ctx;
    struct ibv_device_attr_ex ib_attr = {};
    struct ibv_port_attr port_attr = {};

    if (argc != 2) {
        printf("Usage: %s <ib dev name>\n", argv[0]);
        exit(-1);
    }
    ib_dev_name = argv[1];
    ib_dev = NULL;

    dev_list = ibv_get_device_list(&ib_dev_num);

    if (!dev_list) {
        printf("Failed to get IB device list");
        exit(-1);
    }

    for (i=0; i < ib_dev_num; ++i) {
        name = ibv_get_device_name(dev_list[i]);
        if (!strcmp(name, ib_dev_name)) {
            ib_dev = dev_list[i];
            break;
        }
    }

    if (ib_dev == NULL) {
        goto out;
    }

    printf("%-16s\tnode GUID\n", "device");
    printf("%-16s\t---------------\n", "------");
    printf("%-16s\t%016llx\n",
            ibv_get_device_name(ib_dev),
            (unsigned long long)be64toh(ibv_get_device_guid(ib_dev)));

    ctx = ibv_open_device(ib_dev);
    if (ctx == NULL) {
        goto out;
    }

    ret = ibv_query_device_ex(ctx, NULL, &ib_attr);
    if (ret != 0) {
        printf("Failed to query device props\n");
        goto close;
    }
    printf("transport:\t\t (%d)\n", ib_dev->transport_type);
    printf("node_guid:\t\t(%lld)\n", ib_attr.orig_attr.node_guid);
    printf("phys_port_cnt:\t\t(%d)\n", ib_attr.orig_attr.phys_port_cnt);
    printf("max_qp:\t\t\t(%d)\n", ib_attr.orig_attr.max_qp);

    for (port = 1; port <= ib_attr.orig_attr.phys_port_cnt; ++port) {
        ret = ibv_query_port(ctx, port, &port_attr);
        if (ret != 0) {
            printf("Failed to query port %u props\n", port);
            goto close;
        }
        printf("port:\t\t\t%u\n", port);
        printf("state:\t\t\t(%d)\n", port_attr.state);
        printf("active_mtu:\t\t(%d)\n", port_attr.active_mtu);
        printf("active_speed:\t\t(%d)\n", port_attr.active_speed);
    }

close:
    if (ctx != NULL) {
        if (ibv_close_device(ctx)) {
            printf("Failed to close device\n");
        }
     }

out:
    ibv_free_device_list(dev_list);

    return 0;
}

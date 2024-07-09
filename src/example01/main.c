#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <endian.h>
#include <infiniband/verbs.h>

int main() {
    int i, ib_dev_num;

    struct ibv_device **dev_list;

    dev_list = ibv_get_device_list(&ib_dev_num);

    if (!dev_list) {
        perror("Failed to get IB device list");
        return -1;
    }

    printf("%-16s\tnode GUID\n", "device");
    printf("%-16s\t---------------\n", "------");

    for (i=0; i < ib_dev_num; ++i) {
        printf("%-16s\t%016llx\n",
                ibv_get_device_name(dev_list[i]),
                (unsigned long long)be64toh(ibv_get_device_guid(dev_list[i])));
    }

    ibv_free_device_list(dev_list);

    return 0;
}

本实例就是读取指定 RDMA 网卡及其端口信息. 主要的函数:

* ibv_get_device_list
* ibv_open_device
* ibv_query_device_ex
* ibv_query_port
* ibv_close_device
* ibv_free_device_list

1. 编译

```bash
make
```

2. 执行

```bash
./main
```

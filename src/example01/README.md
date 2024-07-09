本实例就是读取 RDMA 网卡列表, 并展示其 GUID.

1. 编译

```bash
make
```

2. 执行

```bash
./main
```

注意: 如果编译通不过, 需要先安装 rdma-core 相关的开发包.
在 CentOS/Rock/RHEL等发行版上对应的包名是: rdma-core-devel

```bash
yum install -y rdma-core-devel
```

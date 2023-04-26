## 负载生成器

本示例将演示如何使用 Pipy 写一个压力测试用常用的负载生成器（Load Generator）

## 配置说明

在 [`config.json`](./config.json) 中：

- `url`：请求地址
- `method`：请求的方法名
- `headers`：请求头信息
- `payloadSize`：请求的负载大小
- `concurrency`：请求的并发数

## 运行

假如有一个 HTTP 服务，监听在 `8000` 端口：

```shell
$ pipy -e "pipy().listen(8000).serveHTTP(()=>new Message(''))"
```

运行负载生成器，生成负载：

```shell
$ pipy main.js --admin-port=6060 
```

在负载生成器运行的过程中，可以通过访问 `:6060/metrics` 查看请求的指标：

```shell
$ curl -s localhost:6060/metrics | grep 'latency\|counts'
```
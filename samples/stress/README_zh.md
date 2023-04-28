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

在负载生成器运行的过程中，可以通过访问 `:6060/metrics` 查看请求的延迟和数量：

```shell
$ curl -s localhost:6060/metrics | grep 'latency\|counts'
latency_bucket{le="1"} 981189
latency_bucket{le="2"} 3589
latency_bucket{le="3"} 755
latency_bucket{le="4"} 110
latency_bucket{le="5"} 303
latency_bucket{le="6"} 178
latency_bucket{le="7"} 139
latency_bucket{le="8"} 176
latency_bucket{le="9"} 12
latency_bucket{le="10"} 0
latency_bucket{le="11"} 0
latency_bucket{le="12"} 0
latency_bucket{le="13"} 22
latency_bucket{le="14"} 18
latency_bucket{le="15"} 5
latency_bucket{le="16"} 55
latency_bucket{le="17"} 9
latency_bucket{le="18"} 0
latency_bucket{le="19"} 0
latency_bucket{le="20"} 0
latency_bucket{le="21"} 0
latency_bucket{le="22"} 0
latency_bucket{le="23"} 0
latency_bucket{le="24"} 0
latency_bucket{le="25"} 0
latency_bucket{le="26"} 0
latency_bucket{le="27"} 0
latency_bucket{le="28"} 0
latency_bucket{le="29"} 0
latency_bucket{le="30"} 0
latency_bucket{le="Inf"} 0
latency_count 986560
latency_sum 530981
counts 986560
counts{status="200"} 986560
```
## Load Generator

This example demonstrates how to use Pipy to write a commonly used Load Generator for stress testing.

## Configuration

In [`config.json`](./config.json):

- `url`: Request URL
- `method`: Request method
- `headers`: Request headers
- `payloadSize`: Request payload size
- `concurrency`: Request concurrency

## Running

Assuming there is an HTTP service listening on port `8000`:

```shell
$ pipy -e "pipy().listen(8000).serveHTTP(()=>new Message(''))"
```

Run the Load Generator to generate traffic:

```shell
$ pipy main.js --admin-port=6060 
```

While the Load Generator is running, you can view the request latency and count by accessing `:6060/metrics`.

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
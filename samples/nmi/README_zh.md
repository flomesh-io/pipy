## 原生模块接口

本示例演示如何使用 Pipy 的原生模块接口，通过 C 语言来编写模块。

## 介绍

示例中提供了三个 C 编写的模块：

- [hello](./hello/)：实现了一个 web 服务，返回 `Hi!` 响应。
- [line-count](./line-count/)：统计输入内容的行数
- [counter-threads](./counter-threads/)：在线程中间隔 1s 打印 1 - 10

每个模块中：
- `.c` 文件就是模块的逻辑文件
- `main.js` 文件是程序的入口，包含了对编译后模块 `.so` 的引用

## 运行

在运行之前，我们需要对 C 编写的模块进行编译。

### hello 模块

```shell
cd hello
# 编译模块
$ make all
# 运行
$ make test
```

运行之后，通过 curl 来发送测试请求，会返回模块输出的 `Hi!`。

```shell
$ curl localhost:8080
Hi!
```

### line-count 模块

```shell
cd line-count
# 编译模块
$ make all
# 运行
$ pipy main.js
2023-04-28 18:06:28.381 [INF] [config]
2023-04-28 18:06:28.382 [INF] [config] Module /main.js
2023-04-28 18:06:28.382 [INF] [config] ===============
2023-04-28 18:06:28.382 [INF] [config]
2023-04-28 18:06:28.382 [INF] [config]  [Task #1 ()]
2023-04-28 18:06:28.382 [INF] [config]  ----->|
2023-04-28 18:06:28.382 [INF] [config]        |
2023-04-28 18:06:28.382 [INF] [config]       read
2023-04-28 18:06:28.382 [INF] [config]       use ../../../bin/line-count.so
2023-04-28 18:06:28.382 [INF] [config]       handleStreamEnd -->|
2023-04-28 18:06:28.382 [INF] [config]                          |
2023-04-28 18:06:28.382 [INF] [config]  <-----------------------|
2023-04-28 18:06:28.382 [INF] [config]
2023-04-28 18:06:28.383 [INF] [start] Thread 0 started
2023-04-28 18:06:28.385 [INF] Line count: 42
2023-04-28 18:06:28.386 [INF] [start] Thread 0 done
2023-04-28 18:06:28.386 [INF] [start] Thread 0 ended
Done.
```

运行之后，在日志中可以看到打印了文件 `line-count.c` 的行数 “42”。

### counter-threads 模块

```shell
cd counter-threads
# 编译模块
$ make all
# 运行
$ pipy main.js
2023-04-28 18:11:48.474 [INF] [config]
2023-04-28 18:11:48.474 [INF] [config] Module /main.js
2023-04-28 18:11:48.474 [INF] [config] ===============
2023-04-28 18:11:48.474 [INF] [config]
2023-04-28 18:11:48.474 [INF] [config]  [Task #1 ()]
2023-04-28 18:11:48.474 [INF] [config]  ----->|
2023-04-28 18:11:48.474 [INF] [config]        |
2023-04-28 18:11:48.474 [INF] [config]       use ../../../bin/counter-threads.so
2023-04-28 18:11:48.474 [INF] [config]       handleMessage -->|
2023-04-28 18:11:48.474 [INF] [config]                        |
2023-04-28 18:11:48.474 [INF] [config]  <---------------------|
2023-04-28 18:11:48.474 [INF] [config]
2023-04-28 18:11:48.474 [INF] [start] Thread 0 started
2023-04-28 18:11:48.474 [INF] 1
2023-04-28 18:11:49.477 [INF] 2
2023-04-28 18:11:50.482 [INF] 3
2023-04-28 18:11:51.486 [INF] 4
2023-04-28 18:11:52.490 [INF] 5
2023-04-28 18:11:53.495 [INF] 6
2023-04-28 18:11:54.500 [INF] 7
2023-04-28 18:11:55.505 [INF] 8
2023-04-28 18:11:56.512 [INF] 9
2023-04-28 18:11:57.517 [INF] 10
2023-04-28 18:11:59.484 [INF] [start] Thread 0 done
Done.
```

执行 `pipy main.js` 时默认只使用一个线程运行，可以看到程序间隔 1s 打印了计数器的结果。

运行之后，
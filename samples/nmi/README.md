## Native Module Interface

This example demonstrates how to use Pipy's native module interface to write modules in C language.

## Introduction

Three C-written modules are provided in the example:

- [hello](./hello/): implements a web service that returns `Hi!` response.
- [line-count](./line-count/): counts the number of lines in the input content
- [counter-threads](./counter-threads/): prints 1 - 10 at an interval of 1s in threads

In each module:
- The `.c` file is the module's logical file
- The `main.js` file is the program's entry point and contains a reference to the compiled module `.so`.

## Run

Before running, we need to compile the C-written modules.

### hello module

```shell
cd hello
# compile module
$ make all
# run
$ make test
```

After running, send a test request through curl and the module will return the `Hi!` output.

```shell
$ curl localhost:8080
Hi!
```

### line-count module

```shell
cd line-count
# compile module
$ make all
# run
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

After running, you can see in the log that the number of lines in the file `line-count.c` is printed as "42".

### counter-threads module

```shell
cd counter-threads
# compile module
$ make all
# run
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

The command `pipy main.js` runs using only one thread by default, and you can see that the program prints the counter's result every 1 second.

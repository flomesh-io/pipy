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

分别在三个模块的目录中执行如下命令：

```shell
# 编译模块
$ make all
# 运行测试
$ make test
```
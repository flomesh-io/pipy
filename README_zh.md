# Pipy

Pipy 是面向云、边缘和 IoT 的可编程网络代理。使用 C++ 开发，因此 Pipy 非常轻量和快速。还可以使用标准 JavaScript 定制版的 PipyJS 进行编程。

## Pipy 的特点

### 灵活多变

虽然 Pipy 主要用作高性能的反向代理，但 Pipy 的真正能力是提供了一系列可插拔的组件，又称过滤器，同时对组合的方式没有限制。如何使用 Pipy 完全取决于需求。Pipy 已经被用于协议转换、流量录制、消息加签/验签、无服务器的功能启动、健康检查等等。

### 快

Pipy 使用 C++ 开发。使用了异步网络；分配的资源被池化并可复用；内部尽可能地使用指针传递数据，最大限度降低内存带宽的压力。在各方面都极快。

### 小

用于工作节点的可执行文件仅 10MB 左右，并且没有任何外部依赖。使用 Pipy 可以体验到最快速的下载和启动。

### 可编程

Pipy 是一个运行 PipyJS（标准 JavaScript 的定制版本） 的脚本引擎。通过这个使用最广泛的编程语言，Pipy 提供了相比 YAML 或类似格式更强大的表现力。

### 开放

Pipy 比开源更加开放。你将对所有细节都了如指掌，Pipy 不会有任何隐藏。不过无需担心，使用 Pipy 并不需要了解这些细节。但了解之后，会更加有趣。

## 快速开始

### 构建

构建需要满足以下条件：

* Clang 5.0+
* CMake 3.10+
* Node.js v12+ (如果要开启内置的*管理界面*)

执行构建脚本：

```
./build.sh
```

可以在 `bin/pipy` 目录中找到二进制文件。

### 运行

不指定任何参数运行 `bin/pipy` 命令，Pipy 会运行在代码库模式并监听默认端口 6060。

```
$ bin/pipy

[INF] [admin] Starting admin service...
[INF] [listener] Listening on port 6060 at ::
```

在浏览器中访问地址 `http://localhost:6060` 可以打开内置的*管理界面*。在*管理界面*可以浏览文档和使用教程代码库。

## 文档

在 `docs/` 可以浏览 Pipy 的文档：

* [概述](./docs/intro/overview.zh.mdx)
* [概念](./docs/intro/concepts.zh.mdx)
* [快速开始](./docs/getting-started/quick-start.zh.mdx)
* 教程
    * [01 Hello world](./docs/tutorial/01-hello.zh.mdx)
    * [02 回显](./docs/tutorial/02-echo.zh.mdx)
    * [03 代理](./docs/tutorial/03-proxy.zh.mdx)
    * [04 路由](./docs/tutorial/04-routing.zh.mdx)
    * [05 插件](./docs/tutorial/05-plugins.zh.mdx)
    * [06 配置](./docs/tutorial/06-configuration.zh.mdx)
    * [07 负载均衡](./docs/tutorial/07-load-balancing.zh.mdx)
    * [08 负载均衡进阶](./docs/tutorial/08-load-balancing-improved.zh.mdx)
    * [09 连接池](./docs/tutorial/09-connection-pool.zh.mdx)
    * [10 路径重写](./docs/tutorial/10-path-rewriting.zh.mdx)
    * [11 Logging](./docs/tutorial/11-logging.zh.mdx)
    * [12 JWT](./docs/tutorial/12-jwt.zh.mdx)
    * [13 黑白名单](./docs/tutorial/13-ban.zh.mdx)
    * [14 限流](./docs/tutorial/14-throttle.zh.mdx)
    * [15 缓存](./docs/tutorial/15-cache.zh.mdx)
    * [16 静态资源服务](./docs/tutorial/16-serve-static.zh.mdx)
    * [17 数据格式转换](./docs/tutorial/17-body-transform.zh.mdx)
    * [18 TLS](./docs/tutorial/18-tls.zh.mdx)
* 学习和文章
  * [Katacoda](https://katacoda.com/flomesh-io) - Katacoda 场景
  * [InfoQ 文章](https://www.infoq.com/articles/network-proxy-stream-processor-pipy/) - 简要介绍
* [版权](COPYRIGHT)
* [许可](LICENCE)

## 兼容性

Pipy 已经通过如下平台的测试：

* RHEL/CentOS
* Fedora
* Ubuntu
* Debian
* macOS
* FreeBSD
* OpenBSD
* OpenEuler
* OpenWrt
* Deepin
* Kylin

Pipy 可以在以下架构上运行：

* X86/64
* ARM64
* LoongArch
* Hygon

## 版权许可

参考 [版权](https://github.com/flomesh-io/pipy/blob/main/COPYRIGHT) 和 [许可](https://github.com/flomesh-io/pipy/blob/main/LICENCE)

## 联系方式

* 安全问题：security@flomesh.io
* 法务问题：legal@flomesh.io
* 商用、市场相关：sales@flomesh.io
* 其他非公开话题：pipy@flomesh.io
* 其他任何问题，欢迎给我们[提 issue](https://github.com/flomesh-io/pipy/issues)

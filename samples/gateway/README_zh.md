## 模块化的反向代理

本示例是在[反向代理](../serve/)的基础上，将功能进行了模块化处理，并假如了如 JWT、可配置的负载均衡器、缓存、logging 和 metrics 等功能。

模块化的设计是大的程序拆分成若干个较小的模块，每个模块可以独立地进行开发和维护。对于 Pipy 编程来说，将可独立拆分的功能维护在单一的 js 文件中，这个 js 文件就是模块。通过引入模块化可以提升开发效率、提高代码的可读性和可维护性，配合插件化的设计进而提高代码的复用性。

## 示例介绍

- [`main.js`](./main.js) 程序入口
- [`config.json`](./config.json) 配置文件
- [`config.js`](./config.js) 配置文件处理器
- [`plugins`](./plugins/) 模块列表
  - `metrics.js` 指标模块
  - `router.js` 路由模块
  - `serve-files.js` 静态文件模块
  - `balancer.js` 负载均衡模块
  - `cache.js` 缓存模块
  - `default.js` 无法处理路由时的 404 模块
  - `hello.js` Hello web 服务模块
  - `jwt.js` JWT 认证模块
  - `logging.js` logging 模块
- [`secret`](./secret/) 用于 JWT 认证的公钥和私钥
- [`home`](./home/) 静态文件目录

### 配置说明

在 [`config.json`](./config.json) 中可以对反向代理进行配置，这里介绍部分配置：

- `plugins`：配置要使用的模块，通过调整模块在列表中的顺序来配置模块的执行顺序
- `endpoints`：路由配置
- `cache`：缓存配置，包括需要缓存的文件后缀名以及缓存过期时间
- `jwt`：JWT 认证配置，主要配置认证时可用的私钥
- `log`：日志配置，在示例中使用的是远程日志，即将日志通过 HTTP 发送到远端的日志系统。这里配置了日志系统的访问地址以及批处理的大小。

## 运行

```shell
$ pipy main.js
```
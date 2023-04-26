## 服务网格边车

本示例演示的是如何将 Pipy 作为服务网格边车（sidecar）来使用。Pipy 作为服务网格的边车，提供客户端负载均衡的能力，对进出站的流量进行处理。

示例中使用的代码来自 [Flomesh 服务网格](https://github.com/flomesh-io/osm-edge/tree/main/pkg/sidecar/providers/pipy/repo/codebase)（版本更新可能会有差异）。

**由于服务网格边车模式，需要控制面的配合完成配置的自动化。即使本实例可以正常运行，但仅作为示例参考。建议参考[服务网格快速入门文档](https://osm-edge-docs.flomesh.io/docs/quickstart/) 进行体验。**

## 示例介绍

- [`main.js`](./main.js) 程序入口
- [`config.json`](./config.json) 配置文件
- [`config.js`](./config.js) 配置文件处理器
- [`modules`](./plugins/) 模块列表
  - `inbound—xxx` 入站流量处理模块
  - `outbound-xxx` 出站流量处理模块

## 运行


```shell
$ pipy main.js
```
## BGP 宣告

边界网关协议 BGP，全称为 Border Gateway Protocol，是一种基于 TCP 协议的网络路由协议，通常用于在不同自治系统（AS，Autonomous System）之间的路由交换。BGP 负责将一组 IP 地址或者前缀宣告到网络上的其他路由器，路由器在处理网络数据包时会选择最佳的路径以便将数据包传送到目的地。

在本示例中将演示如何使用 Pipy 实现 BGP 宣告，用于完成 BGP 宣告的组件我们称之为 BGP 宣告者（BGP Speaker）。

## 示例介绍

在这个示例中共用到 3 个文件：
- [peer.js](./peer.js)：该文件中定义了 BGP 的相关操作，比如创建 peer、封装请求、处理应答等等
- [config.json](./config.json)：BGP 宣告器的配置文件，将在下面对配置的进行说明
- [main.js](./main.js)：核心程序，每隔 1s 向 peer（通常是路由器）宣告有效和无效的 IP 地址/前缀，并对 peer 的应答进行处理

### 配置说明

- `as`：当前 AS 的标识符
- `id`：BGP 宣告者的 IP 地址
- `peers`：一组对外宣告的路由器的地址
- `ipv4`：宣告 ipv4 地址信息
  - `nextHop`：可访问到宣告 IP 的路由器地址
  - `reachable`：宣告 IP 地址/前缀列表，会在 `peers` 上注册
  - `unreachable`：宣告无效的 IP 地址/前缀列表，会从 `peers` 上摘除
- `ipv6`：宣告 ipv6 地址信息

## 运行

关于如何运行，我们在[博客](https://blog.flomesh.cn/implement-bgp-speaker-with-pipy/)上提供了详细的指引。
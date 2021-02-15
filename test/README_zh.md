# 测试案例、配置示例及参考

## 001 echo

该示例演示了如何使用HTTP请求解码模块和HTTP回复编码模块完成一次HTTP请求处理；并且演示了两个模块共享context。

## 002 hello

该示例演示使用hello模块输出字符串。该模块可以用于调试、演示、输出确定内容。比如在流量处理时，跳转到某个类似HTTP404错误的提示信息。

## 003 json

该示例演示了JSON数据的序列化和反序列化，并且演示了使用filter和replace模块完成简单的JSON数据格式转换。

## 004 js-echo

该示例是001-echo的“进化版”，演示了如何在pipy的pipeline上使用script模块（QuickJS引擎）运行最简单的JS代码。
基于开发团队测试，引入QuickJS，在最低情况下，会带来10%的性能开销，和额外1.5M的内存使用。作为完整支持JS的引擎，
并且是解释执行，QuickJS的表现非常优秀。[感谢Bellard，感谢QuickJS](https://github.com/bellard/quickjs)~

## 005 js-hello

该示例是002-hello的“JS版”。002-hello使用C++原生模块输出字符串；该示例使用JS输出字符串。该示例主要用于对比测试引入JS后带来的资源和性能开销。

## 006 js-json

该示例演示了如何使用JS来处理JSON数据--把流式的JSON数据，构建成内存里的DOM对象树。

## 007 js-json-parse

该示例演示了使用JS把流式的JSON数据整个读入之后进行处理。流式处理效率很高，但是编码难度较大。因此在很多时候，我们可以把数据完整读取之后，按照字符串和对象的方式处理，这样开发难度会低很多，开发效率更高。

## 008 js-crypto

该示例演示了使用内置openssl库进行SM2和SM4的签名/验签和加密/解密。

## 009 routing

该示例演示了基于请求路径“路由”HTTP请求到不同的Pipeline。路由是proxy的核心和基础功能。使用JS，开发人员可以快速的写出自定义的路由规则。比如基于某些复合条件的路由，如“域名+路径”。
同时，多种高级流量分发的功能也是基于该示例的用法。比如灰度发布、蓝绿发布、地域（GEO）流量调度、内测流量分流等。参考该示例，pipy用户可以使用JS快速开发出自定义的路由策略，并且能够保障proxy的高性能、低资源特征。

## 010 load-balancing

该示例演示了最简单的“轮询式”负载均衡。和“路由”一样，负载均衡也是proxy的核心和基础功能。使用JS，pipy可以按需实现各种常用以及定制化的负载均衡算法，提供了灵活、强大、高性能的负载均衡能力。
在实际使用场景中，参考该示例，pipy用户可以快速实现类似“客户端负载均衡”的能力。如在012-logging/clickhouse.cfg中，演示了如何把日志简单分发到多个clickhouse节点；该功能通常用来提高吞吐量。

## 011 serve-static

该示例演示了如何使用pipy服务静态内容的能力--指定一个目录，设置文件扩展名和MIME_TYPE映射关系，一个最简单的web服务器就实现了，并且是高性能、低资源占用的。在基本的内测（wrk -c100 -t1 -d10 http://localhost:6000/）中，该示例在各个平台的测试结果（TPS）都是Nginx的二倍左右。注：测试系统和Nginx都没有做任何优化，如CentOS7就是“最小化安装+yum install nginx”。

## 012 logging

该示例演示了如何从pipy proxy发送日志到外置的NoSQL数据库，示例包含了发送日志到ElasticSearch(pipy.cfg)和ClickHouse(clickhouse.cfg)。Pipy设计目标之一是“云原生”，因此pipy默认没有提供基于文件和控制台的日志能力（感兴趣的用户可以基于该示例扩展支持文件日志和控制台日志），而是提供了“可扩展+直通NoSQL”的日志能力。在format-request.js中，可以看到如何定制日志信息的方法，包括获取报文、环境变量（k8s中大量信息来自环境变量）、以及获取context中信息。这些结构化的信息，pipy可以直接写入NoSQL。该过程中pipy提供了基于内存的缓存能力。当proxy的速度超过NoSQL的入库速度时，该缓冲可以起到“削峰平谷”的作用。pipy的“可扩展+直通NoSQL”的模式简化了目前流行的“采集器（如filebeat/vector）+ 传输（如Kafka）+ 格式化（如logstash）”模式，降低了数据采集工作本身的复杂度和运维复杂度，同时提高了数据的时效性。

## 013 metrics

该示例演示了如何使用count模块和prometheus模块实现基本的“计数”型统计，以及输出prometheus兼容格式。

## 014 throttle

该示例演示了使用tap模块实现“限速”功能--设置最大通过TPS，当请求速度超过该阈值后，请求会被pipy放入队列。

## 015 dubbo

该示例演示了pipy如何处理Dubbo RPC协议，以及Hession2序列化/反序列化。包含了一个Dubbo-to-Dubbo的代理，和一个REST-to-Dubbo/JSON-to-Hession2的功能。

## 016 xml

该示例演示了pipy的xml编解码功能，同样是高性能、低资源占用。

# Translations

## [English](https://github.com/flomesh-io/pipy/blob/main/test/README.md)

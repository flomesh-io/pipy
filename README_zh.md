# Pipy

Pipy是一个轻量、高性能、高稳定、可编程的网络代理。Pipy核心框架使用C++开发，网络IO采用ASIO库。
Pipy的可执行文件在5M左右，基于Alpine的镜像是10M左右，运行期的内存占用10M左右，因此Pipy非常
适合做sidecar proxy。

Pipy内置了[QuickJS](https://github.com/bellard/quickjs) 作为脚本扩展，使得Pipy可以用JS脚本
快速扩展需要定制的逻辑与功能。QuickJS的“确定性垃圾回收（GC）”机制，进一步保证了Pipy的可靠性与确定性，
避免了很多脚本类语言因GC导致的不确定性问题。

Pipy采用了模块化、链式的处理架构，对网络数据块通过一个一个顺序执行的模块完成处理。这种简单的架构使得Pipy
底层简单可靠，同时具备了动态编排流量的能力，兼顾了简单和灵活。通过使用REUSE_PORT的机制（新的Linux和BSD
都支持该功能），Pipy可以以多进程模式运行，使得Pipy不仅仅可以用于sidecar模式，也适用与大规模的流量处理。
在实践中，Pipy独立部署的时候用作“软负载”，可以实现媲美硬件的负载均衡吞吐能力和低延迟，同时具有灵活的扩展性。

# 兼容性

Pipy在设计时注重兼容性考虑，可以支持多种操作系统平台与CPU架构，开发团队测试过的平台和架构包括：

* CentOS 7
* Alpine镜像
* Ubuntu 18/20
* FreeBSD 12/13
* macOS M1(不支持REUSE_PORT)
* macOS x86(不支持REUSE_PORT)

生产环境推荐使用CentOS7(及同类品)或者FreeBSD.

# 编译与构建

## 从源码构建

构建Pipy需要满足如下版本依赖：

* Clang 5.0+
* CMake 3.0+

满足clang和cmake版本后，直接运行如下编译命令：

```
./build.sh
```

Pipy编译结果是“单一可执行文件”，可执行文件是bin/pipy，可以运行`bin/pipy -h`获取更多信息。

## 构建Docker镜像

使用如下命令构建Docker镜像：

```
cd pipy
sudo docker build --squash --rm -t pipy .
```

> 注：可以使用--squash的参数来构建更小的镜像。参考文档：[Docker Documentation](https://docs.docker.com/engine/reference/commandline/image_build/)

# 快速上手

## X86 RPM安装

```
yum -y install http://repo.flomesh.cn/pipy/pipy-latest.el7_pl.x86_64.rpm
```

## 命令行参数

```
$ pipy --help
```

## 模块列表及参数

```
$ pipy --list-modules
$ pipy --help-modules
```

## 命令行运行

以演示案例test/001-echo为例，可以用这个方式运行（其中的--watch-config-file参数用于在配置文件变化时自动重新加载配置）：

```
$ pipy test/001-echo/pipy.cfg --watch-config-file
```

在Linux或者BSD系统上，可以通过如下命令启动两个（或者多个）Pipy监听同一个端口，内核会自动的在多个进程间负载均衡流量。这极大的提高了吞吐能力。同时，
这种进程间share nothing的结构回避了很多多进程/多线程的复杂度，使得Pipy在通过多进程横向扩展吞吐能力的同时，没有引入新的复杂度和不确定性：

```
$ pipy test/001-echo/pipy.cfg --reuse-port &
$ pipy test/001-echo/pipy.cfg --reuse-port &
```

## 使用Docker运行Pipy

Pipy的Docker镜像识别如下的环境变量：

* `PIPY_CONFIG_FILE=</path/to/config-file>` 定义了pipy配置文件的位置

* `PIPY_SPAWN=n` 定义了同时运行几个Pipy进程；注意n是目标进程数减一。也就是说，如果希望运行2个Pipy进程，那么PIPY_SPAWN=1就可以。

```
docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.cfg flomesh/pipy:latest
```

```
docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/011-serve-static/pipy.cfg -e PIPY_SPAWN=1 -p 8000:6000 flomesh/pipy:latest
```

当作为sidecar proxy运行的时候，Pipy支持“透明代理(https://www.kernel.org/doc/Documentation/networking/tproxy.txt "Linux Transparent Proxy")”。
启动时加入NET_ADMIN就可以了。

```
docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.cfg --cap-add NET_ADMIN flomesh/pipy:latest
```

## 在k8s上运行Pipy

我们提供了CRD和Operator用于在k8s上运行Pipy，在另外一个开源项目中[pipy-operator](https://github.com/flomesh-io/pipy-operator)。
如果恰好你也需要通过CRD/Operator的方式运行Pipy，这些代码可以作为参考实现。

```
git clone https://github.com/flomesh-io/pipy-operator
cd pipy-operator
kubectl apply -f etc/cert-manager-v1.1.0.yaml
kubectl apply -f artifact/pipy-operator.yaml
kubectl apply -f config/samples/standalone/001-echo.yaml
kubectl apply -f config/samples/ingress/001-routing.yaml
kubectl apply -f config/samples/sidecar/007-deployment-pipy.yaml
```

# 文档

文档在docs目录：

* [概述](https://github.com/flomesh-io/pipy/blob/main/docs/overview.md)
* [配置](https://github.com/flomesh-io/pipy/blob/main/docs/configuration.md)

# 版权与授权

详见：
* [版权](https://github.com/flomesh-io/pipy/blob/main/COPYRIGHT) 
* [授权]](https://github.com/flomesh-io/pipy/blob/main/LICENCE).

# 联系方式

* 安全问题，请电子邮件给：security@flomesh.io
* 法务问题，请电子邮件给：legal@flomesh.io
* 商用、市场相关，请电子邮件给：sales@flomesh.io
* 其他非公开话题, 请发电子邮件给：pipy@flomesh.io
* 其他任何问题，欢迎给我们提issues: https://github.com/flomesh-io/pipy/issues

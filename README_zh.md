# Pipy

Pipy 是一个轻量级、高性能、高稳定、可编程的网络代理。Pipy 核心框架使用 C++ 开发，网络 IO 采用 ASIO 库。
Pipy 的可执行文件仅有 10M 左右，运行期的内存占用 10M 左右，因此 Pipy 非常适合做 Sidecar proxy。

~~Pipy 内置了 [QuickJS](https://github.com/bellard/quickjs) 作为脚本扩展，使得Pipy 可以用 JS 脚本根据特定需求快速定制逻辑与功能。QuickJS 的“确定性垃圾回收”机制，进一步保证了 Pipy 的可靠性与确定性，避免了很多脚本类语言因 GC 导致的不确定性问题。~~

这次迭代，我们使用自开发的JS引擎替代了之前的QuickJS。自研的JS引擎更加适合描述对流量处理的规则，在多数场景下，语义更简单直接；并且自开发的JS引擎对很多操作进行了优化，具有更好的性能。

Pipy 采用了模块化、链式的处理架构，用顺序执行的模块来对网络数据块进行处理。这种简单的架构使得 Pipy 底层简单可靠，同时具备了动态编排流量的能力，兼顾了简单和灵活。通过使用 `REUSE_PORT` 的机制（主流 Linux 和 BSD
版本都支持该功能），Pipy 可以以多进程模式运行，使得 Pipy 不仅适用于 Sidecar 模式，也适用与于大规模的流量处理场景。
在实践中，Pipy 独立部署的时候用作“软负载”，可以在低延迟的情况下，实现媲美硬件的负载均衡吞吐能力，同时具有灵活的扩展性。

## 兼容性

兼容性是 Pipy 的设计重点之一，它可以支持多种操作系统平台与 CPU 架构，目前开发团队已经完成了在下列平台和架构上的[测试](doc-fix/test)：

* Alpine 3
* CentOS 7
* FreeBSD 12/13
* macOS M1/x86（不支持 `REUSE_PORT`）
* Ubuntu 18/20

生产环境推荐使用 CentOS 7（及同类品）或者 FreeBSD。

## 图形界面

这次迭代我们引入了web编辑器。启动pipy时使用--gui-port=6060来开启该功能，如：

～～～
pipy test --gui-port=6060
~~~~

启动pipy后，用浏览器访问 http://localhost:6060/ 就可以看到pipy控制台。控制台采用类似vscode的布局和操作体验。具体的使用可以参考[docs/pipy-console_zh.md]。

pipy console的设计目的包括如下两个：
* 流量编辑人员可以快速的编辑pipy js(PJS)脚本，进行调试；同时pipy console以图形化方式展示流量处理逻辑，可以帮助使用者在复杂逻辑情况下快速理解处理逻辑
* 在云端的开发测试环境中，用户可以不用登陆服务器就完成PJS脚本的调试，如加入打印语句等。对比ssh到服务器，或者attach到k8s容器内，pipy web console更加易于使用

需要注意的是：pipy console不适合生产环境，不要在不信任的环境里开启pipy console功能，会有安全问题。

## 构建

### 从源码构建

构建 Pipy 需要满足如下版本依赖：

* Clang 5.0+
* CMake 3.0+

满足 clang 和 cmake 版本后，直接运行如下编译命令：

```command
$ ./build.sh
...
```

Pipy 编译生成的单一可执行文件会输出到 `bin/pipy` 目录，可以运行 `bin/pipy -h` 获取更多信息。

### 构建 Docker 镜像

使用如下命令构建 Docker 镜像：

```command
$ cd pipy
$ sudo docker build --rm -t pipy .
...
```

> 注：可以使用 `--squash` 的参数来构建更小的镜像。参考文档：[Docker Documentation](https://docs.docker.com/engine/reference/commandline/image_build/)

## 快速上手

### 下载运行

Github的Release页面提供了多种平台的可执行文件，下载对应的可执行文件后就可以运行了。pipy只有一个可执行文件，采用single binary方式打包，所有的依赖都在这个可执行文件中，包括pipy console。因此，直接下载是最便捷的体验pipy的方法。

### x86 环境中使用 rpm 安装

```command
$ yum -y install http://repo.flomesh.cn/pipy/pipy-latest.el7_pl.x86_64.rpm
...
```

### 命令行参数

```command
$ pipy --help
...
```

### 模块列表及参数

```command
$ pipy --list-modules
...
$ pipy --help-modules
```

### 命令行运行

以演示案例 `test/001-echo` 为例，可以用这个方式运行（其中的 `--watch-config-file` 参数用于在配置文件变化时自动重新加载配置）：

```command
$ pipy test/001-echo/pipy.js
...
```

或者使用pipy console:
```command
$ pipy test/ --gui-port=6060
...
```
然后使用浏览器访问 http://localhost:6060/ 就可以了。

在 Linux 或者 BSD 系统上，可以通过如下命令启动两个（或者多个）Pipy 监听同一个端口，内核会自动的在多个进程间负载均衡流量，这种方式极大地提高了吞吐能力。这种进程间 Share nothing 的结构降低了多进程/多线程的复杂度，使得 Pipy 能够在不引入新的复杂度和不确定性的情况下，具备了多进程横向扩展吞吐能力的能力：

```command
$ pipy test/001-echo/pipy.js --reuse-port &
$ pipy test/001-echo/pipy.js --reuse-port &
...
```

## 使用 Docker 运行 Pipy

Pipy 的 Docker 镜像识别如下的环境变量：

* `PIPY_CONFIG_FILE=</path/to/config-file>` 定义了 Pipy 配置文件的位置

* `PIPY_SPAWN=n` 定义了同时运行的 Pipy 进程数量；注意 `n` 是目标进程数减一。也就是说，如果希望运行两个 Pipy 进程，那么 `PIPY_SPAWN=1` 就可以。

```command
$ docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.js flomesh/pipy-pjs:latest
...
```

```command
docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/011-serve-static/pipy.js -e PIPY_SPAWN=1 -p 8000:6000 flomesh/pipy-pjs:latest
...
```

## 在 Kubernetes 上运行 Pipy

我们在 [pipy-operator](https://github.com/flomesh-io/pipy-operator) 项目中提供了用于 Kubernetes 环境中的 CRD 和 Operator 示例代码。读者可以参考这些代码来实现自己的 CRD/Operator。

```command
$ git clone https://github.com/flomesh-io/pipy-operator
$ cd pipy-operator
$ kubectl apply -f etc/cert-manager-v1.1.0.yaml
$ kubectl apply -f artifact/pipy-operator.yaml
$ kubectl apply -f config/samples/standalone/001-echo.yaml
$ kubectl apply -f config/samples/ingress/001-routing.yaml
$ kubectl apply -f config/samples/sidecar/007-deployment-pipy.yaml
...
```

## 文档

文档在保存在 [`docs`](docs) 目录：

* [概述](docs/overview.md)
* [配置](docs/configuration.md)
* [版权](COPYRIGHT)
* [授权](LICENCE)

## 联系方式

* 安全问题：security@flomesh.io
* 法务问题：legal@flomesh.io
* 商用、市场相关：sales@flomesh.io
* 其他非公开话题：pipy@flomesh.io
* 其他任何问题，欢迎给我们[提 issue](https://github.com/flomesh-io/pipy/issues)

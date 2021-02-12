# Pipy

Pipy is a tiny, high performance, highly stable, programmable proxy. Written
in C++, built on top of Asio asynchronous I/O library, Pipy is extremely
lightweight and fast, making it one of the best choices for service mesh sidecars.

With builtin JavaScript support, thanks to QuickJS, Pipy is highly
customizable and also predictable in performance with no garbage collection
overhead seen in other scriptable counterparts.

At its core, Pipy has a modular design with many small reusable modules
that can be chained together to make a pipeline, through which network data
flow and get processed on the way. The way Pipy is designed makes it versatile
enough for not only sidecars but also other use cases involving intermediate
message processing between network nodes.

# Compatibility

Pipy is designed for high compatibility across different operating systems and
CPU architectures. Pipy has been fully tested on these platforms:

* CentOS 7
* Ubuntu 18/20
* FreeBSD 12/13
* macOS Big Sur

CentOS7/REHL7 or FreeBSD are recommended in production environments.

# How to Build

## Build from Scratch

Before building, the following tools are required to be installed first:

* Clang 5.0+
* CMake 3.0+

With the above tools installed, just run the build script to start building:

```
./build.sh
```

The executable is located under `bin/`. Type `bin/pipy -h` for more information.

## Build the Docker Image

To build the Docker image, run the following commands:

```
cd pipy
sudo docker build --squash --rm -t pipy .
```

> Note: For a smaller image, you might want to use `--squash` option. It is an experimental feature, so
you have to add `{ "experimental": true }` to `/etc/docker/daemon.json` and restart Docker daemon
to enable it.
>
> For more information about Docker's `--squash` option, please refer to
[Docker Documentation](https://docs.docker.com/engine/reference/commandline/image_build/)

# Quick Start

## Install with RPM

```
yum -y install http://repo.flomesh.cn/pipy/pipy-latest.el7_pl.x86_64.rpm
```

## Show Command Line Options

```
$ pipy --help
```

## List Modules and Parameters

```
$ pipy --list-modules
$ pipy --help-modules
```

## Run on CLI

Let's take the echo server in `test/001-echo/` as an example. To start a single-worker
Pipy that auto-reloads when the configuration file changes:

```
$ pipy test/001-echo/pipy.cfg --watch-config-file
```

To start two Pipy instances load balancing on the same port:

```
$ pipy test/001-echo/pipy.cfg --reuse-port &
$ pipy test/001-echo/pipy.cfg --reuse-port &
```

## Run with Docker

Pipy Docker image can be configured with a few environment variables:

* `PIPY_CONFIG_FILE=</path/to/config-file>` for the location of Pipy configuration file

* `PIPY_SPAWN=n` for the number of Pipy instances you want to start, where `n` is the number
  of instantces subtracted by 1. For example, you use `PIPY_SPAWN=3` for 4 instances.

```
docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.cfg flomesh/pipy:latest
```

```
docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/011-serve-static/pipy.cfg -e PIPY_SPAWN=1 -p 8000:6000 flomesh/pipy:latest
```

Pipy also supports [transparent proxy](https://www.kernel.org/doc/Documentation/networking/tproxy.txt "Linux Transparent Proxy")
in Docker environment where `NET_ADMIN` capability is enabled by adding `--cap-add NET_ADMIN` option to the startup command:

```
docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.cfg --cap-add NET_ADMIN flomesh/pipy:latest
```

## Run on Kubernetes

You can run Pipy on Kubernetes by using [pipy-operator](https://github.com/flomesh-io/pipy-operator):

```
git clone https://github.com/flomesh-io/pipy-operator
cd pipy-operator
kubectl apply -f etc/cert-manager-v1.1.0.yaml
kubectl apply -f artifact/pipy-operator.yaml
kubectl apply -f config/samples/standalone/001-echo.yaml
kubectl apply -f config/samples/ingress/001-routing.yaml
kubectl apply -f config/samples/sidecar/007-deployment-pipy.yaml
```

# Documentation

You can find Pipy documentation under `docs/`.

* [Overview](https://github.com/flomesh-io/pipy/blob/main/docs/overview.md)
* [Configuration](https://github.com/flomesh-io/pipy/blob/main/docs/configuration.md)

# Copyright & License

Please see [COPYRIGHT](https://github.com/flomesh-io/pipy/blob/main/COPYRIGHT) and [LICENCE](https://github.com/flomesh-io/pipy/blob/main/LICENCE).

# Contact

* For security issues, please send an email to security@flomesh.io
* For legal issues, please send an email to legal@flomesh.io
* For commercial, sales and marketing topics, please send an email to sales@flomesh.io
* For other topics not suitable for the public, please send an email to pipy@flomesh.io
* For public discussions, please go to GitHub issues: https://github.com/flomesh-io/pipy/issues

# Multi-Language

## [中文版](https://github.com/flomesh-io/pipy/README_zh.md)

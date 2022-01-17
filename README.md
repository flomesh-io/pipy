# Pipy

Pipy is a high performance programmable network proxy. Written in C++, Pipy is extremely
lightweight and fast, making it an ideal solution to service mesh proxies.

With the builtin JavaScript support, thanks to PipyJS, Pipy is highly
customizable as well as predictable in performance since PipyJS opts for reference
counting memory management and thus has no uneven garbage collection
overhead we constantly see in other similar systems.

At its core, Pipy is of a modular design provided with a range of fundamental
filters that can be chained up to make a pipeline, where network data is pumped
in and get processed. The way Pipy is designed makes it extremely versatile
even outside of a service mesh environment. Due to its scriptable nature, it also
benefits any sorts of situations involving intermediate message processing between
network nodes where Pipy can be used as a forward/backward proxy, a load balancer,
an API gateway, a static HTTP server, a node in a CDN, a health checker, a serverless
function trigger, or however you want to combine all those features by just a few lines
of script.

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

Before building, the following prerequisites are required to be installed first:

* Clang 5.0+
* CMake 3.0+
* Node.js v12+ (only required if the builtin web UI is enabled)
* zlib

With the above all installed, run the build script to start building:

```
./build.sh
```

The final executable product will be located under `bin/`. Type `bin/pipy -h` for help information.

## Build a Docker Image

To build a Docker image, run the following commands:

```
cd pipy
sudo docker build --squash --rm -t pipy .
```

> Note: For a smaller image, you might want to use `--squash` option. It is an experimental feature, so
you need to add `{ "experimental": true }` to `/etc/docker/daemon.json` and restart Docker daemon
before using it.
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

## List Builtin Filters and Their Parameters

```
$ pipy --list-filters
$ pipy --help-filters
```

## Run on CLI

To start a Pipy proxy, run `pipy` with a PipyJS script file, for example, the script
in `tutorial/01-hello/hello.js` if you need a simple echo server that responds with the same message
body as in every incoming request:

```
$ pipy tutorial/01-hello/hello.js
```

Alternatively, while developing and debugging, one can start Pipy with a builtin web UI:

```
$ pipy tutorial/01-hello/hello.js --admin-port=6060
```

## Run in Docker

The Pipy Docker image can be configured with a few environment variables:

* `PIPY_CONFIG_FILE=</path/to/config-file>` for the location of Pipy configuration file

* `PIPY_SPAWN=n` for the number of Pipy instances you want to start, where `n` is the number
  of instantces subtracted by 1. For example, you use `PIPY_SPAWN=3` for 4 instances.

```
$ docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.js flomesh/pipy-pjs:latest
```

```
$ docker run -it --rm -e PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.js -e PIPY_SPAWN=1 -p 8000:6000 flomesh/pipy-pjs:latest
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

* [Overview](./docs/overview.mdx)
* [Concept](./docs/concepts.mdx)
* [Quick start](./docs/quick-start.mdx)
* Tutorials
    * [01 Hello world](./docs/tutorial/01-hello.mdx)
    * [02 Echo](./docs/tutorial/02-echo.mdx)
    * [03 Proxy](./docs/tutorial/03-proxy.mdx)
    * [04 Routing](./docs/tutorial/04-routing.mdx)
    * [05 Plugins](./docs/tutorial/05-plugins.mdx)
    * [06 Configuration](./docs/tutorial/06-configuration.mdx)
    * [07 Load balancing](./docs/tutorial/07-load-balancing.mdx)
    * [08 Load balancing improved](./docs/tutorial/08-load-balancing-improved.mdx)
    * [09 Connection pool](./docs/tutorial/09-connection-pool.mdx)
    * [10 Path rewriting](./docs/tutorial/10-path-rewriting.mdx)
    * [11 Logging](./docs/tutorial/11-logging.mdx)
    * [12 JWT](./docs/tutorial/12-jwt.mdx)
    * [13 Ban](./docs/tutorial/13-ban.mdx)
    * [14 Throttle](./docs/tutorial/14-throttle.mdx)
<!--* [15 Cache](./docs/tutorial/15-cache.mdx)
    * [16 Serve static](./docs/tutorial/16-serve-static.mdx)
    * [17 Body transform](./docs/tutorial/17-body-transform.mdx)
    * [18 TLS](./docs/tutorial/18-tls.mdx)-->
* [Copyright](COPYRIGHT)
* [Licence](LICENCE)


# Copyright & License

Please refer to [COPYRIGHT](./COPYRIGHT)
and [LICENCE](./LICENCE).

# Contact

* For security issues, please email to security@flomesh.io
* For legal issues, please email to legal@flomesh.io
* For commercial, sales and marketing topics, please email to sales@flomesh.io
* For other topics not suitable for the public, please email to pipy@flomesh.io
* For public discussions, please go to GitHub issues: https://github.com/flomesh-io/pipy/issues

# Translations

## [中文版](./README_zh.md)

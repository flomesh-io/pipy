![Pipy Logo](./gui/src/images/pipy-300.png)

[中文](./README_zh.md) | [日本語](./README_jp.md)

# Pipy

Pipy is a programmable proxy for the cloud, edge and IoT. Written in C++, Pipy is extremely lightweight and fast. It's also programmable by using PipyJS, a tailored version of the standard JavaScript language.

## Why Pipy?

### Versatile

Although Pipy is mostly used as a high-performance reverse proxy, the true power of Pipy relies on providing you a range of pluggable building blocks, aka. filters, without imposing any restrictions on how you combine them. It's entirely up to you as to how you want to use Pipy. We've seen people using Pipy as a protocol converter, traffic recorder, message signer/verifier, serverless function starter, health checker and more.

### Fast

Pipy is written in C++. It leverages asynchronous networking. Allocated resources are pooled and reused. Data is transferred internally by pointers whenever possible, minimizing the pressure on memory bandwidth. It's fast in every way.

### Tiny

A build of Pipy worker-mode instance gives you an executable merely around 10MB with zero external dependencies. You'll experience the fastest download and startup times with Pipy.

### Programmable

At the core, Pipy is a script engine running PipyJS, a tailored version of ECMA standard JavaScript. By speaking the planet's most widely used programming language, Pipy gives you unparalleled expressiveness over what you have in YAML or the like.

### Open

Pipy is more open than open source. It doesn’t try to hide every detail in the black box, so you always know what you are doing. But never fear. It doesn’t require a rocket scientist to figure out how the different parts work together. In fact, you’ll have more fun as you have full control over everything.

## Quick Start

### Build

The following prerequisites are required before building:

* Clang 5.0+
* CMake 3.10+
* Node.js v12+ (if the builtin _Admin UI_ is enabled)

Run the build script to start building:

```
./build.sh
```

The final product will be left at `bin/pipy`.

### Run

Run `bin/pipy` with zero command line options, Pipy will start in repo-mode listening on default port 6060.

```
$ bin/pipy

[INF] [admin] Starting admin service...
[INF] [listener] Listening on port 6060 at ::
```

Open the browser of your choice, point it to `http://localhost:6060`. You will now see the _Admin UI_ where you can start exploring the documentation and playing around with the tutorial codebases.

## Documentation

* [Overview](https://flomesh.io/docs/intro/overview)
* [Getting started](https://flomesh.io/docs/getting-started/build-install)
* [Tutorials](https://flomesh.io/docs/tutorial/01-hello)
* Reference
  * [API Reference](https://flomesh.io/docs/reference/api)
  * [PipyJS Reference](https://flomesh.io/docs/reference/pjs)
* Learnings & Articles
  * [Katacoda](https://katacoda.com/flomesh-io) - Katacoda scenarios
  * [InfoQ article](https://www.infoq.com/articles/network-proxy-stream-processor-pipy/) - Brief Introduction
* [Copyright](COPYRIGHT)
* [Licence](LICENCE)

## Compatibility

Pipy is being constantly tested on these platforms:

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

Pipy can run on the following architectures:

* X86/64
* ARM64
* LoongArch
* Hygon

## Copyright & License

Please refer to [COPYRIGHT](https://github.com/flomesh-io/pipy/blob/main/COPYRIGHT) and [LICENCE](https://github.com/flomesh-io/pipy/blob/main/LICENCE).

## Contact

* For security issues, please email to security@flomesh.io
* For legal issues, please email to legal@flomesh.io
* For commercial, sales and marketing topics, please email to sales@flomesh.io
* For other topics not suitable for the public, please email to pipy@flomesh.io
* For public discussions, please go to GitHub issues: https://github.com/flomesh-io/pipy/issues


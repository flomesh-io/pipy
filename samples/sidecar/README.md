## Service Mesh Sidecar

This example demonstrates how to use Pipy as a service mesh sidecar, which provides client-side load balancing and handles inbound and outbound traffic.

The code used in this example comes from [Flomesh Service Mesh](https://github.com/flomesh-io/osm-edge/tree/main/pkg/sidecar/providers/pipy/repo/codebase) (versions may differ).

**As service mesh sidecar mode requires coordination with the control plane to automate configuration, this example is only for reference. It is recommended to refer to the [Service Mesh Quick Start Guide](https://osm-edge-docs.flomesh.io/docs/quickstart/) for practical experience.**

## Example Introduction

- [`main.js`](./main.js): Program entry
- [`config.json`](./config.json): Configuration file
- [`config.js`](./config.js): Configuration file processor
- [`modules`](./plugins/): Module list
  - `inbound—xxx`: Inbound traffic processing module
  - `outbound-xxx`: Outbound traffic processing module

## Running

```shell
$ pipy main.js
```

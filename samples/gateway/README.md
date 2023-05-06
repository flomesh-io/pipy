## Modularized Reverse Proxy

This example is based on the [Reverse Proxy](../serve/) example, but with added modularity and features such as JWT, configurable load balancer, caching, logging, and metrics.

Modular design breaks down a large program into smaller modules that can be developed and maintained independently. For Pipy programming, modules are maintained in a single js file that can be independently split. Introducing modularity can improve development efficiency, enhance code readability and maintainability, and with plugin design, can increase code reusability.

## Example Introduction

- [`main.js`](./main.js) Program entry point
- [`config.json`](./config.json) Configuration file
- [`config.js`](./config.js) Configuration file processor
- [`plugins`](./plugins/) Module list
  - `metrics.js` Metrics module
  - `router.js` Router module
  - `serve-files.js` Static file module
  - `balancer.js` Load balancing module
  - `cache.js` Cache module
  - `default.js` 404 module for unhandled routes
  - `hello.js` Hello web service module
  - `jwt.js` JWT authentication module
  - `logging.js` Logging module
- [`secret`](./secret/) Public and private keys used for JWT authentication
- [`home`](./home/) Static file directory

### Configuration Explanation

In [`config.json`](./config.json), the reverse proxy can be configured, and here are some of the configurations:

- `plugins`: Configure the modules to use and adjust the execution order of the modules by adjusting their position in the list.
- `endpoints`: Routing configuration
- `cache`: Cache configuration, including file extensions that need to be cached and cache expiration time.
- `jwt`: JWT authentication configuration, mainly configuring the private key available for authentication.
- `log`: Log configuration. In this example, remote logging is used, which sends logs to a remote logging system via HTTP. Configure the logging system's access address and batch size here.

## Running

```shell
$ pipy main.js
```

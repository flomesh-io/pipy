## Reverse Proxy

This example uses Pipy to implement an HTTP reverse proxy with two functionalities: serving static files and proxying upstream applications. It also provides both HTTP and HTTPS access.

## Example Description

- The `www` directory contains static files.
- The `secret` directory contains the certificate and key files for HTTPS access.

### Configuration

The [`config.json`](./config.json) file defines an HTTP proxy:

- `listen`: Configures the proxy to listen on ports `8080` (TCP) and `8443` (TLS).
- `services`: Configures the proxy services.
  - The `foo` service is a `files` type service for serving static files.
    - `www`: The directory containing the static files.
  - The `bar` service is a `proxy` type service for proxying upstream applications.
    - `targets`: The list of upstream application addresses.
    - `rewrite`: The path rewrite rule.
- `routes`: Configures the proxy routes.

### Running

Start three upstream services:

```shell
$ nohup pipy -e "pipy().listen(9000).serveHTTP(()=>new Message('9000\n'))" &
$ nohup pipy -e "pipy().listen(9001).serveHTTP(()=>new Message('9001\n'))" &
$ nohup pipy -e "pipy().listen(9002).serveHTTP(()=>new Message('9002\n'))" &
```

Run the proxy:

```shell
$ pipy main.js
```

Test:

```shell
$ curl localhost:8080/ # Returns the static page.
$ curl localhost:8080/api # Returns 9000.
$ curl localhost:8080/api # Returns 9001.
$ curl localhost:8080/api # Returns 9002.
```

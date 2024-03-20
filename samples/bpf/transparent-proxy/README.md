# Sample: Transparent proxy by using BPF

This sample project demonstrates how Pipy can proxy outgoing TCP traffic transparently on socket level by using a combination of 3 types of eBPF programs:

- BPF_PROG_TYPE_CGROUP_SOCK_ADDR - Intercepts connect() calls from user applications
- BPF_PROG_TYPE_SOCK_OPS - Receives connection establishment notifications and maps local ports to real destinations
- BPF_PROG_TYPE_CGROUP_SOCKOPT - Intercepts getsockopt() calls and returns real destinations to the application

## Build the eBPF programs

```sh
make
```

The final product is `transparent-proxy.o` and will be loaded by `main.js` later on.

## Start the script

```sh
sudo pipy main.js
```

Or, if `pipy` is visible in your *$PATH*, you can simply do:

```sh
sudo ./main.js
```

The script loads the 3 eBPF programs and attaches them to the root cgroup. It also sets up a dedicated cgroup for itself so that its own outgoing traffic is exempted from being redirected by the eBPF programs.

Once the eBPF part is up and running, the script starts listening on port 18000 for all outgoing traffic redirected by eBPF, and then forwards the traffic to the original destinations. While it's forwarding the traffic, it also tries to decode plain HTTP/1 traffic and print out the requests and responses if any.

To see the effect, try `curl` a plain HTTP/1 website such as *bing.com*:

```sh
curl bing.com
```

You should be able to see Pipy logging the above request and response in the terminal.

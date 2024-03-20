# Sample: Traffic interception by using BPF

This sample project demonstrates how Pipy can intercept TCP traffic on layer 3 by using an eBPF tc filter in direct-action mode.

## Build the BPF program

```sh
make
```

The final product is `port-interceptor.o` and will be loaded by `main.js` later on.

## Start the interceptor

The interceptor script works as a BPF agent, whose responsibility is loading the BPF program and operating its maps.

```sh
sudo pipy main.js
```

Or, if `pipy` is visible in your *$PATH*, you can simply do:

```sh
sudo ./main.js
```

> NOTE: The script needs `sudo` because it needs to call `tc` for hooking the BPF program into the datapath in kernel, which requires administrator privileges.

The script finds out all network interfaces via *Netlink* and watches their changes afterwards if any. It automatically installs the BPF program to all network interfaces except *loopback* ones.

The script also keeps watching `config.yaml` for any changes regarding the ports to intercept and updating the BPF maps accordingly. As an example, the following configuration will capture all *non-loopback* traffic to ports 8000 and 8443, and redirect it to ports 9000 and 9443 respectively.

```yaml
interceptors:
  - port: 9000
    originalPort: 8000
  - port: 9443
    originalPort: 8443
```

## See it run in action

First, start a sample backend and a proxy.

```sh
pipy test-server.js
```

The sample backend listens on port 8000 and replies with a simple `"Hi"` in HTTP. The proxy listens on port 9000 and, while delegating all HTTP requests to port 8000, injects header `Server: Pipy interceptor` to all responses.

Now send a request from a remote address to port 8000. You can do this by using the included `test-curl.sh`, in which a veth is created and connected to a netns simulating a remote client.

If everything works out, you should see header `Server: Pipy interceptor` in responses from port 8000 although the origianl responses from the backend do not include that.

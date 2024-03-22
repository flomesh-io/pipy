# Sample: A basic eBPF program

This sample project demonstrates a most basic eBPF program for XDP. It barely does anything useful other than counting IP packets coming into the *lo* network interface, but it can be a good example showing a few basic tasks working with eBPF:

- Writing and compiling a BPF program in C that runs in the kernel
- Loading the BPF program in Pipy
- Communication between PipyJS in user space and the BPF program in kernel space via BPF maps

## Compile the BPF program

```sh
make
```

The product from the compiler is `packet-counter.o`, which is an object file containing BPF bytecode and type info of BPF maps (BTF).

## Start the user space script

```sh
sudo pipy main.js
```

> NOTE: The script needs `sudo` because loading and hooking up BPF programs into the kernel require administrator privileges.

The script loads the BPF program and parses BTF in order to allow PipyJS to access BPF map entries as plain JS objects. Once the BPF program is up and running, the script goes on and repeatedly prints packet statistics on the *lo* network interface.

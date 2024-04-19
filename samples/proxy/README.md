# Sample: A web proxy with TLS decryption

This sample demonstrates how to build a forward HTTP/HTTPS/SOCKS5 proxy that is capable of inspecting the content of TLS traffic by generating ad-hoc mock certificates.

## Run the script

```sh
pipy main.js
```

Or, if `pipy` is visible in `$PATH`, you can simply do:

```sh
./main.js
```

## Install the CA certificate

Once started, a CA certificate will be generated and printed out to the terminal. Install the certificate to your browser or system as a trusted CA. The details of how to do that depends on the browser or OS you are using.

If doing the above every time the script is run bothers you, you can just copy the printed private key and certificate into 2 files `ca.key` and `ca.crt` along with `main.js`. Next time the script is started, the same CA will be used and no new ones get generated.

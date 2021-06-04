# Generate a private key

```
openssl genrsa -out key.pem 2048
```

# Create a CSR

```
openssl req -new -sha256 -key key.pem -out csr.pem
```

# Create a self-signed certificate

```
openssl x509 -req -in csr.pem -signkey key.pem -out cert.pem
```


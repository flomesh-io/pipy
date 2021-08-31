# Generate CA Key

```
openssl genrsa -out ca-key.pem 4096
```

# Self-sign CA Certificate

```
openssl req -x509 -new -nodes -key ca-key.pem -sha256 -days 10000 -out ca-cert.pem
```

# Generate Server Key

```
openssl genrsa -out server-key.pem 2048
```

# Create Server CSR

```
openssl req -new -key server-key.pem -out server-csr.pem
```

# Sign with CA

```
openssl x509 -req -in server-csr.pem -CA ca-cert.pem -CAkey ca-key.pem -CAcreateserial -out server-cert.pem -days 10000 -sha256
```

# Generate Client Key

```
openssl genrsa -out client-key.pem 2048
```

# Create Client CSR

```
openssl req -new -sha256 -key client-key.pem -out client-csr.pem
```

# Self-sign Client Certificate

```
openssl x509 -req -in client-csr.pem -signkey client-key.pem -out client-cert.pem
```

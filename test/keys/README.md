# Test-only keys

**Never use these for real firmware.** Their private halves are public, in this repository, so
anyone can sign an image or pose as the server with them. They exist only for the on-device
firmware update test, `test/test_hw_ota`, on the test rig's private network.

| File | What it is |
|---|---|
| `test-signing-key.pem` | ECDSA P-256 Signing Key; `scripts/run-hw-tests.py` signs the test images with it |
| `test-ca.pem` | the test CA, which the test firmware trusts through `setRootCa()` |
| `test-server-cert.pem`, `test-server-key.pem` | the HTTPS certificate (for `10.42.0.1`, the rig's access point) and key that `run-hw-tests.py` serves the Manifests and images with |

`test/test_hw_ota/TestKeys.h` holds the Public Key and `test-ca.pem` as strings for the
firmware. The certificates are valid for 100 years; the CA's own key was thrown away after
signing the server certificate. To make a new set (for another server address, say), with
`openssl`:

```bash
scripts/sign-firmware.py new-key test/keys/test-signing-key.pem   # prints the Public Key
openssl ecparam -name prime256v1 -genkey -noout -out ca-key.pem
openssl req -new -x509 -key ca-key.pem -sha256 -days 36500 -out test/keys/test-ca.pem \
    -subj "/CN=EspManager rig test CA" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign"
openssl ecparam -name prime256v1 -genkey -noout -out test/keys/test-server-key.pem
openssl req -new -key test/keys/test-server-key.pem -subj "/CN=10.42.0.1" -out server.csr
printf 'subjectAltName=IP:10.42.0.1,DNS:10.42.0.1\nextendedKeyUsage=serverAuth\n' > ext.cnf
openssl x509 -req -in server.csr -CA test/keys/test-ca.pem -CAkey ca-key.pem -CAcreateserial \
    -days 36500 -sha256 -extfile ext.cnf -out test/keys/test-server-cert.pem
rm ca-key.pem server.csr ext.cnf test/keys/test-ca.srl
```

Then copy the Public Key and `test-ca.pem` into `TestKeys.h`.

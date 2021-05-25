## gRPC Security Configuration

[TOC]

It is important to note that by default the gRPC connections in MozoLM are not
secured to ease the development and, in particular, debugging and stress
testing. This insecure mode should not be used in the deployment settings.

To secure the communication channels between the MozoLM microservice and the
clients we make use of some of the security
[mechanisms](https://grpc.io/docs/guides/auth/) built-in into the gRPC, which we
describe below.

### SSL/TLS

The main means of secure authentication in MozoLM is
[Transport Security Layer (TLS)](https://en.wikipedia.org/wiki/Transport_Layer_Security),
a cryptographic protocol which succeeds an older Secure Sockets Layer (SSL)
protocol. Two authentication scenarios are currently supported:

1.  The clients authenticate the MozoLM server using the TLS certificate
    provided by the server and all the data exchanged between the client and the
    server is encrypted. This is useful for the cases when the server is not
    concerned about the identity of its clients.
1.  Extension to the previous mode implementing *mutual* authentication, whereby
    in addition to clients authenticating the server, the server is also
    required to authenticate the clients using the provided TLS certificate.
    This mode provides stronger security than the previous one.

#### Configuration

SSL/TLS authentication is configured using the corresponding server and client
configuration protocol buffers depending on the security mode. When not
mentioned explicitly, all the configuration fields below refer to string
*values* encoded in
[Privacy Enhanced Mail (PEM)](https://en.wikipedia.org/wiki/Privacy-Enhanced_Mail)
format.

##### Server Authentication

To configure the server authentication, on the server side one needs the server
public certificate and the corresponding private key, and, optionally, the
details of the Certificate Authority (CA), aka the *root* certificate, that were
used to generate server certificate. All that is required on the client side is
either the public certificate of the server *or* the public CA certificate of
the entity that generated it.

###### Server

For server-side authentication the corresponding fields in the
[ServerConfig](../mozolm/grpc/server_config.proto)
protocol buffer need to be set for the server.

1.  `auth.credential_type`: (required) Set to `CREDENTIAL_TLS`.
1.  `auth.tls.client_verify`: (required) Set to `false`.
1.  `auth.tls.server_cert`: (required) The server public TLS certificate.
1.  `auth.tls.server_key`: (required) The private cryptographic key (only
    visible to the server).
1.  `auth.tls.server_ca_cert`: (optional) The certificate authority who has
    signed the `auth.tls.server_cert`.

Example:

```protocol-buffer
...
auth {
  credential_type: CREDENTIAL_TLS
  tls {
    server_cert:
      "-----BEGIN CERTIFICATE-----"
      "..."
      "-----END CERTIFICATE-----"
    server_key:
      "-----BEGIN RSA PRIVATE KEY-----"
      "..."
      "-----END RSA PRIVATE KEY-----"
    server_ca_cert:                     # <--- Optional.
      "-----BEGIN CERTIFICATE-----"
      "..."
      "-----END CERTIFICATE-----"
  }
}
...
```

###### Client

The client-side configuration
[ClientConfig](../mozolm/grpc/client_config.proto)
includes the corresponding server configuration as a sub-message. The following
fields need to be set in the client configuration:

1.  `server.auth.credential_type`: (required) Set to `CREDENTIAL_TLS`.
1.  `server.auth.tls.server_cert`: (required) The public server TLS certificate
    or the public CA certificate (aka the root certificate).
1.  `auth.tls.target_name_override`: (optional)
    [The Subject Alternative Name (SAN)](https://en.wikipedia.org/wiki/Subject_Alternative_Name)
    is an extension to the X.509 specification that allows users to specify
    additional host names for a single TLS certificate. Only use for testing,
    where the certificate may not necessarily be issued for a fixed server name.

Example:

```protocol-buffer
...
auth {
  tls {
    target_name_override: "*.test.example.com"
  }
}
server {
  auth {
    credential_type: CREDENTIAL_TLS
    tls {
      server_cert:
        "-----BEGIN CERTIFICATE-----"
        "..."
        "-----END CERTIFICATE-----"
    }
  }
}
...
```

##### Mutual Authentication

The mutual authentication requires each side of the communication channel to
present and verify the presented certificates.

###### Server

For mutual authentication the corresponding fields in the
[ServerConfig](../mozolm/grpc/server_config.proto)
protocol buffer need to be set:

1.  `auth.credential_type`: (required) Set to `CREDENTIAL_TLS`.
1.  `auth.tls.client_verify`: (required) Set to `true`.
1.  `auth.tls.server_cert`: (required) The server public TLS certificate.
1.  `auth.tls.server_key`: (required) The private cryptographic key (only
    visible to the server).
1.  `auth.tls.server_ca_cert`: (required) The certificate authority (CA) who has
    signed the *client* certificates the server expects.

Example:

```protocol-buffer
...
auth {
  credential_type: CREDENTIAL_TLS
  tls {
    server_cert:
      "-----BEGIN CERTIFICATE-----"
      "..."
      "-----END CERTIFICATE-----"
    server_key:
      "-----BEGIN RSA PRIVATE KEY-----"
      "..."
      "-----END RSA PRIVATE KEY-----"
    server_ca_cert:                     # <--- Mandatory: CA for client.
      "-----BEGIN CERTIFICATE-----"
      "..."
      "-----END CERTIFICATE-----"
  }
}
...
```

###### Client

The client-side configuration
[ClientConfig](../mozolm/grpc/client_config.proto)
includes the corresponding server configuration as a sub-message. The following
fields need to be set in the client configuration:

1.  `server.auth.credential_type`: (required) Set to `CREDENTIAL_TLS`.
1.  `server.auth.tls.server_cert`: (required) The public server TLS certificate.
1.  `server.auth.tls.server_ca_cert`: (optional) The certificate authority (CA)
    who has signed the *server* certificates the client expects.
1.  `auth.tls.client_cert`: (required) The public client TLS certificate.
1.  `auth.tls.client_key`: (required) The private client cryptographic key (only
    visible to the client).
1.  `auth.tls.target_name_override`: (optional)
    [The Subject Alternative Name (SAN)](https://en.wikipedia.org/wiki/Subject_Alternative_Name)
    is an extension to the X.509 specification that allows users to specify
    additional host names for a single TLS certificate. Only use for testing,
    where the certificate may not necessarily be issued for a fixed server name.

Example:

```protocol-buffer
...
auth {
  tls {
    target_name_override: "*.test.example.com"
    client_cert:
      "-----BEGIN CERTIFICATE-----"
      "..."
      "-----END CERTIFICATE-----"
    client_key:
      "-----BEGIN RSA PRIVATE KEY-----"
      "..."
      "-----END RSA PRIVATE KEY-----"
  }
}
server {
  auth {
    credential_type: CREDENTIAL_TLS
    tls {
      server_cert:
        "-----BEGIN CERTIFICATE-----"
        "..."
        "-----END CERTIFICATE-----"
      server_ca_cert:                     # <--- Optional: CA for server.
        "-----BEGIN CERTIFICATE-----"
        "..."
        "-----END CERTIFICATE-----"
    }
  }
}
...
```

#### Command-line

Rather than keeping the PEM-encoded certificate and key values in client and
server configuration files it is possible to specify the original files that
contain these values via the command-line flags. In this case the client and
server configuration is updated accordingly with the PEM values read from the
supplied files.

##### Example Data

The example data can be found under the
[mozolm/grpc/testdata/cred/x509](../mozolm/grpc/testdata/cred/x509/)
directory that contains:

*   For client and server:
    *   Self-signed Central Authority (CA) certificates and private keys.
    *   Certificates and private keys generated using the above CA.
*   A simple
    [tool](../mozolm/grpc/testdata/cred/x509/create.sh)
    to re-generate and verify the certificates and keys using
    [OpenSSL](https://www.openssl.org/).

For more information on this setup please consult a similar test data
configuration in
[gRPC-Go](https://github.com/grpc/grpc-go/tree/master/testdata/x509), the Go
implementation of gRPC.

##### Server Authentication

###### Server

Example:

```shell
> bazel-bin/mozolm/grpc/server_async \
  --server_config_file /tmp/server_config.textproto \
  --tls_server_key_file mozolm/grpc/testdata/cred/x509/server1_key.pem \
  --tls_server_cert_file mozolm/grpc/testdata/cred/x509/server1_cert.pem \
  --notls_client_verify
Listening on "localhost:0"
Selected port: 38039
Waiting for requests ...
...
```

###### Client

Example (with server certificate):

```shell
bazel-bin/mozolm/grpc/client_async \
  --client_config="server { address_uri:\"localhost:38039\" } request_type:RANDGEN" \
  --tls_server_cert_file mozolm/grpc/testdata/cred/x509/server1_cert.pem \
  --tls_target_name_override "*.test.example.com"
```

Example (with CA certificate):

```shell
bazel-bin/mozolm/grpc/client_async \
  --client_config="server { address_uri:\"localhost:38039\" } request_type:RANDGEN" \
  --tls_server_cert_file mozolm/grpc/testdata/cred/x509/server_ca_cert.pem \
  --tls_target_name_override "*.test.example.com"
```

##### Mutual Authentication

For mutual authentication of the channel the server needs to be aware of the
client CA and the client needs to know about the server CA or any other
certificate which is signed by the server's CA. A simpler example (omitted in
this documentation) involves a single root certificate that is used to sign both
client and server certificates, in which case that single CA certificate can be
shared by both client and server.

###### Server

Example:

```shell
> bazel-bin/mozolm/grpc/server_async \
    --server_config_file /tmp/server_config.textproto \
    --tls_server_key_file mozolm/grpc/testdata/cred/x509/server1_key.pem \
    --tls_server_cert_file mozolm/grpc/testdata/cred/x509/server1_cert.pem \
    --tls_custom_ca_cert_file mozolm/grpc/testdata/cred/x509/client_ca_cert.pem \
    --tls_client_verify
```

###### Client

Example:

```shell
> bazel-bin/mozolm/grpc/client_async \
    --client_config="server { address_uri:\"localhost:34761\" } request_type:RANDGEN" \
    --tls_server_cert_file mozolm/grpc/testdata/cred/x509/server1_cert.pem \
    --tls_target_name_override "*.test.example.com" \
    --tls_client_cert_file mozolm/grpc/testdata/cred/x509/client1_cert.pem \
    --tls_client_key_file mozolm/grpc/testdata/cred/x509/client1_key.pem
```

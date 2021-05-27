## Tools for Releasing Bazel Container Images

The code in this directory is under construction. Please don't use yet.

Once the container images in this directory are built using
`build_containers.sh` and made available to Docker, the server can be run as
follows (the example below uses dummy configuration):

```shell
> docker images
REPOSITORY      TAG                         IMAGE ID       CREATED        SIZE
...
bazel/release   mozolm_client_async_image   396ca3152bda   51 years ago   16.7MB
bazel/release   mozolm_server_async_image   7f0d0d54b838   51 years ago   17.3MB
...

> docker run --init --net=host bazel/release:mozolm_server_async_image \
   --server_config="address_uri:\"127.0.0.1:0\""
[libprotobuf INFO mozolm/models/model_factory.cc:45] Reading ...
[libprotobuf WARNING mozolm/models/simple_bigram_char_model.cc:108] No vocabulary and counts files specified.
[libprotobuf INFO mozolm/models/model_factory.cc:48] Model read in 0 msec.
[libprotobuf WARNING mozolm/grpc/server_helper.cc:61] Using insecure server credentials
[libprotobuf INFO mozolm/grpc/server_async_impl.cc:261] Listening on "127.0.0.1:0"
[libprotobuf INFO mozolm/grpc/server_async_impl.cc:263] Selected port: 41743
[libprotobuf INFO mozolm/grpc/server_helper.cc:68] Waiting for requests ...
```

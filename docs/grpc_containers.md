## Running gRPC Microservice using Containers

[TOC]

MozoLM uses [Bazel](https://bazel.build/) as a default build system. While using
Bazel to build MozoLM from scratch on the supported platform is straightforward,
it may take a considerable amount of time dependending on the build host
hardware specification because there are quite a few dependencies that the
library relies upon.

### Using the Latest Images from Google Container Registry

In order to circumvent potentially lengthy and resource-intensive building from
source, the project provides several pre-built Linux container images hosted on
[Google Container Registry](https://cloud.google.com/container-registry) (GCR)
that can be executed on the platform of interest using any container runner that
supports [Open Containers Initiative](https://opencontainers.org/) (OCI) format.

In the examples below we use [Docker](https://www.docker.com/) which you may
need to install.

#### Pulling the Images

The images are hosted in `gcr.io/mozolm-release` repository. To pull the latest
server and client images execute the following Docker commands:

```shell
> docker pull gcr.io/mozolm-release/server_async:latest
latest: Pulling from mozolm-release/server_async
...
Digest: sha256:3f4f53227d08f12505c0253d57fa0802136e5020541a8801d0804bd9fd0413ff
Status: Downloaded newer image for gcr.io/mozolm-release/server_async:latest
gcr.io/mozolm-release/server_async:latest

> docker pull gcr.io/mozolm-release/client_async:latest
latest: Pulling from mozolm-release/client_async
...
Digest: sha256:b0c0a123955aeb191409340a4135901d0fcb05e528cc38c077fdcc97001714b6
Status: Downloaded newer image for gcr.io/mozolm-release/client_async:latest
gcr.io/mozolm-release/client_async:latest
```

To verify the downloaded images:

```shell
> docker images
REPOSITORY                           TAG       IMAGE ID       CREATED        SIZE
...
gcr.io/mozolm-release/server_async   latest    b84babf560c0   51 years ago   17.3MB
gcr.io/mozolm-release/client_async   latest    3b3f25063390   51 years ago   16.7MB
...
```

Please note, the sizes of the container images in the above example are shown
for the demonstration purposes only and are subject to change. Nevertheless they
are roughly in the ballpark of what to expect in terms of the container sizes.

#### Running the Images

The images should be runnable via Docker on the platform of your choice, such as
MacOS X, Windows or Linux.

In the example below, the MozoLM service will serve a 4-gram PPM model trained
on startup from a file in `/data/training.txt` in the container filesystem that
corresponds to the physical file `~/training.txt` in a directory on a host
machine mounted by Docker using `-v` option:

```shell
> docker run --init --net=host -v ~/:/data gcr.io/mozolm-release/server_async \
   --server_config="address_uri:\"127.0.0.1:55959\" model_hub_config { model_config { \
     type:PPM_AS_FST storage { model_file:\"/data/training.txt\" ppm_options { \
     max_order: 4 static_model: false } } } }"
[libprotobuf INFO mozolm/models/model_factory.cc:45] Reading ...
[libprotobuf INFO mozolm/models/ppm_as_fst_model.cc:406] Loading from "/data/training.txt" ...
[libprotobuf INFO mozolm/models/ppm_as_fst_model.cc:412] Constructing ...
[libprotobuf INFO mozolm/models/ppm_as_fst_model.cc:500] Constructed in 315 msec.
[libprotobuf INFO mozolm/models/model_factory.cc:48] Model read in 317 msec.
[libprotobuf WARNING mozolm/grpc/server_helper.cc:61] Using insecure server credentials
[libprotobuf INFO mozolm/grpc/server_async_impl.cc:261] Listening on "127.0.0.1:55959"
[libprotobuf INFO mozolm/grpc/server_helper.cc:68] Waiting for requests ...
```

### Troubleshooting

#### Authenticating with GCR using GCP

The container images above are publicly available and should not require
authentication with Google Container Registry. In an unlikely case that they do,
please install [Google Cloud SDK](https://cloud.google.com/sdk/docs/install),
also available as Docker [image](https://hub.docker.com/r/google/cloud-sdk/),
and authenticate via

```shell
docker login -u _token -p "$(gcloud auth print-access-token)" https://gcr.io
```

or follow the Google Container Registry authentication
[instructions](https://cloud.google.com/container-registry/docs/advanced-authentication).

## Example MozoLM client in Java

### Build the binaries with Bazel (or Bazelisk)

Build the client and server binaries using Bazel or
[Bazelisk](https://github.com/bazelbuild/bazelisk):

1.  Build the C++ gRPC server using Bazel:

    ```shell
    bazel build -c opt mozolm/grpc:server_async
    ```

1.  In a different terminal window Build the Java example clients in this
    directory:

    ```shell
    bazel build -c opt mozolm/java/com/google/mozolm/examples/...
    ```

### Server providing initially trained dynamic model

Please note, the example below runs the dynamic PPM model which requires the
training data to bootstrap itself at server initialization time. You may need to
specify a larger training file than the one used in this example for more
accurate estimates.

To run this example execute the following steps:

1.  Start the server serving the PPM model at the port specified by the
    environment variable `PORT`:

    ```shell
    PORT=50055
    TEXTFILE=mozolm/models/testdata/en_wiki_1Kline_sample.txt
    bazel-bin/mozolm/grpc/server_async \
      --server_config="address_uri:\"localhost:${PORT}\" \
         model_hub_config { model_config { type:PPM_AS_FST storage \
         { model_file:\"$TEXTFILE\" ppm_options { max_order: 4 static_model: false } \
         } } }"
    ```

1.  Run the client against the server:

    ```shell
    PORT=50055
    bazel-bin/mozolm/java/com/google/mozolm/examples/SimpleClientExample \
      localhost:${PORT}
    ```

    The above invocation sends a simple query and displays the top five
    hypotheses.

#### Windows Cmd equivalents for above

```shell
    set PORT=50055
    set TEXTFILE=mozolm\\models\\testdata\\en_wiki_1Kline_sample.txt
    bazel-bin\mozolm\grpc\server_async \
      --server_config="address_uri:""localhost:%PORT%"" \
        model_hub_config { model_config { \
        type:PPM_AS_FST storage { model_file:""%TEXTFILE%"" ppm_options { \
        max_order: 4 static_model: false } } } }"
```

```shell
    set PORT=50055
    bazel-bin\mozolm\java\com\google\mozolm\examples\SimpleClientExample localhost:%PORT%
```

### Server with no initial estimates (vocabulary only)

1.  Create a text newline-separated vocabulary file containing two entries `a`
    and `b`:

    ```shell
    VOCAB_FILE=/tmp/vocab_file.txt
    echo -e "a\nb" > ${VOCAB_FILE}
    ```

1.  Start the server serving the PPM model at the port specified by the
    environment variable `PORT`. Since the server was only initialized with the
    vocabulary file, the estimates are initialized to uniform distribution:

    ```shell
    PORT=50055
    bazel-bin/mozolm/grpc/server_async \
      --server_config="address_uri:\"localhost:${PORT}\" model_hub_config { \
      model_config { type:PPM_AS_FST storage { \
      vocabulary_file: \"${VOCAB_FILE}\" } } }"
    ```

1.  Run the client against the server:

    ```shell
    PORT=50055
    bazel-bin/mozolm/java/com/google/mozolm/examples/VocabOnlyModelClientExample \
      localhost:${PORT}
    ```

    The above invocation sends two updates with singleton observations of `a`
    and `b` to the server and prints the current model estimates for unigram
    symbols.

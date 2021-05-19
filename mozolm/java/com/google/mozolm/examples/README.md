## Example MozoLM client in Java

Please note, the example below runs the dynamic PPM model which requires the
training data to bootstrap itself at server initialization time. You may need
to specify a larger training file than the one used in this example for more
accurate estimates.

To run this example execute the following steps:

1.  Build the C++ gRPC server using Bazel:

    ```shell
    bazel build -c opt mozolm/grpc:server_async
    ```

1.  Start the server serving the PPM model at the port specified by the
    environment variable `PORT`:

    ```shell
    PORT=50055
    TEXTFILE=mozolm/data/en_wiki_1Kline_sample.txt
    bazel-bin/mozolm/grpc/server_async \
      --server_config="address_uri:\"localhost:${PORT}\" \
         model_hub_config { model_config { type:PPM_AS_FST storage \
         { model_file:\"$TEXTFILE\" ppm_options { max_order: 4 static_model: false } \
         } } }"
    ```

1.  In a different terminal window Build the Java example client in this directory:

    ```shell
    bazel build -c opt mozolm/java/com/google/mozolm/examples:SimpleClientExample
    ```

1.  Run the client against the server:

    ```shell
    PORT=50055
    bazel-bin/mozolm/java/com/google/mozolm/examples/SimpleClientExample \
      localhost:${PORT}
    ```

    The above invocation sends a simple query and displays the top five hypotheses.

### Windows Cmd equivalents for above

```shell
    set PORT=50055
    set TEXTFILE=mozolm\\data\\en_wiki_1Kline_sample.txt
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

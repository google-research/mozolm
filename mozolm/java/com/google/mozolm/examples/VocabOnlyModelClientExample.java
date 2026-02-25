// Copyright 2026 MozoLM Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Simple demonstration of MozoLM client API for the server initialized with
// vocabulary only.

package com.google.mozolm.examples;

import com.google.mozolm.LMScores;
import com.google.mozolm.grpc.GetContextRequest;
import com.google.mozolm.grpc.MozoLMServiceGrpc;
import com.google.mozolm.grpc.NextState;
import com.google.mozolm.grpc.UpdateLMScoresRequest;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import java.util.ArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * Simple gRPC client for communicating with MozoLM server.
 *
 * This client connects to the MozoLM server providing dynamic model which can
 * be started with vocabulary only, updates the model with several observations
 * and requests the probabilities of extending a given string context.
 */
final class VocabOnlyModelClientExample {
  private static final Logger logger = Logger.getLogger(
      VocabOnlyModelClientExample.class.getName());

  private VocabOnlyModelClientExample() {
  }

  public static void main(String[] args) throws InterruptedException {
    String serverSpec = "localhost:50051";
    if (args.length > 0) {
      if ("--help".equals(args[0])) {
        System.err.println("Usage: [serverSpec]");
        System.err.println("");
        System.err.println("  target  The server to connect to. Defaults to "
            + serverSpec);
        System.exit(1);
      }
      serverSpec = args[0];
    }
    new VocabOnlyModelClientExample().run(serverSpec);
  }

  private void run(String serverSpec) throws InterruptedException {
    info("Connecting to \"{0}\" ...", serverSpec);
    ExecutorService executor = Executors.newFixedThreadPool(1);
    ManagedChannel channel =
        ManagedChannelBuilder.forTarget(serverSpec).executor(executor).
        usePlaintext().build();
    try {
      final MozoLMServiceGrpc.MozoLMServiceBlockingStub blockingStub =
          MozoLMServiceGrpc.newBlockingStub(channel);

      // We should get back the uniform probability mass for the following three
      // symbols: '' (end-of-sentence), 'a' and 'b'.
      ArrayList<Pair<Double, String>> cands = getProbs(blockingStub,
          0, "");
      info("Initial counts ...");
      for (int i = 0; i < cands.size(); ++i) {
        info("===> {0}: \"{1}\" ({2})", i, cands.get(i).getSecond(),
            cands.get(i).getFirst());
      }

      // Update the model.
      updateModel(blockingStub, "aaaaabbbbbbbbbbbbbbbbbbbbbb");

      // Retrieve the estimates after updating the model. These should be
      // different from the initial estimates.
      info("Final counts ...");
      cands = getProbs(blockingStub, 0, "");
      for (int i = 0; i < cands.size(); ++i) {
        info("===> {0}: \"{1}\" ({2})", i, cands.get(i).getSecond(),
            cands.get(i).getFirst());
      }
     } finally {
      info("Shutting down ...");
      channel.shutdownNow().awaitTermination(5, TimeUnit.SECONDS);
    }
  }

  /**
   * Updates the model with `count` observations for `symbol`. Fetches probabilities for all the
   * symbols continuing the initial state.
   */
  private ArrayList<Pair<Double, String>> updateModel(
      MozoLMServiceGrpc.MozoLMServiceBlockingStub stub, String symbols) {
    // Send the sentence updating the counts character-by-character.
    final long initialState = 0;
    long state = initialState;
    for (int pos = 0; pos < symbols.length(); ++pos) {
      // Update the count of a given character (codepont) at the current `state`.
      final int codepointAtPos = Character.codePointAt(symbols, pos);
      final UpdateLMScoresRequest request = UpdateLMScoresRequest.newBuilder()
                                                .setState(state)
                                                .addUtf8Sym(codepointAtPos)
                                                .setCount(1)
                                                .build();
      stub.updateLMScores(request);
      // Advance the state given the character at this position.
      final String characterAtPos = Character.toString(symbols.charAt(pos));
      final GetContextRequest contextRequest =
          GetContextRequest.newBuilder().setState(state).setContext(characterAtPos).build();
      final NextState nextState = stub.getNextState(contextRequest);
      state = nextState.getNextState();
    }
    // Update end-of-string character.
    @SuppressWarnings("CharacterGetNumericValue")
    final UpdateLMScoresRequest request =
        UpdateLMScoresRequest.newBuilder().setState(state).addUtf8Sym(0).setCount(1).build();
    stub.updateLMScores(request);
    return getProbs(stub, initialState, "");
  }

  /**
   * Fetches the probabilities of being in the `state` corresponding to the
   * `context` given the blocking  client connection `stub`.
   */
  private ArrayList<Pair<Double, String>> getProbs(
      MozoLMServiceGrpc.MozoLMServiceBlockingStub stub, long state, String context) {
    final LMScores scores = stub.getLMScores(GetContextRequest.newBuilder()
        .setState(state)
        .setContext(context)
        .build());
    final int vocabSize = scores.getSymbolsCount();
    if (vocabSize != scores.getProbabilitiesCount()) {
      throw new AssertionError("Sizes of vocabulary and probability "
          + "containers should match");
    }
    ArrayList<Pair<Double, String>> symbolProbs =
        new ArrayList<Pair<Double, String>>(vocabSize);
    for (int i = 0; i < vocabSize; ++i) {
      symbolProbs.add(Pair.createPair(
          scores.getProbabilities(i), scores.getSymbols(i)));
    }
    return symbolProbs;
  }

  private void info(String msg, Object... params) {
    logger.log(Level.INFO, msg, params);
  }

  /**
   * Custom cheap and cheerful implementation of a pair.
   */
  static class Pair<K, V> {
    private final K first;
    private final V second;

    public static <K, V> Pair<K, V> createPair(K key, V value) {
      return new Pair<K, V>(key, value);
    }

    public Pair(K first, V second) {
      this.first = first;
      this.second = second;
    }

    public K getFirst() {
      return first;
    }

    public V getSecond() {
      return second;
    }
  }
}

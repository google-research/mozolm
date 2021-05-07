// Copyright 2021 MozoLM Authors.
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

// Simple demonstration of MozoLM client API.

package com.google.mozolm.examples;

import com.google.mozolm.LMScores;
import com.google.mozolm.grpc.GetContextRequest;
import com.google.mozolm.grpc.MozoLMServerGrpc;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * Simple gRPC client for communicating with MozoLM server.
 *
 * This client connects to the MozoLM server and requests the probabilities of
 * extending a given string context.
 */
final class SimpleClientExample {
  private static final Logger logger = Logger.getLogger(
      SimpleClientExample.class.getName());

  private SimpleClientExample() {
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
    new SimpleClientExample().run(serverSpec);
  }

  private void run(String serverSpec) throws InterruptedException {
    info("Connecting to \"{0}\" ...", serverSpec);
    ExecutorService executor = Executors.newFixedThreadPool(1);
    ManagedChannel channel =
        ManagedChannelBuilder.forTarget(serverSpec).executor(executor).
        usePlaintext().build();
    try {
      final MozoLMServerGrpc.MozoLMServerBlockingStub blockingStub =
          MozoLMServerGrpc.newBlockingStub(channel);
      final ArrayList<Pair<Double, String>> topCands = getTopCandidates(
          blockingStub, "Hello wo");
      // For the "Hello wo..." context above, the next batch of hypotheses
      // should (for a decent language model) contain letter "r".
      final int numTopCands = 5;
      for (int i = 0; i < numTopCands; ++i) {
        info("Candidate {0}: {1} ({2})", i, topCands.get(i).getSecond(),
            topCands.get(i).getFirst());
        if (topCands.get(i).getSecond().equals("r")) {
          info("===> Correct hypothesis found.");
        }
      }
    } finally {
      info("Shutting down ...");
      channel.shutdownNow().awaitTermination(5, TimeUnit.SECONDS);
    }
  }

  /** Fetches probabilities for all the symbols continuing the supplied context. */
  private ArrayList<Pair<Double, String>> getTopCandidates(
      MozoLMServerGrpc.MozoLMServerBlockingStub stub, String contextString)
      throws AssertionError {
    final int initialState = -1;
    final GetContextRequest request = GetContextRequest.newBuilder()
                                      .setState(initialState)
                                      .setContext(contextString)
                                      .build();
    final LMScores scores = stub.getLMScores(request);
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
    symbolProbs.sort(new ProbComparator());
    return symbolProbs;
  }

  private void info(String msg, Object... params) {
    logger.log(Level.INFO, msg, params);
  }

  /** Compares by decreasing probabilities. */
  static class ProbComparator implements Comparator<Pair<Double, String>> {
    @Override
    public int compare(Pair<Double, String> a, Pair<Double, String> b) {
      return -Double.compare(a.getFirst(), b.getFirst());
    }
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

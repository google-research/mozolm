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

#ifndef MOZOLM_MOZOLM_GRPC_AUTH_TEST_UTILS_H_
#define MOZOLM_MOZOLM_GRPC_AUTH_TEST_UTILS_H_

#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"

namespace mozolm {
namespace grpc {
namespace test {

// Test data for SSL/TLS credentials.
const char kTlsCredTestDir[] =
    "com_google_mozolm/mozolm/grpc/testdata/cred/x509";
const char kTlsServerPrivateKeyFile[] = "server1_key.pem";
const char kTlsServerPublicCertFile[] = "server1_cert.pem";
const char kTlsServerCentralAuthCertFile[] = "server_ca_cert.pem";
const char kTlsClientPrivateKeyFile[] = "client1_key.pem";
const char kTlsClientPublicCertFile[] = "client1_cert.pem";
const char kTlsClientCentralAuthCertFile[] = "client_ca_cert.pem";
const char kTlsAltServerName[] = "*.test.example.com";

// Reads contents of the SSL/TLS credentials file identified by `filename` which
// is located in the `kTlsCredTestDir` directory.
void ReadTlsCredFileContents(std::string_view filename, std::string *contents);

// Reads all the SSL/TLS credentials under `kTlsCredTestDir` directory into a
// mapping between the filenames and their contents.
void ReadAllTlsCredentials(
    absl::flat_hash_map<std::string, std::string> *name2contents);

}  // namespace test
}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_GRPC_AUTH_TEST_UTILS_H_

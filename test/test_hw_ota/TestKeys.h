#pragma once

// The test-only keys of test/keys/, as the test firmware needs them. Never use them for real
// firmware: their private halves are public.

namespace testkeys
{

// The Public Key of test/keys/test-signing-key.pem.
constexpr const char* kPublicKey = R"(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEttJ+15S4w4fpBVwmIStHQcicYAz3
S/Cs/92u683hRxrknjFpXKH67gT6/2EWiHq3sBVIkxMDFoel6yrbPQ9jZg==
-----END PUBLIC KEY-----
)";

// test/keys/test-ca.pem, which issued the certificate run-hw-tests.py serves with.
constexpr const char* kRootCa = R"(-----BEGIN CERTIFICATE-----
MIIBfjCCASOgAwIBAgIJAKPBZnmLHkueMAoGCCqGSM49BAMCMCExHzAdBgNVBAMM
FkVzcE1hbmFnZXIgcmlnIHRlc3QgQ0EwIBcNMjYxMTEzMTczNDM5WhgPMjEyNjEw
MjAxNzM0MzlaMCExHzAdBgNVBAMMFkVzcE1hbmFnZXIgcmlnIHRlc3QgQ0EwWTAT
BgcqhkjOPQIBBggqhkjOPQMBBwNCAAQB77h3+pUzdjBxFXf5ZaB6TgaMptGnaqOm
3Vw7joiyGJOvfsBo/M4xncLu31LfEsIStJOUaSmHOhyh8ibXIyZDo0IwQDAPBgNV
HRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBBjAdBgNVHQ4EFgQUokZZRfwcsBGH
W9VqRE8bstks3hIwCgYIKoZIzj0EAwIDSQAwRgIhANHZPzBplZXkKG9mDGoGzz6j
qgNKvlhiyzLhaUQk4WWQAiEA/Tqs7LdF0u1KxBRH6FXLMMs9CMjpzuDYWqEUKKXf
y+0=
-----END CERTIFICATE-----
)";

}  // namespace testkeys

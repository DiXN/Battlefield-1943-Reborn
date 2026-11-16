// Protect your name (TekRantGaming, 15 Nov 2026: "a secure your username option ... prevents anyone else from taking
// that name ... store long term progression tracking for the user", with no email and no setup). The player's account
// is a recovery code: 20 characters (100 random bits) the game makes when the name is protected, keeps in the settings
// file and shows once (and saves to a text file). The code gives an Ed25519 key; the backend keeps only the public half,
// with the name (server/plasma.py, accounts). Each request that needs the account is signed over a one time challenge
// from the backend, so the code never leaves this PC and a request seen on the network cannot be used again.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace account {

constexpr int kCodeLength = 20;

// A new recovery code: 20 characters of Crockford's base32 (no I, L, O or U).
std::string NewCode();
// A code as typed (any case, spaces, dashes; O read as 0, I and L as 1): the 20 characters, or empty if not a code.
std::string NormalizeCode(std::string_view typed);
// "ABCDE FGHIJ KLMNO PQRST".
std::string FormatCode(const std::string& code);

struct Key {
  uint8_t seed[32] = {};
  uint8_t pub[32] = {};
};
// The account's key: the first half of SHA-512("BF1943 Reborn account\n" + code) as the Ed25519 seed.
Key KeyFromCode(const std::string& code);
std::string PublicHex(const Key& key);
// The Ed25519 signature of `message`, 128 hex characters.
std::string Sign(const Key& key, std::string_view message);

// The pieces, for the tests (tools/test_accounts.py checks them against OpenSSL).
void Sha512(const uint8_t* data, size_t size, uint8_t out[64]);
void PublicKey(const uint8_t seed[32], uint8_t pub[32]);
void SignRaw(const uint8_t seed[32], const uint8_t* message, size_t size, uint8_t sig[64]);
std::string Hex(const uint8_t* data, size_t size);

}  // namespace account

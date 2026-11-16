// Protect your name: the account's code, key and signatures (account_key.h). Ed25519 signing after TweetNaCl (Bernstein,
// van Gastel, Janssen, Lange, Schwabe, Smetsers; public domain), with our own SHA-512; only what signing needs (the
// backend checks the signatures: server/ed25519.py). The constants were computed (tools/test_accounts.py prints them
// again), not typed. Not constant time against a local attacker who can time this process; the code is in the same
// settings file anyway.
#include "account_key.h"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace account {
namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

bool RandomBytes(uint8_t* out, size_t size) {
#if defined(_WIN32)
  return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out, ULONG(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
  size_t got = 0;
  while (got < size) {
    const ssize_t n = getrandom(out + got, size - got, 0);
    if (n <= 0) return false;
    got += size_t(n);
  }
  return true;
#endif
}

// ---- SHA-512 (FIPS 180-4)
constexpr uint64_t kH[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
constexpr uint64_t kK[80] = {0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL, 0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

inline uint64_t Rotr(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

void Sha512Block(uint64_t h[8], const uint8_t* p) {
  uint64_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = 0;
    for (int j = 0; j < 8; ++j) w[i] = (w[i] << 8) | p[i * 8 + j];
  }
  for (int i = 16; i < 80; ++i) {
    const uint64_t s0 = Rotr(w[i - 15], 1) ^ Rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
    const uint64_t s1 = Rotr(w[i - 2], 19) ^ Rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 80; ++i) {
    const uint64_t t1 = hh + (Rotr(e, 14) ^ Rotr(e, 18) ^ Rotr(e, 41)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
    const uint64_t t2 = (Rotr(a, 28) ^ Rotr(a, 34) ^ Rotr(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
  }
  h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}

// ---- the field, 16 limbs of 16 bits (TweetNaCl's gf)
using i64 = int64_t;
using gf = i64[16];
const gf kGf0 = {0}, kGf1 = {1};
const gf kD2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const gf kX = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const gf kY = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
constexpr i64 kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

void Set(gf r, const gf a) {
  for (int i = 0; i < 16; ++i) r[i] = a[i];
}
void Carry(gf o) {
  for (int i = 0; i < 16; ++i) {
    o[i] += i64(1) << 16;
    const i64 c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c * 65536;
  }
}
void Select(gf p, gf q, int b) {
  const i64 c = ~i64(b - 1);
  for (int i = 0; i < 16; ++i) {
    const i64 t = c & (p[i] ^ q[i]);
    p[i] ^= t, q[i] ^= t;
  }
}
void Pack25519(uint8_t* o, const gf n) {
  gf m, t;
  Set(t, n);
  Carry(t), Carry(t), Carry(t);
  for (int j = 0; j < 2; ++j) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; ++i) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    const int b = int((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    Select(t, m, 1 - b);
  }
  for (int i = 0; i < 16; ++i) o[2 * i] = uint8_t(t[i] & 0xff), o[2 * i + 1] = uint8_t(t[i] >> 8);
}
int Parity(const gf a) {
  uint8_t d[32];
  Pack25519(d, a);
  return d[0] & 1;
}
void Add(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}
void Sub(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}
void Mul(gf o, const gf a, const gf b) {
  i64 t[31] = {};
  for (int i = 0; i < 16; ++i)
    for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; ++i) o[i] = t[i];
  Carry(o), Carry(o);
}
void Square(gf o, const gf a) { Mul(o, a, a); }
void Invert(gf o, const gf in) {
  gf c;
  Set(c, in);
  for (int a = 253; a >= 0; --a) {
    Square(c, c);
    if (a != 2 && a != 4) Mul(c, c, in);
  }
  Set(o, c);
}

// ---- points (extended coordinates)
void PointAdd(gf p[4], gf q[4]) {
  gf a, b, c, d, t, e, f, g, h;
  Sub(a, p[1], p[0]), Sub(t, q[1], q[0]), Mul(a, a, t);
  Add(b, p[0], p[1]), Add(t, q[0], q[1]), Mul(b, b, t);
  Mul(c, p[3], q[3]), Mul(c, c, kD2);
  Mul(d, p[2], q[2]), Add(d, d, d);
  Sub(e, b, a), Sub(f, d, c), Add(g, d, c), Add(h, b, a);
  Mul(p[0], e, f), Mul(p[1], h, g), Mul(p[2], g, f), Mul(p[3], e, h);
}
void PointSwap(gf p[4], gf q[4], int b) {
  for (int i = 0; i < 4; ++i) Select(p[i], q[i], b);
}
void PointPack(uint8_t* r, gf p[4]) {
  gf tx, ty, zi;
  Invert(zi, p[2]);
  Mul(tx, p[0], zi), Mul(ty, p[1], zi);
  Pack25519(r, ty);
  r[31] ^= uint8_t(Parity(tx) << 7);
}
void ScalarMult(gf p[4], gf q[4], const uint8_t* s) {
  Set(p[0], kGf0), Set(p[1], kGf1), Set(p[2], kGf1), Set(p[3], kGf0);
  for (int i = 255; i >= 0; --i) {
    const int b = (s[i / 8] >> (i & 7)) & 1;
    PointSwap(p, q, b);
    PointAdd(q, p);
    PointAdd(p, p);
    PointSwap(p, q, b);
  }
}
void ScalarBase(gf p[4], const uint8_t* s) {
  gf q[4];
  Set(q[0], kX), Set(q[1], kY), Set(q[2], kGf1), Mul(q[3], kX, kY);
  ScalarMult(p, q, s);
}

// ---- scalars modulo L
void ModL(uint8_t* r, i64 x[64]) {
  i64 carry;
  for (int i = 63; i >= 32; --i) {
    carry = 0;
    int j;
    for (j = i - 32; j < i - 12; ++j) {
      x[j] += carry - 16 * x[i] * kL[j - (i - 32)];
      carry = (x[j] + 128) >> 8;
      x[j] -= carry * 256;
    }
    x[j] += carry;
    x[i] = 0;
  }
  carry = 0;
  for (int j = 0; j < 32; ++j) {
    x[j] += carry - (x[31] >> 4) * kL[j];
    carry = x[j] >> 8;
    x[j] &= 255;
  }
  for (int j = 0; j < 32; ++j) x[j] -= carry * kL[j];
  for (int i = 0; i < 32; ++i) {
    x[i + 1] += x[i] >> 8;
    r[i] = uint8_t(x[i] & 255);
  }
}
void Reduce(uint8_t* r) {
  i64 x[64];
  for (int i = 0; i < 64; ++i) x[i] = i64(r[i]);
  for (int i = 0; i < 64; ++i) r[i] = 0;
  ModL(r, x);
}

}  // namespace

void Sha512(const uint8_t* data, size_t size, uint8_t out[64]) {
  uint64_t h[8];
  std::memcpy(h, kH, sizeof(h));
  size_t at = 0;
  for (; size - at >= 128; at += 128) Sha512Block(h, data + at);
  uint8_t last[256] = {};
  const size_t rest = size - at;
  std::memcpy(last, data + at, rest);
  last[rest] = 0x80;
  const size_t blocks = rest + 1 + 16 <= 128 ? 1 : 2;
  const uint64_t bits = uint64_t(size) * 8;
  for (int i = 0; i < 8; ++i) last[blocks * 128 - 1 - i] = uint8_t(bits >> (8 * i));
  for (size_t b = 0; b < blocks; ++b) Sha512Block(h, last + b * 128);
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 8; ++j) out[i * 8 + j] = uint8_t(h[i] >> (56 - 8 * j));
}

void PublicKey(const uint8_t seed[32], uint8_t pub[32]) {
  uint8_t d[64];
  gf p[4];
  Sha512(seed, 32, d);
  d[0] &= 248, d[31] &= 127, d[31] |= 64;
  ScalarBase(p, d);
  PointPack(pub, p);
}

void SignRaw(const uint8_t seed[32], const uint8_t* message, size_t size, uint8_t sig[64]) {
  uint8_t d[64], h[64], r[64], pub[32];
  gf p[4];
  Sha512(seed, 32, d);
  d[0] &= 248, d[31] &= 127, d[31] |= 64;
  ScalarBase(p, d);
  PointPack(pub, p);
  std::vector<uint8_t> buf(64 + size);
  std::memcpy(buf.data() + 32, d + 32, 32);  // r = H(prefix || M)
  if (size) std::memcpy(buf.data() + 64, message, size);
  Sha512(buf.data() + 32, 32 + size, r);
  Reduce(r);
  ScalarBase(p, r);
  PointPack(sig, p);  // R
  std::memcpy(buf.data(), sig, 32);  // h = H(R || A || M)
  std::memcpy(buf.data() + 32, pub, 32);
  Sha512(buf.data(), 64 + size, h);
  Reduce(h);
  i64 x[64] = {};
  for (int i = 0; i < 32; ++i) x[i] = i64(r[i]);
  for (int i = 0; i < 32; ++i)
    for (int j = 0; j < 32; ++j) x[i + j] += i64(h[i]) * i64(d[j]);
  ModL(sig + 32, x);  // S = r + h * a
}

std::string Hex(const uint8_t* data, size_t size) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) out += kDigits[data[i] >> 4], out += kDigits[data[i] & 15];
  return out;
}

std::string NewCode() {
  uint8_t bytes[kCodeLength];
  if (!RandomBytes(bytes, sizeof(bytes))) return {};
  std::string code;
  for (uint8_t b : bytes) code += kAlphabet[b & 31];  // 5 random bits a character: 100 bits
  return code;
}

std::string NormalizeCode(std::string_view typed) {
  std::string code;
  for (char c : typed) {
    if (c == ' ' || c == '-' || c == '\t') continue;
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    if (!std::strchr(kAlphabet, c) || c == 0) return {};
    code += c;
  }
  return int(code.size()) == kCodeLength ? code : std::string();
}

std::string FormatCode(const std::string& code) {
  std::string out;
  for (size_t i = 0; i < code.size(); ++i) {
    if (i && i % 5 == 0) out += ' ';
    out += code[i];
  }
  return out;
}

Key KeyFromCode(const std::string& code) {
  Key key;
  const std::string text = "BF1943 Reborn account\n" + code;
  uint8_t h[64];
  Sha512(reinterpret_cast<const uint8_t*>(text.data()), text.size(), h);
  std::memcpy(key.seed, h, 32);
  PublicKey(key.seed, key.pub);
  return key;
}

std::string PublicHex(const Key& key) { return Hex(key.pub, 32); }

std::string Sign(const Key& key, std::string_view message) {
  uint8_t sig[64];
  SignRaw(key.seed, reinterpret_cast<const uint8_t*>(message.data()), message.size(), sig);
  return Hex(sig, 64);
}

}  // namespace account

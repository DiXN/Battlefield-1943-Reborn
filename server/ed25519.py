"""Ed25519 (RFC 8032) in plain Python, for the players' accounts (plasma.py, "Protect your name"): the backend checks
a player's signature against the public key their name was claimed with. Our own code after the RFC's section 5.1;
no package to install on a server. Checked against OpenSSL (tools/test_accounts.py). Slow (a few ms a signature),
which is fine for one check at a sign in.

    public_key(seed)            -> 32 bytes   (a 32 byte seed: the private key)
    sign(seed, message)         -> 64 bytes
    verify(public, message, sig) -> bool
"""
import hashlib

P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _sha512(data):
    return hashlib.sha512(data).digest()


def _inv(x):
    return pow(x, P - 2, P)


# Points in extended coordinates (X, Y, Z, T), x = X/Z, y = Y/Z, x*y = T/Z.
def _add(a, b):
    A = (a[1] - a[0]) * (b[1] - b[0]) % P
    B = (a[1] + a[0]) * (b[1] + b[0]) % P
    C = 2 * a[3] * b[3] * D % P
    Dd = 2 * a[2] * b[2] % P
    E, F, G, H = B - A, Dd - C, Dd + C, B + A
    return (E * F % P, G * H % P, F * G % P, E * H % P)


def _mul(s, p):
    q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _equal(a, b):
    return (a[0] * b[2] - b[0] * a[2]) % P == 0 and (a[1] * b[2] - b[1] * a[2]) % P == 0


def _recover_x(y, sign):
    if y >= P:
        return None
    x2 = (y * y - 1) * _inv(D * y * y + 1) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_GY = 4 * _inv(5) % P
_GX = _recover_x(_GY, 0)
G = (_GX, _GY, 1, _GX * _GY % P)


def _compress(p):
    zinv = _inv(p[2])
    x, y = p[0] * zinv % P, p[1] * zinv % P
    return int.to_bytes(y | ((x & 1) << 255), 32, 'little')


def _decompress(s):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, 'little')
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % P)


def _expand(seed):
    if len(seed) != 32:
        raise ValueError('an Ed25519 seed is 32 bytes')
    h = _sha512(seed)
    a = int.from_bytes(h[:32], 'little')
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed):
    a, _ = _expand(seed)
    return _compress(_mul(a, G))


def sign(seed, message):
    a, prefix = _expand(seed)
    A = _compress(_mul(a, G))
    r = int.from_bytes(_sha512(prefix + message), 'little') % L
    R = _compress(_mul(r, G))
    h = int.from_bytes(_sha512(R + A + message), 'little') % L
    s = (r + h * a) % L
    return R + int.to_bytes(s, 32, 'little')


def verify(public, message, signature):
    try:
        if len(public) != 32 or len(signature) != 64:
            return False
        A = _decompress(public)
        if not A:
            return False
        R = _decompress(signature[:32])
        if not R:
            return False
        s = int.from_bytes(signature[32:], 'little')
        if s >= L:
            return False
        h = int.from_bytes(_sha512(signature[:32] + public + message), 'little') % L
        return _equal(_mul(s, G), _add(R, _mul(h, A)))
    except (TypeError, ValueError):
        return False

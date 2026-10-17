"""nn::util ResDic (the Patricia-trie name dictionary of BNTX / BFRES files).

Bits are numbered from the end of the key: bit b is bit (b & 7) of the character len-1-(b >> 3); bits past the
start of a shorter key read as 0. Entry 0 is the root (bit -1, its left link is the tree). A lookup walks from
the root's left child and stops when the bit index no longer grows.

build(keys) -> list of (bit, left, right) for entries 0..n (entry i + 1 is keys[i]).
"""


def _bit(key, b):
    i = b >> 3
    return 0 if i >= len(key) else (key[len(key) - 1 - i] >> (b & 7)) & 1


def _first_diff(a, b):
    for i in range(max(len(a), len(b)) * 8):
        if _bit(a, i) != _bit(b, i):
            return i
    return -1


def build(keys):
    keys = [k.encode() if isinstance(k, str) else k for k in keys]
    bit, left, right, key = [-1], [0], [0], [b'']
    for k in keys:
        # closest existing key
        prev, cur = 0, left[0]
        while bit[prev] < bit[cur]:
            prev, cur = cur, (right[cur] if _bit(k, bit[cur]) else left[cur])
        nb = _first_diff(k, key[cur])
        # insertion point: descend while the bit index grows and stays below nb
        prev, cur = 0, left[0]
        while bit[prev] < bit[cur] and bit[cur] < nb:
            prev, cur = cur, (right[cur] if _bit(k, bit[cur]) else left[cur])
        n = len(bit)
        bit.append(nb)
        key.append(k)
        if _bit(k, nb):
            left.append(cur); right.append(n)
        else:
            left.append(n); right.append(cur)
        if prev == 0:
            left[0] = n
        elif _bit(k, bit[prev]):
            right[prev] = n
        else:
            left[prev] = n
    return list(zip(bit, left, right))

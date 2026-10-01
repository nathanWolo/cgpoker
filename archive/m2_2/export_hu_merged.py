"""Export the heads-up MCCFR solution (solvers/hu/hu.cpp `grid`) as bot/hu_tables.hpp.

    python3 solvers/hu/export_hu.py [--grid data/hu/grid9b.bin [more.bin]] [--out bot/hu_tables.hpp] [--base 1.25]

Preflop nodes are stored per stack point with their full history, in the tree's depth-first order (the bot
rebuilds the tree from the rules and checks a count and a hash of the preflop histories per stack).
Postflop nodes are merged across stack points and betting lines on the key
    (street, pot bucket, stack-behind bucket, actions available, this street's history)
with pot and stack behind (BB) bucketed on a log grid (`--base`); merged nodes' probabilities are averaged.
The bot computes the same key from the real pot and stacks, so the opponent's actual bet sizes shape the
lookup rather than the abstract line's.  Probabilities are 4 bits each (q/15, renormalised), two per byte.

Payload layout (CJK14-packed):
    for each stack point, for each preflop decision node in DFS order: 169 x nact nibbles (byte-padded)
    for each postflop key: street(1) pot_idx(1) behind_idx(1) nact(1) acts(4) hist_len(1) hist(hist_len) then
        10 x nact nibbles (byte-padded)
"""
import argparse, collections, math, os, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
from cjk14 import encode_cjk14, decode_cjk14  # noqa: E402

MAXA = 4
ACT = ["F", "C", "R", "A", "K", "B", "b"]
IDX_OFF = 40


def load_grid(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"HUS1"
        ns = struct.unpack("<i", f.read(4))[0]
        thr = np.frombuffer(f.read(4 * 3 * 9), dtype="<f4").reshape(3, 9)
        stacks = []
        for _ in range(ns):
            S = struct.unpack("<d", f.read(8))[0]
            nn = struct.unpack("<i", f.read(4))[0]
            nodes = []
            for _ in range(nn):
                hl = struct.unpack("<i", f.read(4))[0]
                hist = f.read(hl).decode()
                player, street, term = struct.unpack("<3i", f.read(12))
                c = struct.unpack("<2d", f.read(16))
                nact = struct.unpack("<i", f.read(4))[0]
                act = struct.unpack(f"<{MAXA}i", f.read(4 * MAXA))
                child = struct.unpack(f"<{MAXA}i", f.read(4 * MAXA))
                nb = struct.unpack("<i", f.read(4))[0]
                probs = np.frombuffer(f.read(8 * MAXA * nb), dtype="<f8").reshape(nb, MAXA)
                nodes.append(dict(hist=hist, player=player, street=street, term=term, c=c, nact=nact, act=act[:nact], child=child[:nact], nb=nb, probs=probs[:, :nact]))
            stacks.append((S, nodes))
    return thr, stacks


def log_idx(x, base):
    return int(round(math.log(max(x, 0.5)) / math.log(base))) + IDX_OFF


def pack_probs(probs):
    q = np.clip(np.rint(probs * 15), 0, 15).astype(np.uint8).reshape(-1)
    if len(q) % 2:
        q = np.concatenate([q, np.zeros(1, np.uint8)])
    return bytes((q[0::2] << 4) | q[1::2])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--grid", nargs="+", default=[os.path.join(REPO, "data", "hu", "grid9b.bin")])
    ap.add_argument("--out", default=os.path.join(REPO, "bot", "hu_tables.hpp"))
    ap.add_argument("--base", type=float, default=1.25)
    a = ap.parse_args()
    thr, stacks = None, []
    for path in a.grid:
        t, st = load_grid(path)
        assert thr is None or np.allclose(t, thr), "grids from different pools"
        thr = t; stacks += st
    stacks.sort(key=lambda x: x[0])
    payload = bytearray()
    ndec, hashes = [], []
    merged = collections.OrderedDict()           # key -> [sum of probs, count]
    for S, nodes in stacks:
        h = 1469598103934665603
        cnt = 0
        for n in nodes:
            if n["term"]:
                continue
            if n["street"] == 0:
                for ch in n["hist"].encode():
                    h = ((h ^ ch) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
                h = ((h ^ 0x2F) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
                cnt += 1
                payload += pack_probs(n["probs"])
                continue
            pot = n["c"][0] + n["c"][1]
            behind = S - max(n["c"])
            acts = "".join(ACT[x] for x in n["act"])
            sh = n["hist"].split("/")[-1]
            key = (n["street"], log_idx(pot, a.base), log_idx(behind, a.base), acts, sh)
            if key not in merged:
                merged[key] = [np.zeros_like(n["probs"]), 0]
            merged[key][0] += n["probs"]; merged[key][1] += 1
        ndec.append(cnt); hashes.append(h)
    npre = len(payload)
    for (street, pi, bi, acts, sh), (ps, c) in merged.items():
        payload += bytes([street, pi, bi, len(acts)]) + acts.encode().ljust(4, b"\0") + bytes([len(sh)]) + sh.encode()
        payload += pack_probs(ps / c)
    text = encode_cjk14(bytes(payload))
    assert decode_cjk14(text)[:len(payload)] == bytes(payload)
    with open(a.out, "w") as f:
        f.write("#pragma once\n// hu_tables.hpp - heads-up 8-120 BB strategy from solvers/hu (MCCFR), exported by solvers/hu/export_hu.py.\n")
        f.write("// Preflop nodes per stack point (DFS order, hash-checked by bot/hu_play.hpp); postflop nodes merged on\n")
        f.write("// (street, pot bucket, stack-behind bucket, actions, this street's history), pot and stack on a log grid\n")
        f.write("// of base LOGBASE with offset IDX_OFF.  4-bit probabilities.  THR: EHS bucket thresholds per postflop street.\n")
        f.write("namespace hu_t {\n")
        f.write(f"const int NS = {len(stacks)};\nconst double STACKS[{len(stacks)}] = {{{', '.join(f'{S:g}' for S, _ in stacks)}}};\n")
        f.write(f"const double THR[3][9] = {{{', '.join('{' + ', '.join(f'{x:.4f}' for x in row) + '}' for row in thr)}}};\n")
        f.write(f"const int NDEC[{len(stacks)}] = {{{', '.join(map(str, ndec))}}};\n")
        f.write(f"const unsigned long long HASH[{len(stacks)}] = {{{', '.join(f'{x}ull' for x in hashes)}}};\n")
        f.write(f"const int NPRE = {npre}, NKEYS = {len(merged)}, IDX_OFF = {IDX_OFF};\nconst double LOGBASE = {a.base};\n")
        f.write(f'const char DATA[] = R"~({text})~";\n')
        f.write("}  // namespace hu_t\n")
    size = os.path.getsize(a.out)
    chars = len(open(a.out, encoding="utf-8").read())
    print(f"wrote {os.path.relpath(a.out, REPO)}: {len(stacks)} stacks, preflop {npre} bytes, {len(merged)} postflop keys from "
          f"{sum(c for _, c in merged.values())} nodes, {len(payload)} bytes packed, {chars} chars")


if __name__ == "__main__":
    main()

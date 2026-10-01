"""Export the heads-up MCCFR solution (solvers/hu/hu.cpp `grid`) as bot/hu_tables.hpp.

    python3 solvers/hu/export_hu.py [--grid data/hu/grid.bin] [--out bot/hu_tables.hpp]

Per stack point and decision node (full action history, perfect recall) the average strategy over the
node's buckets (169 preflop classes, NB EHS buckets postflop) is quantised to 4 bits per action
probability (0..15 -> p = q / 15, renormalised at runtime) and packed, with the tree (histories,
actions, commitments) in a compact text table and the EHS bucket thresholds.  CJK14-packed.
"""
import argparse, os, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
from cjk14 import encode_cjk14, decode_cjk14  # noqa: E402

MAXA = 4
ACT = ["F", "C", "R", "A", "K", "B"]


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--grid", default=os.path.join(REPO, "data", "hu", "grid.bin"))
    ap.add_argument("--out", default=os.path.join(REPO, "bot", "hu_tables.hpp"))
    a = ap.parse_args()
    thr, stacks = load_grid(a.grid)
    # the tree is the same shape for every stack point except where raises collapse into all-ins at short
    # stacks, so each stack carries its own node list; the bot finds a node by (stack index, history)
    # the bot rebuilds the tree from the same rules (bot/hu_play.hpp, HuTree); a node count and an FNV-1a hash of
    # the decision-node histories per stack let it check that its tree is this one
    ndec, hashes = [], []
    payload = bytearray()
    total_dec = 0
    for si, (S, nodes) in enumerate(stacks):
        h = 1469598103934665603
        cnt = 0
        for n in nodes:
            if n["term"]:
                continue
            for ch in n["hist"].encode():
                h = ((h ^ ch) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
            h = ((h ^ 0x2F) * 1099511628211) & 0xFFFFFFFFFFFFFFFF      # separator
            cnt += 1
            q = np.clip(np.rint(n["probs"] * 15), 0, 15).astype(np.uint8)   # [nb][nact]
            # pack 4-bit values, two per byte, action-major within a bucket
            flat = q.reshape(-1)
            if len(flat) % 2:
                flat = np.concatenate([flat, np.zeros(1, np.uint8)])
            payload += bytes((flat[0::2] << 4) | flat[1::2])
            total_dec += n["nb"]
        ndec.append(cnt); hashes.append(h)
    text = encode_cjk14(bytes(payload))
    assert decode_cjk14(text)[:len(payload)] == bytes(payload)
    with open(a.out, "w") as f:
        f.write("#pragma once\n// hu_tables.hpp - heads-up 8-30 BB strategy from solvers/hu (MCCFR), exported by solvers/hu/export_hu.py.\n")
        f.write("// DATA: per stack point, per decision node in the tree's depth-first order (bot/hu_play.hpp rebuilds the\n")
        f.write("// tree), nb buckets x nact actions, 4 bits each, two per byte.  NDEC / HASH: decision-node count and FNV-1a\n")
        f.write("// of the histories per stack, checked by the bot.  THR: EHS bucket thresholds per postflop street.\n")
        f.write("namespace hu_t {\n")
        f.write(f"const int NS = {len(stacks)};\nconst double STACKS[{len(stacks)}] = {{{', '.join(f'{S:g}' for S, _ in stacks)}}};\n")
        f.write(f"const double THR[3][9] = {{{', '.join('{' + ', '.join(f'{x:.4f}' for x in row) + '}' for row in thr)}}};\n")
        f.write(f"const int NDEC[{len(stacks)}] = {{{', '.join(map(str, ndec))}}};\n")
        f.write(f"const unsigned long long HASH[{len(stacks)}] = {{{', '.join(f'{x}ull' for x in hashes)}}};\n")
        f.write(f'const char DATA[] = R"~({text})~";\n')
        f.write("}  // namespace hu_t\n")
    size = os.path.getsize(a.out)
    chars = len(open(a.out, encoding="utf-8").read())
    print(f"wrote {os.path.relpath(a.out, REPO)}: {len(stacks)} stacks, {total_dec} decision rows, {len(payload)} bytes packed, {chars} chars")


if __name__ == "__main__":
    main()

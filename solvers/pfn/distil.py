"""Distil the solved push/fold grids (solvers/pfn/pfn.cpp `grid`) into bot/pfn_tables.hpp.

    python3 solvers/pfn/distil.py [--grids data/pfn/grid_3p3.bin ...] [--out bot/pfn_tables.hpp]

For every decision node the fictitious-play strategy at each stack configuration is replaced by a
threshold on one node-specific ranking of the 169 preflop classes (ranked by the class's average jam
frequency over the grid, ties by average EV gain of jamming): jam with the first T classes.  T is chosen
per grid point to minimise the EV lost against the solver's own values (evj - evf per class, in prize-pool
units); the loss is reported, node by node, next to the solver's own convergence gap (eps).

Layout of the packed bytes, per game (N players, NT the number the game started with):
    rank[node][169]   class index at each rank position
    thr[node][P]      threshold at each grid point (P = L^N, stack levels in action order, last index fastest)
The header carries the levels, the node ids ((1<<i)-1+prefix, prefix bit j = player j jammed) and one
CJK14 string per game (14 bits per character, tools/cjk14.py; the bot decodes it at start-up).
"""
import argparse, os, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
from cjk14 import encode_cjk14, decode_cjk14   # noqa: E402

K = 169
R = "23456789TJQKA"


def cname(c):
    r1, r2 = divmod(c, 13)
    if r1 == r2:
        return R[r1] * 2
    return (R[r1] + R[r2] + "s") if r1 > r2 else (R[r2] + R[r1] + "o")


def combos(c):
    r1, r2 = divmod(c, 13)
    return 6 if r1 == r2 else 4 if r1 > r2 else 12


P1 = np.array([combos(c) for c in range(K)], float) / 1326


def load_grid(path):
    with open(path, "rb") as f:
        magic = f.read(4)
        assert magic == b"PFN1", path
        N, nt, L, nn = struct.unpack("<4i", f.read(16))
        levels = np.frombuffer(f.read(4 * L), dtype="<f4").astype(float)
        nodes = list(struct.unpack(f"<{nn}i", f.read(4 * nn)))
        P = L ** N
        sig = np.frombuffer(f.read(P * nn * K), dtype=np.uint8).reshape(P, nn, K).astype(float) / 255
        dev = np.frombuffer(f.read(4 * P * nn * K), dtype="<f4").reshape(P, nn, K).astype(float)
        eps = np.frombuffer(f.read(4 * P), dtype="<f4").astype(float)
    return dict(N=N, nt=nt, levels=levels, nodes=nodes, sig=sig, dev=dev, eps=eps, P=P, nn=nn)


def node_name(N, nd):
    L = {2: ["SB", "BB"], 3: ["D", "SB", "BB"], 4: ["U", "D", "SB", "BB"]}[N]
    i = 0
    while (2 << i) - 1 <= nd:
        i += 1
    p = nd - ((1 << i) - 1)
    hist = "".join("j" if p >> j & 1 else "f" for j in range(i))
    return f"{L[i]} {'after ' + hist + ' (call)' if p else 'first in (jam)'}"


def distil(g):
    """-> rank[nn][K], thr[nn][P], loss[nn] (mean EV loss per point, pool units), solver value loss baseline"""
    nn, P = g["nn"], g["P"]
    rank = np.zeros((nn, K), int)
    thr = np.zeros((nn, P), int)
    loss = np.zeros(nn)
    for k in range(nn):
        score = g["sig"][:, k, :].mean(0) * 1000 + g["dev"][:, k, :].mean(0)
        order = np.argsort(-score, kind="stable")
        rank[k] = order
        d = g["dev"][:, k, :][:, order]                 # [P][rankpos] EV(jam) - EV(fold)
        w = P1[order]
        # loss(t) = sum_{pos<t} max(0,-d) w + sum_{pos>=t} max(0,d) w
        neg = np.cumsum(np.concatenate([np.zeros((P, 1)), np.maximum(0, -d) * w], 1), 1)   # [P][t]
        pos = np.maximum(0, d) * w
        tot = pos.sum(1, keepdims=True)
        posc = tot - np.cumsum(np.concatenate([np.zeros((P, 1)), pos], 1), 1)
        tl = neg + posc
        thr[k] = tl.argmin(1)
        loss[k] = tl.min(1).mean()
    return rank, thr, loss


def pack(g, rank, thr):
    b = bytearray()
    for k in range(g["nn"]):
        b += bytes(int(x) for x in rank[k])
    for k in range(g["nn"]):
        b += bytes(int(x) for x in thr[k])
    return bytes(b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--grids", nargs="+", default=[os.path.join(REPO, "data", "pfn", f"grid_{x}.bin") for x in ("3p3", "3p4", "4p4")])
    ap.add_argument("--out", default=os.path.join(REPO, "bot", "pfn_tables.hpp"))
    a = ap.parse_args()
    games = []
    for path in a.grids:
        g = load_grid(path)
        rank, thr, loss = distil(g)
        print(f"{os.path.basename(path)}: N={g['N']} of {g['nt']}, {g['P']} points, {g['nn']} nodes, solver eps mean {g['eps'].mean():.5f} max {g['eps'].max():.5f}")
        for k, nd in enumerate(g["nodes"]):
            top = " ".join(cname(c) for c in rank[k][:4])
            print(f"   {node_name(g['N'], nd):26s} threshold loss {loss[k]:.5f}  mean T {thr[k].mean():5.1f}  ranking starts {top}")
        print(f"   total threshold loss {loss.sum():.5f} of the pool per hand (solver eps {g['eps'].mean():.5f})")
        data = pack(g, rank, thr)
        text = encode_cjk14(data)
        assert decode_cjk14(text)[:len(data)] == data
        games.append((g, rank, thr, text, path))
    with open(a.out, "w") as f:
        f.write("#pragma once\n// pfn_tables.hpp - ICM push/fold charts for 3-4 players (solvers/pfn, distilled by solvers/pfn/distil.py).\n")
        f.write("// Per game: stack levels (BB, every player; action order, the BB last), node ids ((1<<i)-1+prefix,\n")
        f.write("// prefix bit j = player j jammed) and a CJK14 string holding rank[node][169] then thr[node][L^N]:\n")
        f.write("// at a grid point jam/call with the first thr classes of the node's ranking (class index as in eq.c).\n")
        f.write("namespace pfn_t {\nstruct GameT { int N, nt, L, nn; const double* levels; const int* nodes; const char* data; };\n")
        for gi, (g, rank, thr, text, path) in enumerate(games):
            f.write(f"// {os.path.basename(path)}: {g['P']} points, {g['nn']} nodes, {len(text)} chars\n")
            f.write(f"const double LV{gi}[{len(g['levels'])}] = {{{', '.join(f'{x:g}' for x in g['levels'])}}};\n")
            f.write(f"const int ND{gi}[{g['nn']}] = {{{', '.join(map(str, g['nodes']))}}};\n")
            f.write(f'const char D{gi}[] = R"~({text})~";\n')
        f.write(f"const int NG = {len(games)};\n")
        f.write("const GameT GAMES[] = {" + ", ".join(f"{{{g['N']}, {g['nt']}, {len(g['levels'])}, {g['nn']}, LV{i}, ND{i}, D{i}}}" for i, (g, _, _, _, _) in enumerate(games)) + "};\n")
        f.write("}  // namespace pfn_t\n")
    size = os.path.getsize(a.out)
    chars = len(open(a.out, encoding="utf-8").read())
    print(f"wrote {os.path.relpath(a.out, REPO)}: {size} bytes, {chars} chars")


if __name__ == "__main__":
    main()

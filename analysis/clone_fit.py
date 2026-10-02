"""Clones of the live bots for the arena (arena/clone.hpp): training data, fitting, held-out validation, export.

    python3 analysis/clone_fit.py data      # replays -> data/cache/clone_games.log -> build/arena/clone_data
                                            #   -> data/cache/clone_decisions.tsv (one row per decision, every seat
                                            #   except ours)
    python3 analysis/clone_fit.py fit [--min-games 8] [--out data/clones/clones.txt]
    python3 analysis/clone_fit.py schedule tools/m21_games.txt > data/clones/sched_m21.txt
                                            # a live run's tables (size and opponents, one game per line) for
                                            #   build/arena/arena --schedule: a version replayed against that run's field

Model, per situation (7, arena/clone.hpp): a multinomial logit over the legal action classes (fold/check, call,
bet, all-in) on 18 features (hand strength and shape, effective depth, opponents, position, facing size, heads-up phase, ...).  The field
model is fitted on every player's decisions; a player's model is the field model plus a deviation, fitted on
that player's decisions with an L2 penalty lam * |W - W_field|^2, so a player with few decisions stays near the
field.  lam is chosen per situation by held-out log-loss (games split 80/20 by game id).  Bet sizes: the player's recorded
sizes per situation (the chips beyond the call over the pot after calling), each with the strength and depth it
was made at; the clone draws among the nearest ones (arena/clone.hpp).  The field's when the player has fewer than 8.
Weights: how often we met each player in our live games (data/battles/flawedaxioms*.json.gz and our replays).
"""
import argparse, collections, glob, os, subprocess, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "sim"))
from replay_io import REPLAY_DIR, load_replay, recorded_actions   # noqa: E402

CACHE = os.path.join(REPO, "data", "cache")
GAMES_LOG = os.path.join(CACHE, "clone_games.log")
TSV = os.path.join(CACHE, "clone_decisions.tsv")
BIN = os.path.join(REPO, "build", "arena", "clone_data")
ME = "flawedaxioms"
NF, NA, NSIT = 18, 4, 7
SIT = ["open", "bbopt", "vsraise", "vsjam", "ck", "vsbet", "vsjam_post"]


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def cmd_data(a):
    os.makedirs(CACHE, exist_ok=True)
    paths = sorted(glob.glob(os.path.join(REPLAY_DIR, "*.json.gz")))
    with open(GAMES_LOG, "w", encoding="utf-8") as f:
        for p in paths:
            g = load_replay(p)
            seed = int(g["refereeInput"].strip().split("=")[1]); n = len(g["agents"])
            f.write(f"G {g['gameId']} {n} {seed}\n")
            for ag in g["agents"]:
                f.write(f"N {ag['index']} {ag['codingamer']['pseudo'] if ag.get('codingamer') else 'agent' + str(ag['index'])}\n")
            for pid, text in recorded_actions(g):
                f.write(f"A {pid} \\0\n" if text is None else f"A {pid} {esc(text)}\n")
            f.write("R " + " ".join(str(int(s)) for s in g["scores"]) + "\n")
    print(f"{len(paths)} games -> {os.path.relpath(GAMES_LOG, REPO)}")
    src = os.path.join(REPO, "arena", "clone_data.cpp")
    os.makedirs(os.path.dirname(BIN), exist_ok=True)
    subprocess.check_call(["g++", "-std=gnu++20", "-O2", "-march=native", "-o", BIN, src])
    subprocess.check_call([BIN, GAMES_LOG, TSV])


def load_tsv():
    rows = [l.rstrip("\n").split("\t") for l in open(TSV)]
    hdr = rows[0]; rows = rows[1:]
    col = {k: i for i, k in enumerate(hdr)}
    rows = [r for r in rows if r[col["pseudo"]] != ME]
    d = dict(
        game=np.array([int(r[col["game"]]) for r in rows]),
        pseudo=np.array([r[col["pseudo"]] for r in rows]),
        sit=np.array([int(r[col["sit"]]) for r in rows]),
        mask=np.array([int(r[col["mask"]]) for r in rows]),
        X=np.array([[float(r[col[f"x{k}"]]) for k in range(NF)] for r in rows]),
        cls=np.array([int(r[col["cls"]]) for r in rows]),
        size=np.array([float(r[col["size"]]) for r in rows]),
        n_total=np.array([int(r[col["n_total"]]) for r in rows]),
        s=np.array([float(r[col["s"]]) for r in rows]),
        eff=np.array([float(r[col["eff_bb"]]) for r in rows]),
        own=np.array([float(r[col["own_bb"]]) for r in rows]),
        alive=np.array([int(r[col["alive"]]) for r in rows]),
        pot=np.array([int(r[col["pot"]]) for r in rows]),
        call=np.array([int(r[col["call"]]) for r in rows]),
        bb=np.array([int(r[col["bb"]]) for r in rows]),
        stack=np.array([int(r[col["stack"]]) for r in rows]),
    )
    return d


# ---------------------------------------------------------------- multinomial logit with masks, Newton's method
def logits(W, X, M):
    Z = X @ W.T                                   # [n, NA]
    Z = np.where(M, Z, -np.inf)
    Z -= Z.max(1, keepdims=True)
    P = np.exp(Z); P /= P.sum(1, keepdims=True)
    return P


def nll(W, X, M, y):
    P = logits(W, X, M)
    return -np.log(np.maximum(P[np.arange(len(y)), y], 1e-12)).sum()


def fit(X, M, y, W0, lam, iters=30):
    """argmin_W  NLL(W) + lam * |W - W0|^2 over the classes that appear in M (others stay at W0)"""
    W = W0.copy()
    act = M.any(0)                                # classes legal somewhere in this data
    idx = [(a, f) for a in range(NA) if act[a] for f in range(NF)]
    if not idx or len(y) == 0:
        return W
    Y = np.zeros((len(y), NA)); Y[np.arange(len(y)), y] = 1
    for _ in range(iters):
        P = logits(W, X, M)
        G = (P - Y).T @ X + 2 * lam * (W - W0)    # [NA, NF]
        g = np.array([G[a, f] for a, f in idx])
        # Hessian of the softmax NLL: sum_i x x^T (diag(p) - p p^T)
        A = [a for a in range(NA) if act[a]]
        H = np.zeros((len(idx), len(idx)))
        for i1, a1 in enumerate(A):
            for i2, a2 in enumerate(A):
                w = P[:, a1] * ((a1 == a2) - P[:, a2])
                H[i1 * NF:(i1 + 1) * NF, i2 * NF:(i2 + 1) * NF] = (X * w[:, None]).T @ X
        H += 2 * lam * np.eye(len(idx)) + 1e-6 * np.eye(len(idx))
        step = np.linalg.solve(H, g)
        # damped Newton: halve until the objective decreases
        obj = nll(W, X, M, y) + lam * ((W - W0) ** 2).sum()
        t = 1.0
        while t > 1e-4:
            Wn = W.copy()
            for k, (a, f) in enumerate(idx): Wn[a, f] -= t * step[k]
            if nll(Wn, X, M, y) + lam * ((Wn - W0) ** 2).sum() <= obj: break
            t /= 2
        W = Wn
        if np.abs(t * step).max() < 1e-6: break
    return W


def masks(m):
    return np.stack([(m >> a) & 1 for a in range(NA)], 1).astype(bool)


def cmd_fit(a):
    d = load_tsv()
    n = len(d["cls"])
    test = (d["game"] * 2654435761 % 1000) < 200          # held-out games, 20%
    M = masks(d["mask"])
    print(f"{n} decisions by {len(set(d['pseudo']))} players (ours excluded); held-out {test.sum()} decisions")
    # field model per situation: fit on train, light ridge toward 0
    Wf = np.zeros((NSIT, NA, NF)); Wf_all = np.zeros((NSIT, NA, NF))
    for s in range(NSIT):
        tr = (d["sit"] == s) & ~test
        Wf[s] = fit(d["X"][tr], M[tr], d["cls"][tr], np.zeros((NA, NF)), 1.0)
        al = d["sit"] == s
        Wf_all[s] = fit(d["X"][al], M[al], d["cls"][al], np.zeros((NA, NF)), 1.0)
    # per-player deviations; choose lam on held-out log-loss of the frequent players
    games_by = collections.Counter()
    for g, p in set(zip(d["game"], d["pseudo"])): games_by[p] += 1
    players = [p for p, k in games_by.items() if k >= a.min_games]
    print(f"players with >= {a.min_games} games: {len(players)}")
    lams = [0.01, 0.03, 0.1, 0.3, 1, 3, 10, 30]
    pmask = {p: d["pseudo"] == p for p in players}
    # held-out log-loss per situation and lam, summed over the players; lam chosen per situation
    res = np.zeros((NSIT, len(lams))); base = np.zeros(NSIT); unif = np.zeros(NSIT); nte = np.zeros(NSIT)
    per = collections.defaultdict(lambda: np.zeros((NSIT, len(lams) + 2)))     # player -> [sit][field, n, lams...]
    for p in players:
        for s in range(NSIT):
            sel = pmask[p] & (d["sit"] == s)
            tr, te = sel & ~test, sel & test
            if te.sum() == 0: continue
            bf = nll(Wf[s], d["X"][te], M[te], d["cls"][te])
            base[s] += bf; unif[s] += np.log(M[te].sum(1)).sum(); nte[s] += te.sum()
            per[p][s, 0] += bf; per[p][s, 1] += te.sum()
            for j, lam in enumerate(lams):
                W = fit(d["X"][tr], M[tr], d["cls"][tr], Wf[s], lam) if tr.sum() else Wf[s]
                v = nll(W, d["X"][te], M[te], d["cls"][te])
                res[s, j] += v; per[p][s, 2 + j] += v
    best = res.argmin(1)
    lam_s = [lams[j] for j in best]
    print("held-out log-loss per decision (players with enough games): situation  n  uniform  field  player(best lam)")
    for s in range(NSIT):
        print(f"  {SIT[s]:11s} {int(nte[s]):6d}  {unif[s] / nte[s]:.4f}  {base[s] / nte[s]:.4f}  {res[s, best[s]] / nte[s]:.4f} (lam {lam_s[s]})")
    tot_n = nte.sum()
    print(f"  {'all':11s} {int(tot_n):6d}  {unif.sum() / tot_n:.4f}  {base.sum() / tot_n:.4f}  {sum(res[s, best[s]] for s in range(NSIT)) / tot_n:.4f}")
    print("per player (held-out decisions, log-loss field -> player):")
    for p in sorted(players, key=lambda p: -games_by[p]):
        v = per[p]; k = v[:, 1].sum()
        if k: print(f"  {p:22s} games {games_by[p]:4d}  test decisions {int(k):5d}  {v[:, 0].sum() / k:.4f} -> {sum(v[s, 2 + best[s]] for s in range(NSIT)) / k:.4f}")
    # final fit on all data, export
    weights = live_weights()
    added = d["call"] + d["size"] * (d["pot"] + d["call"])          # chips the bet or raise added
    units = [d["pot"] + d["call"], d["bb"].astype(float), d["own"] * d["bb"]]
    def size_unit(d, sel, s):
        """the unit the player sizes in, for this street group: the one in which their sizes vary least"""
        grp = (0, 1, 2) if s <= 3 else (4, 5)
        k = sel & np.isin(d["sit"], grp) & (d["cls"] == 2)
        if k.sum() < 20 or sel.all(): return 0
        return int(np.argmin([np.log(added[k] / u[k]).std() for u in units]))
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w") as f:
        f.write(f"# arena clones fitted by analysis/clone_fit.py: {n} decisions, lam per situation {lam_s}; P name live-encounter-weight\n")
        def emit(name, w, W, sel):
            f.write(f"P {name} {w:.4f}\n")
            for s in range(NSIT):
                for c in range(NA):
                    f.write(f"W {s} {c} " + " ".join(f"{v:.5f}" for v in W[s, c]) + "\n")
                # recorded sizes with the strength and depth they were made at (the clone draws among the
                # nearest ones: big hands and short stacks bet differently), in the player's unit; at most 400
                k = np.flatnonzero(sel & (d["sit"] == s) & (d["cls"] == 2))
                if len(k) >= 8:
                    u = size_unit(d, sel, s)
                    v = added[k] / units[u][k]
                    if len(k) > 400: k, v = k[np.linspace(0, len(k) - 1, 400).astype(int)], v[np.linspace(0, len(k) - 1, 400).astype(int)]
                    f.write(f"S {s} {u} " + " ".join(f"{d['X'][i, 1]:.3f} {d['X'][i, 3]:.3f} {x:.4f}" for i, x in zip(k, v)) + "\n")
        emit("_field", 0.0, Wf_all, np.ones(n, bool))
        for p in sorted(players):
            W = np.stack([fit(d["X"][pmask[p] & (d["sit"] == s)], M[pmask[p] & (d["sit"] == s)], d["cls"][pmask[p] & (d["sit"] == s)], Wf_all[s], lam_s[s])
                          for s in range(NSIT)])
            emit(p, weights.get(p, 0.0), W, pmask[p])
    print(f"wrote {os.path.relpath(a.out, REPO)}: field + {len(players)} players")


def live_weights():
    """share of our live opponents' seats taken by each player, over our recorded games"""
    c = collections.Counter()
    for p in glob.glob(os.path.join(REPLAY_DIR, "*.json.gz")):
        g = load_replay(p)
        names = [ag["codingamer"]["pseudo"] if ag.get("codingamer") else "?" for ag in g["agents"]]
        if ME in names:
            for nm in names:
                if nm != ME: c[nm] += 1
    tot = sum(c.values())
    return {k: v / tot for k, v in c.items()}


def cmd_schedule(a):
    for gid in open(a.ids).read().split():
        g = load_replay(os.path.join(REPLAY_DIR, f"{gid}.json.gz"))
        names = [ag["codingamer"]["pseudo"] if ag.get("codingamer") else "?" for ag in g["agents"]]
        print(len(names), " ".join(n for n in names if n != ME))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("data")
    f = sub.add_parser("fit"); f.add_argument("--min-games", type=int, default=8); f.add_argument("--out", default=os.path.join(REPO, "data", "clones", "clones.txt"))
    sc = sub.add_parser("schedule"); sc.add_argument("ids")
    a = ap.parse_args()
    {"data": cmd_data, "fit": cmd_fit, "schedule": cmd_schedule}[a.cmd](a)


if __name__ == "__main__":
    main()

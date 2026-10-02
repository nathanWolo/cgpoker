"""Per-regime chip results of the seat under test, from the arena's hand ledger (ARENA_HANDLOG).

    python3 analysis/arena_hands.py data/cache/clone_arena/hands_diag.tsv [--by alive,depth] [--ref om1]

For each variant (the bot or clone in our seat): hands and net big blinds per hand, with standard errors, by
regime: players alive (2/3/4), effective depth (<=10, 10-20, 20-40, 40-80, >80 BB), and with --detail the
preflop context before our first action (unopened / limped / raised), what we did (fold, check, call, bet,
all-in) and how the hand went (folded preflop, won preflop, saw a flop).  Chips are not payout, but in the
deep regimes they are close, and the difference to the reference variant (--ref) per regime shows where a
policy gains or gives away chips.
"""
import argparse, collections, math


def depth(e):
    return "<=10" if e <= 10 else "10-20" if e <= 20 else "20-40" if e <= 40 else "40-80" if e <= 80 else ">80"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ledger")
    ap.add_argument("--ref", default="om1")
    ap.add_argument("--detail", action="store_true", help="also split by context / first action / outcome")
    ap.add_argument("--alive", type=int, default=0, help="only hands with this many players alive")
    ap.add_argument("--min-hands", type=int, default=200)
    a = ap.parse_args()
    rows = [l.rstrip("\n").split("\t") for l in open(a.ledger)]
    hdr = rows[0]; c = {k: i for i, k in enumerate(hdr)}; rows = rows[1:]
    acc = collections.defaultdict(lambda: [0, 0.0, 0.0])      # (variant, key) -> n, sum, sumsq
    variants = []
    for r in rows:
        v = r[c["variant"]]
        if v not in variants: variants.append(v)
        al = int(r[c["alive"]])
        if a.alive and al != a.alive: continue
        x = float(r[c["net_bb"]])
        keys = [("all",), (f"{al}p",), (f"{al}p", depth(float(r[c["eff_bb"]])))]
        if a.detail:
            keys.append((f"{al}p", depth(float(r[c["eff_bb"]])), r[c["context"]], r[c["first"]]))
            keys.append((f"{al}p", r[c["context"]], r[c["first"]], r[c["outcome"]]))
        for k in keys:
            s = acc[(v, k)]; s[0] += 1; s[1] += x; s[2] += x * x
    keys = sorted({k for (_, k) in acc}, key=lambda k: (len(k), k))
    ref = a.ref if a.ref in variants else variants[0]
    others = [v for v in variants if v != ref]
    print(f"net BB per hand of the seat under test; columns: {ref} (n, mean +- se), then each variant's difference to it")
    print(f"{'regime':42s} {ref:>22s}  " + "  ".join(f"{v[-12:]:>14s}" for v in others))
    for k in keys:
        n0, s0, q0 = acc[(ref, k)]
        if n0 < a.min_hands: continue
        m0 = s0 / n0; se0 = math.sqrt(max(q0 / n0 - m0 * m0, 0) / n0)
        cells = []
        for v in others:
            n1, s1, q1 = acc[(v, k)]
            if n1 < a.min_hands: cells.append(f"{'-':>14s}"); continue
            m1 = s1 / n1; se1 = math.sqrt(max(q1 / n1 - m1 * m1, 0) / n1)
            cells.append(f"{m1 - m0:+7.2f}+-{math.hypot(se0, se1):4.2f}")
        print(f"{' '.join(k):42s} {n0:6d} {m0:+6.2f}+-{se0:4.2f}  " + "  ".join(cells))


if __name__ == "__main__":
    main()

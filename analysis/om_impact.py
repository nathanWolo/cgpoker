"""Where the opponent model changed live decisions, from a retrace of the live games (analysis/retrace.py).

    python3 analysis/om_impact.py data/cache/retrace_om1.jsonl

Reports the reproduction rate, the decision-tag histogram, and for postflop checked-to decisions how often the
model bet where the fixed M2.3 rule (bet if equity > 0.55 + 0.04 per extra opponent) would have checked, and the
reverse, with the net chips of those hands; bluffs and the measured heads-up jam (pf-sb-om) likewise.  Hand
results are what happened, not a controlled comparison: a hand the model bet would have played out differently
under the old rule.  Equity is the model's (its belief width), so the fixed-rule counterfactual is approximate.
"""
import json, sys, collections
recs = [json.loads(l) for l in open(sys.argv[1])]
print(f"{len(recs)} decisions; bot reproduces live action on {sum(r['rec']==r['bot'] for r in recs)}")
tags = collections.Counter(r["tag"] for r in recs)
print("tags:", ", ".join(f"{k} {v}" for k, v in tags.most_common()))
# hands: net per (game, hand), with the set of tags in it
hands = collections.defaultdict(lambda: dict(net=0, tags=set(), changed=set()))
for r in recs:
    h = hands[(r["game"], r["hand"])]; h["net"] = r["net_bb"]
    h["tags"].add(r["tag"])
    # postflop checked-to: would the fixed rule (M2.3) have done the same?
    if r["street"] >= 3 and r["call"] == 0 and r["tag"] in ("bet", "bluff", "check") and r["note"] is not None:
        e = float(r["eq"]); n_opp = r["live"] - 1
        fixed_bet = e > 0.55 + 0.04 * (n_opp - 1)
        om_bet = r["tag"] in ("bet", "bluff")
        if fixed_bet != om_bet:
            h["changed"].add("om-bet-not-fixed" if om_bet else "om-check-not-fixed")
pf = collections.defaultdict(lambda: [0, 0.0])
for k, h in hands.items():
    for c in h["changed"]: pf[c][0] += 1; pf[c][1] += h["net"]
    for t in ("bluff", "pf-sb-om"):
        if t in h["tags"]: pf[t][0] += 1; pf[t][1] += h["net"]
for k, (n, s) in pf.items():
    print(f"hands with {k:20s} n={n:4d} net {s:+8.1f} BB  ({s/max(n,1):+.2f} BB/hand)")
# checked-to postflop decisions: OM vs fixed rule
ct = [r for r in recs if r["street"] >= 3 and r["call"] == 0 and r["tag"] in ("bet", "bluff", "check")]
nb = sum(r["tag"] != "check" for r in ct)
fb = sum(float(r["eq"]) > 0.55 + 0.04 * (r["live"] - 2) for r in ct)
print(f"postflop checked-to decisions {len(ct)}: model bet {nb}, fixed rule would bet {fb}")

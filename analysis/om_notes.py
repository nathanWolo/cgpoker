"""What the opponent model concluded about each live opponent by the end of a game (from a retrace with notes).

    python3 analysis/om_notes.py data/cache/retrace_om_notes.jsonl

Prints, per game, the last model note of the heads-up phase (open / limp / jam widths, fold-to-raise, bet frequency,
fold-to-bet, with the observation counts in brackets) next to the opponent's name, so it can be compared with
analysis/tendencies.py's long-run numbers.
"""
import json, sys, os, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sim"))
from replay_io import load_replay, REPLAY_DIR   # noqa: E402

recs = [json.loads(l) for l in open(sys.argv[1])]
last = {}
for r in recs:
    if r.get("note") and r["alive"] == 2 and "om" in r["note"]:
        r["note"] = r["note"][r["note"].index("om"):]
        last[r["game"]] = r
for gid, r in last.items():
    g = load_replay(os.path.join(REPLAY_DIR, f"{gid}.json.gz"))
    names = {a["index"]: (a["codingamer"]["pseudo"] if a.get("codingamer") else "?") for a in g["agents"]}
    opp = int(r["note"].split()[0][2:])
    print(f"g{gid} h{r['hand']:2d} {names.get(opp, '?'):18s} {r['note'][4:]}")

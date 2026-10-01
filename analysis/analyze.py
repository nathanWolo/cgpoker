"""Replay CodinGame replays through the Java referee driver and cache the per-decision logs.

Usage: python3 analysis/analyze.py [game_set]   (game_set: analysis371 (default) | validation120 | all | path to id list)
Writes data/cache/replayed.pkl = [(gameId, names, ranks, scores, driver_output), ...].
Also imported by the stats scripts (and archive/profiling/) for the preflop tables PE / PCT and hclass().
"""
import json, glob, collections, sys, re, pickle, os
from concurrent.futures import ThreadPoolExecutor
from common import HERE, CACHE, REPLAYED_PKL, REPLAYER_DIR, replay_paths
# preflop table
PE={}
for l in open(os.path.join(HERE,'preeq.tsv')):
    k,*e=l.split('\t'); PE[k]=[float(x) for x in e]
def combos(k): return 6 if len(k)==2 else (4 if k[2]=='s' else 12)
# percentile: fraction of combos with HU equity >= this hand (i.e. top X%)
order=sorted(PE, key=lambda k:-PE[k][0])
cum=0; PCT={}
for k in order:
    cum+=combos(k); PCT[k]=cum/1326
RK="23456789TJQKA"
def hclass(h):
    a,b=h.split('_'); r1,r2=RK.index(a[0]),RK.index(b[0])
    if r1<r2: a,b=b,a; r1,r2=r2,r1
    if r1==r2: return a[0]+b[0]
    return a[0]+b[0]+('s' if a[1]==b[1] else 'o')
def replay(path):
    """(replay JSON, Java driver stdout) via replayer/replay_game.py (builds the Java driver on first use)."""
    if REPLAYER_DIR not in sys.path: sys.path.insert(0, REPLAYER_DIR)
    import replay_game as R
    return R.replay(path)
errors=[]
def run(path):
    try:
        d,o=replay(path)
    except Exception as e:
        errors.append((path,repr(e)))
        return None
    return d['gameId'], [a['codingamer']['pseudo'] if a.get('codingamer') else f"BOSS{a.get('agentId')}" for a in sorted(d['agents'], key=lambda a:a['index'])], d['ranks'], d['scores'], o
if __name__=='__main__':
    files=replay_paths(sys.argv[1] if len(sys.argv)>1 else 'analysis371')
    with ThreadPoolExecutor(8) as ex:
        res=[r for r in ex.map(run, files) if r]
    os.makedirs(CACHE, exist_ok=True)
    pickle.dump(res, open(REPLAYED_PKL,'wb'))
    bad=[r[0] for r in res if 'GAMEOVER' not in r[4] and 'CANCEL' not in r[4]]
    print('replayed',len(res),'bad',len(bad), bad[:5], '->', REPLAYED_PKL)
    if errors: print('failed',len(errors),errors[:3],file=sys.stderr)
    sys.exit(1 if errors or bad or not res else 0)

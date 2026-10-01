"""Duplicate-seed variance check, 4 players: every seed is played with the 4 cyclic seat rotations (deterministic toy bots).
Usage: python3 eval/dup4.py N_SEEDS   (4*N_SEEDS games; the README numbers use 400)
"""
import sys, time, statistics as st, random
from dup import jammer, tag, run
bots=[jammer(24), tag(30,18), tag(34,10), jammer(30)]
random.seed(2); seeds=[random.getrandbits(62) for _ in range(int(sys.argv[1]))]
ind=[]; blk=[]; t=time.time()
for s in seeds:
    b=[]
    for r in range(4):
        seats=[bots[(i+r)%4] for i in range(4)]   # bot k sits at seat (k-r)%4
        sc=run(4,seats,s)
        pos={k:(k-r)%4 for k in range(4)}
        # pairwise finish-ahead of bot0 vs the other three
        v=sum(1.0 if sc[pos[0]]>sc[pos[k]] else (0.5 if sc[pos[0]]==sc[pos[k]] else 0.0) for k in (1,2,3))/3
        ind.append(v); b.append(v)
    blk.append(st.mean(b))
n=len(blk)
print('4p games',4*n,'secs',round(time.time()-t,1),'bot0 pairwise',round(st.mean(ind),3))
print('SE independent',round(st.pstdev(ind)/(4*n)**.5,4),' SE rotation-block',round(st.pstdev(blk)/n**.5,4), 'variance ratio', round((st.pstdev(blk)**2/n)/(st.pstdev(ind)**2/(4*n)),2))

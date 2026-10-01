"""Duplicate-seed variance check, heads-up: every seed is played twice with seats swapped (deterministic toy bots).
Usage: python3 eval/dup.py N_SEEDS   (2*N_SEEDS games; the README numbers use 3000)
"""
import sys, os, time, statistics as st, random
sys.path.insert(0,os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),'sim'))
from poker_sim import PokerSim
R='23456789TJQKA'
def score(cards):
    a,b=cards.split('_'); ra,rb=R.index(a[0]),R.index(b[0]); hi,lo=max(ra,rb),min(ra,rb)
    s=hi*2+lo+(1.5 if a[1]==b[1] else 0)-(max(0,hi-lo-2))*0.7
    if ra==rb: s=26+ra*2
    return s  # range roughly 0..50
def jammer(th):
    def f(o):
        if score(o.cards)>=th: return 'ALL-IN'
        return 'CALL' if 'CHECK' in o.possible else 'FOLD'
    return f
def tag(th_raise, th_call):
    def f(o):
        s=score(o.cards); bd=o.board.count('X')
        if s>=th_raise:
            b=[x for x in o.possible if x.startswith('BET')]
            return b[0].replace('_',' ') if b else 'CALL'
        if s>=th_call: return 'CALL'
        return 'CHECK' if 'CHECK' in o.possible else 'FOLD'
    return f
def run(n, seats, seed):
    g=PokerSim(n, seed); r=g.run(seats); return r['scores']
if __name__=='__main__':
    A=jammer(24); B=tag(30,18)
    random.seed(1); seeds=[random.getrandbits(62) for _ in range(int(sys.argv[1]))]
    ind=[];pair=[]
    t=time.time()
    for s in seeds:
        x=run(2,[A,B],s); y=run(2,[B,A],s)
        a1=1.0 if x[0]>x[1] else 0.0; a2=1.0 if y[1]>y[0] else 0.0
        ind+= [a1,a2]; pair.append((a1+a2)/2)
    n=len(pair)
    print('games',2*n,'secs',round(time.time()-t,1),'A win',round(st.mean(ind),3))
    print('SE independent (per 2n games)', round(st.pstdev(ind)/ (2*n)**.5,4), ' SE duplicate', round(st.pstdev(pair)/n**.5,4))
    print('pair outcomes', {k:pair.count(k) for k in (0,0.5,1)})
    print('variance ratio (SE duplicate / SE independent)^2', round((st.pstdev(pair)**2/n)/(st.pstdev(ind)**2/(2*n)),2))

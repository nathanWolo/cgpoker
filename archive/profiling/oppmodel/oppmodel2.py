# Opponent-model experiment on 371 reconstructed replays: how fast can a within-game
# model beat a population model, and how fast can a bot be fingerprinted from a library?
import pickle, collections, math, sys, json
import os; sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','..','analysis'))
from analyze import PCT, hclass
from common import REPLAYED_PKL
res=pickle.load(open(REPLAYED_PKL,'rb'))   # data/cache/replayed.pkl from analysis/analyze.py
RK="23456789TJQKA"
def pf_bucket(cards):
    p=PCT[hclass(cards)]
    return 0 if p<=.10 else 1 if p<=.25 else 2 if p<=.50 else 3 if p<=.75 else 4
def post_bucket(cards,board):
    h=cards.split('_'); b=board.split('_')
    hr=[RK.index(c[0]) for c in h]; br=[RK.index(c[0]) for c in b]
    allc=h+b; suits=collections.Counter(c[1] for c in allc)
    hs=[c[1] for c in h]
    if any(v>=5 and hs.count(s)>=1 for s,v in suits.items()): return 3
    rs=set(RK.index(c[0]) for c in allc); rs2=rs|({-1} if 12 in rs else set())
    for lo in range(-1,9):
        if all(r in rs2 for r in range(lo,lo+5)) and any(r in range(lo,lo+5) or (r==12 and lo==-1) for r in hr): return 3
    cnt=collections.Counter(hr+br); bc=collections.Counter(br)
    mine=[r for r in set(hr) if cnt[r]>=2 and cnt[r]>bc.get(r,0)]
    if any(cnt[r]>=3 for r in mine) or len(mine)>=2: return 3
    if mine:
        r=max(mine)
        if hr[0]==hr[1] and hr[0]>max(br): return 2
        if r>=max(br): return 2
        return 1
    return 0
def act_class(a,put,pot,call,stack,bb,street,rb_me):
    if a=='FOLD': return 0
    if a in('CHECK','CALL'): return 1
    if a=='ALL-IN': return 4 if put>call else 1
    # BET
    if street==0:
        to=(rb_me+put)/bb
        return 2 if to<=3.01 else 3
    frac=(put-call)/max(1,pot+call)
    return 2 if frac<=0.6 else 3
def facing(call,stack,pot,street,bb):
    if call==0: return 0
    if call>=stack: return 3
    if street==0 and call<=bb: return 0  # limp/complete situation counts as unopened
    return 1 if call<=0.6*(pot-call) else 2
games=[]
for gid,names,ranks,scores,out in res:
    dec=[]
    for l in out.splitlines():
        f=l.split('\t')
        if f[0]!='ACT': continue
        (_,turn,hn,pid,board,cards,bb,pot,call,stack,sts,poss,act,put,alive,notf,dealer,sb,bbid,raw)=f
        pid=int(pid); bb=int(bb); pot=int(pot); call=int(call); stack=int(stack); put=int(put)
        a=act.split('_')[0]
        street=0 if board=='-' else len(board.split('_'))-2
        rb_me=int(sts.split(',')[pid].split('/')[2].rstrip('F'))
        hb=pf_bucket(cards) if street==0 else post_bucket(cards,board)
        st=0 if street==0 else 1
        depth=0 if stack/bb<15 else 1
        ctx=(st,facing(call,stack,pot,street,bb),hb,depth, min(int(notf),3) if st==0 else 0)
        dec.append((names[pid],ctx,act_class(a,put,pot,call,stack,bb,street,rb_me)))
    games.append((gid,names,dec))
NA=5
tot=collections.defaultdict(lambda:[0]*NA); per=collections.defaultdict(lambda:collections.defaultdict(lambda:[0]*NA))
ngames=collections.Counter()
for gid,names,dec in games:
    for n in set(names): ngames[n]+=1
    for n,c,a in dec:
        tot[c][a]+=1; per[n][c][a]+=1
LIB=[n for n in ngames if ngames[n]>=15]
print('library bots (>=15 games):',len(LIB))
def pop_p(c,sub):
    v=[tot[c][i]-sub.get(c,[0]*NA)[i] for i in range(NA)]
    s=sum(v); return [(x+0.5)/(s+0.5*NA) for x in v]
def bot_p(n,c,sub,n0=4.0):
    base=pop_p(c,sub)
    v=[per[n][c][i]-sub.get(c,[0]*NA)[i] for i in range(NA)]
    s=sum(v); return [(v[i]+n0*base[i])/(s+n0) for i in range(NA)]
bins=[(1,10),(11,25),(26,50),(51,999)]
LL={k:collections.defaultdict(lambda:[0.0,0]) for k in ['pop','online','types','types+online','oracle']}
LLin={k:[0.0,0] for k in ['pop','online','types','types+online']}
LLout={k:[0.0,0] for k in ['pop','online','types','types+online']}
idacc=collections.defaultdict(lambda:[0,0])
for gid,names,dec in games:
    for n in set(names):
        mine=[(c,a) for (nn,c,a) in dec if nn==n]
        if len(mine)<20: continue
        sub=collections.defaultdict(lambda:[0]*NA)
        for (nn,c,a) in dec:
            sub[c][a]+=1  # remove whole game from population (all players)
        subn={}
        for c,a in mine: subn.setdefault(c,[0]*NA); subn[c][a]+=1
        lib=[k for k in LIB if not(k==n and ngames[n]<=15)]
        logw={k:0.0 for k in lib}; logw['_pop']=0.0
        cnt=collections.defaultdict(lambda:[0]*NA)
        for i,(c,a) in enumerate(mine,1):
            pp=pop_p(c,sub)
            # online Dirichlet around population
            v=cnt[c]; s=sum(v); n0=3.0
            po=[(v[j]+n0*pp[j])/(s+n0) for j in range(NA)]
            # type mixture
            m=max(logw.values()); w={k:math.exp(x-m) for k,x in logw.items()}; Z=sum(w.values())
            preds={}
            for k in w: preds[k]=pp if k=='_pop' else bot_p(k,c,subn if k==n else {})
            pt=[sum(w[k]*preds[k][j] for k in w)/Z for j in range(NA)]
            pr=bot_p(n,c,subn) if n in LIB else pp
            n1=2.0; ptc=[(v[j]+n1*pt[j])/(s+n1) for j in range(NA)]
            b=next(bb for bb in bins if bb[0]<=i<=bb[1])
            for key,p in (('pop',pp),('online',po),('types',pt),('types+online',ptc),('oracle',pr)):
                LL[key][b][0]+=-math.log(max(1e-9,p[a])); LL[key][b][1]+=1
                if key!='oracle':
                    D=LLin if n in LIB else LLout; D[key][0]+=-math.log(max(1e-9,p[a])); D[key][1]+=1
            if n in LIB:
                for kk in (5,10,20,40):
                    if i==kk:
                        best=max((x for x in logw if x!='_pop'),key=lambda x:logw[x])
                        idacc[kk][0]+=best==n; idacc[kk][1]+=1
            for k in logw:
                pk=preds[k]; logw[k]+=math.log(0.97*pk[a]+0.03/NA)
            cnt[c][a]+=1
print('mean log-loss (nats/decision) by decision index within game (target player):')
for b in bins:
    print(b, {k:round(LL[k][b][0]/max(1,LL[k][b][1]),4) for k in LL}, 'n=',LL['pop'][b][1])
allb={k:sum(LL[k][b][0] for b in bins)/sum(LL[k][b][1] for b in bins) for k in LL}
print('overall',{k:round(v,4) for k,v in allb.items()})
print('fingerprint top-1 accuracy among',len(LIB),'library bots after k own decisions:',{k:(round(v[0]/v[1],3),v[1]) for k,v in sorted(idacc.items())})

print('in-library targets',{k:(round(v[0]/max(1,v[1]),4),v[1]) for k,v in LLin.items()})
print('out-of-library targets',{k:(round(v[0]/max(1,v[1]),4),v[1]) for k,v in LLout.items()})
nz=sum(1 for c in tot if sum(tot[c])>0); print('non-empty contexts',nz)
print('lib', sorted(LIB))
per_opp=[sum(1 for (nn,c,a) in dec if nn==n) for gid,names,dec in games for n in set(names)]
import statistics as S; print('decisions per player-game median',S.median(per_opp),'p25',sorted(per_opp)[len(per_opp)//4])

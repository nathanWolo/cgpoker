import pickle, collections, json, statistics as S
from analyze import PCT, hclass
from common import REPLAYED_PKL, load_leaderboard
res=pickle.load(open(REPLAYED_PKL,'rb'))
lb=load_leaderboard(); rank={u['pseudo']:u['rank'] for u in lb['users']}
opensize=collections.defaultdict(collections.Counter)
betfrac=collections.defaultdict(collections.Counter)
fold_small_open=collections.defaultdict(lambda:[0,0])
fold_small_bet=collections.defaultdict(lambda:[0,0])
first_pf_by_depth=collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
for gid,names,ranks,scores,out in res:
    hand_pf=None
    for l in out.splitlines():
        f=l.split('\t')
        if f[0]=='HAND': seen=set(); raises=0
        if f[0]!='ACT': continue
        (_,turn,hn,pid,board,cards,bb,pot,call,stack,sts,poss,act,put,alive,notf,dealer,sb,bbid,raw)=f
        pid=int(pid); bb=int(bb); pot=int(pot); call=int(call); stack=int(stack); put=int(put)
        p=names[pid]; a=act.split('_')[0]
        rb=[int(x.split('/')[2].rstrip('F')) for x in sts.split(',')]
        stk=[int(x.split('/')[0]) for x in sts.split(',')]
        if board=='-':
            maxrb=max(rb)
            if pid not in seen:
                seen.add(pid)
                d=stack/bb; bucket='<10' if d<10 else '10-20' if d<20 else '20-50' if d<50 else '50+'
                if maxrb<=bb: first_pf_by_depth[p][bucket][a]+=1
            if maxrb<=bb and a=='BET':
                opensize[p][round((rb[pid]+put)/bb,1)]+=1
            # facing exactly one non-allin open raise of <= 3BB
            if bb<maxrb<=3*bb and call<stack:
                fold_small_open[p][1]+=1
                if a=='FOLD': fold_small_open[p][0]+=1
        else:
            if call==0 and a=='BET' and pot>0: betfrac[p][round(put/pot,1)]+=1
            if call>0 and call<stack and call<=0.6*(pot-call):
                fold_small_bet[p][1]+=1
                if a=='FOLD': fold_small_bet[p][0]+=1
for p in sorted(opensize,key=lambda p:rank.get(p,999)):
    if rank.get(p,999)>25: continue
    tot=sum(opensize[p].values()); tb=sum(betfrac[p].values())
    fo=fold_small_open[p]; fb=fold_small_bet[p]
    print(f"{p[:14]:14s} {rank[p]:3d} open-to(BB) top: {[ (k,round(v/tot,2)) for k,v in opensize[p].most_common(3)]} | post bet/pot top: {[(k,round(v/tb,2)) for k,v in betfrac[p].most_common(3)] if tb else '-'} | fold vs <=3BB open: {fo[0]/max(1,fo[1]):.2f} (n={fo[1]}) | fold vs <=~half-pot bet: {fb[0]/max(1,fb[1]):.2f} (n={fb[1]})")
print()
for p in ['Waffle3z','kovi','BrandV','Zylo','Tuo','JuMaKre']:
    print(p, {b: {k: round(v/sum(c.values()),2) for k,v in c.most_common()} for b,c in sorted(first_pf_by_depth[p].items())})

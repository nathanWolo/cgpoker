import pickle, collections, json, statistics as S
from analyze import PE, PCT, hclass
from common import REPLAYED_PKL, load_leaderboard
res=pickle.load(open(REPLAYED_PKL,'rb'))
lb=load_leaderboard(); rank={u['pseudo']:u['rank'] for u in lb['users']}
# chips won per hand in BB (by depth bucket), finishing positions in replay set, and 'who wins the chips'
won=collections.defaultdict(lambda: collections.defaultdict(list))
place=collections.defaultdict(lambda: collections.defaultdict(list))
showdown_share=collections.Counter(); allin_hands=0; hands_total=0; allin_pf=0
decided_by_allin=0
for gid,names,ranks,scores,out in res:
    n=len(names)
    for i,p in enumerate(names): place[p][n].append(ranks[i])
    hand=None
    for l in out.splitlines():
        f=l.split('\t')
        if f[0]=='HAND':
            bb=int(f[3]); stacks={}
            for x in f[7:]:
                i,s,c=x.split(':'); stacks[int(i)]=(int(s),c)
            alive=[i for i in stacks if stacks[i][1]!='OUT']
            hand={'bb':bb,'alive':alive,'stacks':stacks,'allin':False,'pfallin':False}
            hands_total+=1
        elif f[0]=='ACT':
            if f[12]=='ALL-IN' or (int(f[13])>0 and int(f[13])==int(f[9])):
                hand['allin']=True
                if f[4]=='-': hand['pfallin']=True
        elif f[0]=='END':
            nf=sum(1 for x in f[2:2+n] if x.split(':')[4]=='-' and int(x.split(':')[0]) in hand['alive'])
            if nf>=2: showdown_share['showdown']+=1
            else: showdown_share['uncontested']+=1
            if hand['allin']: allin_hands+=1
            if hand['pfallin']: allin_pf+=1
            for x in f[2:2+n]:
                i,c,w,s,fo=x.split(':'); i=int(i)
                if i in hand['alive']:
                    alive=hand['alive']; st=hand['stacks'][i][0]
                    eff=min(st, max(hand['stacks'][j][0] for j in alive if j!=i))/hand['bb'] if len(alive)>1 else 0
                    b='<10' if eff<10 else '10-20' if eff<20 else '20-40' if eff<40 else '40-80' if eff<80 else '80+'
                    won[names[i]][b].append(int(w)/hand['bb'])
                    won[names[i]]['all'].append(int(w)/hand['bb'])
print('hands',hands_total,'showdown/uncontested',showdown_share,'hands with an all-in',allin_hands/hands_total,'preflop all-in',allin_pf/hands_total)
print('player rank | mean BB won/hand overall and by eff-stack bucket (n) | mean place by n (0=1st) in replay set')
for p in sorted(won,key=lambda p:rank.get(p,999)):
    if len(won[p]['all'])<300: continue
    s=' '.join(f"{b}:{S.mean(won[p][b]):+.2f}({len(won[p][b])})" for b in ['all','<10','10-20','20-40','40-80','80+'] if won[p][b])
    pl=' '.join(f"{n}p:{S.mean(v):.2f}/{len(v)}" for n,v in sorted(place[p].items()))
    print(f"{p[:14]:14s} {rank.get(p,'?'):>3} | {s} | {pl}")

import pickle, collections, json, statistics as S
from analyze import PE, PCT, hclass
from common import REPLAYED_PKL, STATS_PKL, load_leaderboard
res=pickle.load(open(REPLAYED_PKL,'rb'))
lb=load_leaderboard(); rank={u['pseudo']:u['rank'] for u in lb['users']}
TOP=[u['pseudo'] for u in lb['users'] if u['rank']<=12]
st=collections.defaultdict(lambda: collections.Counter())
lists=collections.defaultdict(lambda: collections.defaultdict(list))
msgs=collections.defaultdict(collections.Counter)
G=collections.Counter(); hands_per_game=collections.defaultdict(list); rounds=[]; cancel=0
bbAtEnd=[]; effbb_hist=collections.Counter()
timeouts=collections.Counter()
for gid, names, ranks, scores, out in res:
    n=len(names)
    G[n]+=1
    lines=out.splitlines()
    cur=None; hand=None
    lasthand=0
    for l in lines:
        f=l.split('\t')
        if f[0]=='HAND':
            hn=int(f[1]); bb=int(f[3]); lasthand=hn
            stacks={}
            for x in f[7:]:
                i,s,c=x.split(':'); stacks[int(i)]=(int(s),c)
            alive=[i for i in stacks if stacks[i][1]!='OUT']
            hand={'bb':bb,'stacks':stacks,'alive':alive,'raised':False,'pf_actions':collections.defaultdict(list),'n_alive':len(alive),'sb':int(f[5]),'bbid':int(f[6]),'btn':int(f[4])}
            srt=sorted([stacks[i][0] for i in alive])
            eff = srt[-2] if len(srt)>=2 else srt[-1]   # 2nd largest stack = max effective
            effbb_hist[(len(alive), min(eff//bb, 300)//10*10)]+=1
            for i in alive:
                p=names[i]; st[p]['hands']+=1
                lists[p]['stackbb'].append(stacks[i][0]/bb)
                lists[p]['effbb'].append(min(stacks[i][0], max(stacks[j][0] for j in alive if j!=i))/bb if len(alive)>1 else 0)
        elif f[0]=='ACT':
            (_,turn,hn,pid,board,cards,bb,pot,call,stack,sts,poss,act,put,alive,notf,dealer,sb,bbid,raw)=f
            pid=int(pid); bb=int(bb); pot=int(pot); call=int(call); stack=int(stack); put=int(put)
            p=names[pid]
            if ';' in raw: msgs[p][raw.split(';',1)[1][:60]]+=1
            if act=='TIMEOUT': timeouts[p]+=1
            rb=[int(x.split('/')[2].rstrip('F')) for x in sts.split(',')]
            atype=act.split('_')[0]
            hc=hclass(cards); pct=PCT[hc]
            if board=='-':
                facing_raise = max(rb) > bb
                if pid not in hand['pf_actions']:
                    # first decision this hand
                    st[p]['pf_dec']+=1
                    key='pf_first_'+('vsraise' if facing_raise else 'unopened')
                    st[p][key+'_n']+=1
                    st[p][key+'_'+atype]+=1
                    if not facing_raise and atype in ('BET','ALL-IN'):
                        lists[p]['open_pct'].append(pct)
                        if atype=='BET': lists[p]['open_size_bb'].append(rb[pid]/bb + put/bb)
                    if not facing_raise and atype=='ALL-IN':
                        lists[p]['shove_pct'].append(pct); lists[p]['shove_effbb'].append(stack/bb)
                    if not facing_raise and atype=='CALL': lists[p]['limp_pct'].append(pct)
                    if not facing_raise and atype=='FOLD': lists[p]['fold_unopened_pct'].append(pct)
                hand['pf_actions'][pid].append(atype)
                if atype in ('CALL','BET','ALL-IN'): st[p]['vpip_hand_%d'%hand['bb']]+=0; hand.setdefault('vpip',set()).add(pid)
                if atype in ('BET','ALL-IN'): hand.setdefault('pfr',set()).add(pid)
                # facing all-in / big bet: call >= stack means decision to call all-in
                if call>0 and call>=stack:
                    st[p]['face_allin_n']+=1; st[p]['face_allin_'+atype]+=1
                    lists[p]['callallin_pct' if atype in ('ALL-IN','CALL') else 'foldallin_pct'].append(pct)
                    lists[p]['face_allin_potodds'].append(call/(pot+call))
                elif facing_raise and call>0:
                    st[p]['face_raise_n']+=1; st[p]['face_raise_'+atype]+=1
            else:
                street={3:'flop',4:'turn',5:'river'}[len(board.split('_'))]
                st[p]['post_n']+=1; st[p]['post_'+atype]+=1
                if call==0:
                    st[p]['post_nobet_n']+=1; st[p]['post_nobet_'+atype]+=1
                    if atype in ('BET','ALL-IN') and pot>0: lists[p]['bet_potfrac'].append(put/pot)
                else:
                    st[p]['post_facebet_n']+=1; st[p]['post_facebet_'+atype]+=1
        elif f[0]=='END':
            if hand:
                for i in hand['alive']:
                    p=names[i]
                    if i in hand.get('vpip',()): st[p]['vpip']+=1
                    if i in hand.get('pfr',()): st[p]['pfr']+=1
                    wa=int(f[2+i].split(':')[2]) if False else None
                for x in f[2:2+n]:
                    i,c,w,s,fo=x.split(':'); i=int(i)
                    if names[i] and i in hand['alive']:
                        lists[names[i]]['win_bb'].append(int(w)/hand['bb'])
        elif f[0]=='CANCEL': cancel+=1
        elif f[0]=='GAMEOVER': rounds.append(int(f[1]))
    hands_per_game[n].append(lasthand)
print('games by n',G,'cancelled(600 cap)',cancel)
for n in sorted(hands_per_game): v=hands_per_game[n]; print(n,'p hands/game median',S.median(v),'min',min(v),'max',max(v))
print('eff BB distribution of hands (alive, effBB bucket): ')
for na in (2,3,4):
    tot=sum(v for (a,b),v in effbb_hist.items() if a==na)
    print(na, ' '.join(f"{b}:{effbb_hist[(na,b)]/tot:.2f}" for b in range(0,310,10) if effbb_hist[(na,b)]))
print('timeouts',timeouts)
def fr(c,a,b): return f"{c[a]/c[b]:.2f}" if c[b] else '-'
print()
hdr='player rank hands VPIP PFR | unopened: n fold call(limp) bet shove | vsRaise n fold call raise allin | faceAllin n call% | post nobet: chk bet ; facebet: fold call raise | medOpenBB | medShoveEffBB | shove top% med | callAllin top% med'
print(hdr)
players=sorted(st, key=lambda p: rank.get(p,999))
for p in players:
    c=st[p]
    if c['hands']<150: continue
    L=lists[p]
    med=lambda k: f"{S.median(L[k]):.2f}" if L[k] else '-'
    print(f"{p[:14]:14s} {rank.get(p,'?'):>3} {c['hands']:5d} {fr(c,'vpip','hands')} {fr(c,'pfr','hands')} | {c['pf_first_unopened_n']:4d} {fr(c,'pf_first_unopened_FOLD','pf_first_unopened_n')} {fr(c,'pf_first_unopened_CALL','pf_first_unopened_n')} {fr(c,'pf_first_unopened_BET','pf_first_unopened_n')} {fr(c,'pf_first_unopened_ALL-IN','pf_first_unopened_n')} | {c['face_raise_n']:4d} {fr(c,'face_raise_FOLD','face_raise_n')} {fr(c,'face_raise_CALL','face_raise_n')} {fr(c,'face_raise_BET','face_raise_n')} {fr(c,'face_raise_ALL-IN','face_raise_n')} | {c['face_allin_n']:3d} {(c['face_allin_CALL']+c['face_allin_ALL-IN'])/max(1,c['face_allin_n']):.2f} | {fr(c,'post_nobet_CHECK','post_nobet_n')} {(c['post_nobet_BET']+c['post_nobet_ALL-IN'])/max(1,c['post_nobet_n']):.2f} ; {fr(c,'post_facebet_FOLD','post_facebet_n')} {fr(c,'post_facebet_CALL','post_facebet_n')} {(c['post_facebet_BET']+c['post_facebet_ALL-IN'])/max(1,c['post_facebet_n']):.2f} | {med('open_size_bb')} | {med('shove_effbb')} | {med('shove_pct')} | {med('callallin_pct')} bet/pot {med('bet_potfrac')}")
pickle.dump((dict(st),{k:dict(v) for k,v in lists.items()},{k:dict(v) for k,v in msgs.items()}),open(STATS_PKL,'wb'))

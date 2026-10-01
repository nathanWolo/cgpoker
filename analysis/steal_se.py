import pickle, collections, sys, statistics as S
from analyze import PCT, hclass
from common import REPLAYED_PKL
res=pickle.load(open(REPLAYED_PKL,'rb'))
R=collections.defaultdict(lambda:[0,0,0.0,0.0])  # (nlive, sizebucket, handbucket) -> n, allfold, net BB
Rj=collections.defaultdict(lambda:[0,0,0.0,0.0])
for gid,names,ranks,scores,out in res:
    cur=None
    for l in out.splitlines():
        f=l.split('\t')
        if f[0]=='HAND':
            cur={'open':None,'unopened':True,'acts':[], 'bb':int(f[3]),'n':sum(1 for x in f[7:] if int(x.split(':')[1])>0)}
        elif f[0]=='ACT' and cur is not None:
            (_,turn,hn,pid,board,cards,bb,pot,call,stack,sts,poss,act,put,alive,notf,dealer,sb,bbid,raw)=f
            if board!='-': continue
            a=act.split('_')[0]; pid=int(pid); bb=int(bb); put=int(put); call=int(call); stack=int(stack)
            rb=int(sts.split(',')[pid].split('/')[2].rstrip('F'))
            if cur['unopened'] and cur['open'] is None:
                if a=='BET' or (a=='ALL-IN' and put>call):
                    to=(rb+put)/bb; allin=(a=='ALL-IN')
                    sz='jam' if allin else '<=2' if to<=2.01 else '<=3' if to<=3.01 else '<=6' if to<=6.01 else '>6'
                    p=PCT[hclass(cards)]
                    hb='top10' if p<=.1 else 'top30' if p<=.3 else 'top60' if p<=.6 else 'bot40'
                    cur['open']=(pid,sz,hb,bb,int(notf)-1, stack/bb)
                    cur['others']=[]
                elif a in ('CALL','CHECK'):
                    cur['limped']=True
            elif cur['open'] is not None:
                if pid!=cur['open'][0]: cur['others'].append(a)
        elif f[0]=='END' and cur is not None and cur['open'] is not None:
            pid,sz,hb,bb,nbehind,depth=cur['open']
            if cur.get('limped'): cur=None; continue
            net=None
            for x in f[2:]:
                parts=x.split(':')
                if len(parts)>=3 and parts[0].isdigit() and int(parts[0])==pid: net=int(parts[2])
            allfold=all(a=='FOLD' for a in cur['others'])
            key=(cur['n'],sz)
            if depth<15: key=(cur['n'],sz+'(<15bb)')
            R[key][0]+=1; R[key][1]+=allfold; R[key][2]+=net/bb; R[key][3]+=(net/bb)**2
            Rj[(cur['n'],sz,hb)][0]+=1; Rj[(cur['n'],sz,hb)][1]+=allfold; Rj[(cur["n"],sz,hb)][2]+=net/bb; Rj[(cur["n"],sz,hb)][3]+=(net/bb)**2
            cur=None
print('unopened first-in raise (no limpers): n, P(all fold), opener mean net BB')
for k in sorted(R):
    n,f,s,_=R[k]
    if n>=40: print(k, n, round(f/n,3), round(s/n,2))
print()
for k in sorted(Rj):
    n,f,s,_=Rj[k]
    if n>=60 and k[1] in('<=2','<=3','<=6'): print(k,n,round(f/n,3),round(s/n,2))
print("\nWITH SE")
import math
for D in (R,Rj):
  for k in sorted(D):
    n,f,s,s2=D[k]
    if n>=60 and ('<=2' in k[1] or '<=3' in k[1]):
        m=s/n; sd=math.sqrt(max(s2/n-m*m,0)); print(k,n,'fold',round(f/n,3),'mean',round(m,2),'SE',round(sd/math.sqrt(n),2))

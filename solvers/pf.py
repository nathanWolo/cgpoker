import numpy as np, itertools, os
# eq169.bin sits next to this file (built by eq.c). exploit.py, mmdstep2.py and mmd_ab.py exec the part of this file above the main S loop.
E=np.fromfile(os.path.join(os.path.dirname(os.path.abspath(__file__)),'eq169.bin'),dtype=np.float64).reshape(169,169)
R='23456789TJQKA'
def combos(c):
    r1,r2=divmod(c,13)
    if r1==r2: return [(r1*4+a,r1*4+b) for a in range(4) for b in range(a+1,4)]
    if r1>r2: return [(r1*4+s,r2*4+s) for s in range(4)]
    return [(r2*4+a,r1*4+b) for a in range(4) for b in range(4) if a!=b]
CB=[combos(c) for c in range(169)]
cnt=np.array([len(x) for x in CB],float)
W=np.zeros((169,169))
for i in range(169):
    for j in range(169):
        W[i,j]=sum(1 for a in CB[i] for b in CB[j] if len({a[0],a[1],b[0],b[1]})==4)
def name(c):
    r1,r2=divmod(c,13)
    if r1==r2: return R[r1]*2
    return (R[r1]+R[r2]+'s') if r1>r2 else (R[r2]+R[r1]+'o')
def solve(S,iters=3000):
    J=np.ones(169)*0.5; C=np.ones(169)*0.5; Js=np.zeros(169); Cs=np.zeros(169)
    for t in range(1,iters+1):
        # BB best response to J
        num=(W*J[:,None]*(2*E.T[:,:].T*0-0)).sum(0)  # placeholder
        wj=W*J[:,None]                 # [i,j]
        evc=((wj*(2*(1-E)-1)).sum(0)*S)/np.maximum(wj.sum(0),1e-12)  # BB call EV (net, BB has already 1 in)
        Cbr=(evc>-1).astype(float)
        wc=W*C[None,:]
        pc=(wc.sum(1))/W.sum(1)       # prob called
        evj=(pc*(( (wc*(2*E-1)).sum(1)/np.maximum(wc.sum(1),1e-12))*S) + (1-pc)*1.0)
        Jbr=(evj>-0.5).astype(float)
        J=J+(Jbr-J)/(t+1); C=C+(Cbr-C)/(t+1)
    # SB value
    wc=W*C[None,:]; pc=wc.sum(1)/W.sum(1)
    evj=(pc*(((wc*(2*E-1)).sum(1)/np.maximum(wc.sum(1),1e-12))*S)+(1-pc)*1.0)
    v=(cnt*np.where(J>0.5,evj,-0.5)).sum()/1326
    return J,C,v
for S in [3,5,7,10,12,15,20,25]:
    J,C,v=solve(S)
    jp=(cnt*J).sum()/1326; cp=(cnt*C).sum()/1326
    worst_j=[name(c) for c in np.argsort(-J*1000+np.arange(169)*0)[:0]]
    print(f"S={S:>2}BB  SB jam {jp:5.1%}  BB call {cp:5.1%}  SB EV of jam/fold eq = {v:+.3f} BB/hand")
    if S in (10,15,20):
        print('   SB folds:', ' '.join(name(c) for c in range(169) if J[c]<0.5)[:400])
        print('   BB calls:', ' '.join(name(c) for c in range(169) if C[c]>0.5)[:400])

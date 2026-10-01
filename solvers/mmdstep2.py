import numpy as np
import numpy as np
import os; exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'pf.py')).read().split('for S in')[0])
def sb_ev(J,C,S):
    wc=W*C[None,:]; pc=wc.sum(1)/W.sum(1)
    evj=(pc*(((wc*(2*E-1)).sum(1)/np.maximum(wc.sum(1),1e-12))*S)+(1-pc)*1.0)
    return (cnt*(J*evj+(1-J)*-0.5)).sum()/1326, evj
# hand-strength order: by equity vs random (avg over W)
hs=(W*E).sum(1)/W.sum(1); order=np.argsort(-hs)
def top(frac):
    C=np.zeros(169); acc=0
    for c in order:
        if acc/1326>=frac: break
        C[c]=1; acc+=cnt[c]
    return C

def bb_br(J,S):
    wj=W*J[:,None]; evc=((wj*(2*(1-E)-1)).sum(0)*S)/np.maximum(wj.sum(0),1e-12)
    return (evc>-1).astype(float)
S=10; Jn,Cn,v0=solve(S)
eps=0.02; pj=np.clip(Jn,eps,1-eps)          # smoothed blueprint pi_theta(jam|hand)
alpha=0.02
for label,C in [('nit top15%',top(.15)),('station top70%',top(.70))]:
    _,evj=sb_ev(Jn,C,S)                       # q(jam) under opponent model; q(fold)=-0.5
    for eta in [0.3,1,3,10,1e9]:
        lj=(np.log(pj)+eta*evj+eta*alpha*np.log(0.5))/(1+eta*alpha)
        lf=(np.log(1-pj)+eta*(-0.5)+eta*alpha*np.log(0.5))/(1+eta*alpha)
        J=1/(1+np.exp(lf-lj))
        vm,_=sb_ev(J,C,S); vnash,_=sb_ev(Jn,C,S)
        wc,_=sb_ev(J,bb_br(J,S),S)            # value vs best-responding BB (worst case)
        kl=(cnt*(J*np.log(J/pj)+(1-J)*np.log((1-J)/(1-pj)))).sum()/1326
        print(f"10BB vs {label:14s} eta={eta:>6g}: gain vs model {vm-vnash:+.3f} BB/hand | worst-case value {wc:+.3f} (Nash {v0:+.3f}) | mean KL to blueprint {kl:.3f}")

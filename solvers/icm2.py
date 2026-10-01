from functools import lru_cache
def icm(stacks,pay):
    n=len(stacks); pay=list(pay)+[0]*(n-len(pay))
    alive=[i for i in range(n) if stacks[i]>0]; dead=[i for i in range(n) if stacks[i]<=0]
    out=[0.0]*n
    # dead players (busted this hand) take the last places (ties split)
    k=len(alive)
    if dead:
        v=sum(pay[k:k+len(dead)])/len(dead)
        for i in dead: out[i]=v
    @lru_cache(None)
    def rec(mask,place):
        idx=[i for i in range(n) if mask>>i&1]; tot=sum(stacks[i] for i in idx); o=[0.0]*n
        if len(idx)==1: o[idx[0]]=pay[place]; return tuple(o)
        for i in idx:
            p=stacks[i]/tot; o[i]+=p*pay[place]; sub=rec(mask&~(1<<i),place+1)
            for j in idx:
                if j!=i: o[j]+=p*sub[j]
        return tuple(o)
    r=rec(sum(1<<i for i in alive),0)
    for i in alive: out[i]=r[i]
    return out
def req(s,pay,a,b):
    m=min(s[a],s[b]); w=list(s); w[a]+=m; w[b]-=m; l=list(s); l[a]-=m; l[b]+=m
    e0,ew,el=icm(s,pay)[a],icm(w,pay)[a],icm(l,pay)[a]
    return (e0-el)/(ew-el)
T4=(1,.645,.355,0); T3=(1,.5,0)
print('3p equal', round(req([1600]*3,T3,0,1),3))
print('4p equal', round(req([1200]*4,T4,0,1),3))
print('4p (2400,1200,800,400) big calls short', round(req([2400,1200,800,400],T4,0,3),3))
print('4p (2400,1200,800,400) 2nd calls short', round(req([2400,1200,800,400],T4,1,3),3))
print('4p (600,1800,1800,600) big vs big', round(req([600,1800,1800,600],T4,1,2),3))
print('4p (600,1800,1800,600) short vs short', round(req([600,1800,1800,600],T4,0,3),3))
print('4p one out (3000,900,900) mid vs mid', round(req([3000,900,900],(1,.645,.355),1,2),3))
print('4p one out (1600,1600,1600) ', round(req([1600]*3,(1,.645,.355),0,1),3))
print('3p (1600,1600,1600) with linear', round(req([1600]*3,T3,0,1),3))

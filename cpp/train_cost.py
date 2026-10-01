# CPU cost of one training step (fwd + bwd, plain SGD update) for small MLPs, numpy/BLAS float32.
import numpy as np, time
np.random.seed(0)
def step(Ws, X, B):
    acts=[X]; h=X
    for i,W in enumerate(Ws):
        h=h@W
        if i<len(Ws)-1: h=np.maximum(h,0)
        acts.append(h)
    g=np.random.randn(*h.shape).astype(np.float32)  # dL/dout stand-in
    for i in range(len(Ws)-1,-1,-1):
        gW=acts[i].T@g
        if i>0:
            g=(g@Ws[i].T)*(acts[i]>0)
        Ws[i]-=1e-4*gW
for dims in [(128,128,128,8),(128,256,256,8),(256,512,256,8)]:
    Ws=[(np.random.randn(a,b)*0.05).astype(np.float32) for a,b in zip(dims[:-1],dims[1:])]
    P=sum(W.size for W in Ws)
    for B in (4096,):
        X=np.random.randn(B,dims[0]).astype(np.float32)
        step(Ws,X,B); t=time.time(); n=0
        while time.time()-t<2: step(Ws,X,B); n+=1
        dt=(time.time()-t)/n
        print(f"MLP {dims} {P} params, batch {B}: {dt*1e3:.1f} ms/step -> {B/dt/1e3:.0f}k samples/s train (fwd+bwd), {6*P*B/dt/1e9:.0f} GFLOP/s")

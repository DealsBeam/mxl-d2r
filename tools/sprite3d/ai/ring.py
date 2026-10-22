import sys, itertools
import numpy as np
from PIL import Image, ImageFilter
fdir, f = sys.argv[1], sys.argv[2]
ims=[]
for d in range(16):
    im=Image.open(f"{fdir}/d{d}_f{f}.png").convert("RGBA")
    bg=Image.new("RGBA",im.size,(0,0,0,255)); bg.alpha_composite(im)
    g=np.asarray(bg.convert("RGB").resize((48,48),Image.BILINEAR),np.float32)/255
    ims.append(g.ravel())
X=np.array(ims)
D=np.sqrt(((X[:,None]-X[None])**2).sum(-1))
n=16
# held-karp for shortest Hamiltonian cycle
INF=1e18
dp=np.full((1<<n,n),INF); par=np.full((1<<n,n),-1)
dp[1][0]=0
for m in range(1<<n):
    if not m&1: continue
    for j in range(n):
        v=dp[m][j]
        if v>=INF or not m>>j&1: continue
        for k in range(n):
            if m>>k&1: continue
            nm=m|1<<k; nv=v+D[j][k]
            if nv<dp[nm][k]: dp[nm][k]=nv; par[nm][k]=j
full=(1<<n)-1
best=min(range(1,n),key=lambda j:dp[full][j]+D[j][0])
order=[];m=full;j=best
while j!=-1:
    order.append(j); pj=par[m][j]; m^=1<<j; j=pj
order=order[::-1]
print("ring:",order, "cost",dp[full][best]+D[best][0])
print("nearest:",[int(np.argsort(D[i])[1]) for i in range(n)])

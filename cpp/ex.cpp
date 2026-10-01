#pragma GCC optimize("O3")
// Exact heads-up preflop all-in equity: enumerates all C(48,5) = 1,712,304 boards with pe7c.hpp.
#include <cstdio>
#include <chrono>
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
#include "pe7c.hpp"
using namespace pe;
double exact(int a0,int a1,int b0,int b1){ u64 u=1ull<<a0|1ull<<a1|1ull<<b0|1ull<<b1; double w=0,n=0; H ha=add(add(E,a0),a1), hb=add(add(E,b0),b1);
 int c[48],m=0; for(int i=0;i<52;i++) if(!(u>>i&1)) c[m++]=i;
 for(int i=0;i<m;i++)for(int j=i+1;j<m;j++)for(int k=j+1;k<m;k++)for(int l=k+1;l<m;l++)for(int q=l+1;q<m;q++){ H b=add(add(add(add(add(E,c[i]),c[j]),c[k]),c[l]),c[q]); u16 x=ev(add(b,ha)),y=ev(add(b,hb)); w+= x>y?1:x==y?0.5:0; n++; }
 return w/n; }
int main(){ double t0=now(); init(); double t1=now();
 // 7c2d vs AhAs ; 7h2c vs AhAs (shares a suit); card = 4*rank+suit, suits c=0 d=1 h=2 s=3; rank 2->0, 7->5, A->12
 printf("7c2d vs AhAs %.4f\n", exact(5*4+0,0*4+1,12*4+2,12*4+3));
 printf("7h2c vs AhAs %.4f\n", exact(5*4+2,0*4+0,12*4+2,12*4+3));
 printf("7h2s vs AhAs %.4f\n", exact(5*4+2,0*4+3,12*4+2,12*4+3));
 printf("AKs(AsKs) vs QcQd %.4f\n", exact(12*4+3,11*4+3,10*4+0,10*4+1));
 printf("init %.1f ms; 4 exact matchups %.2f ms (%.2f ms each)\n",(t1-t0)*1e3,(now()-t1)*1e3,(now()-t1)*1e3/4);
}

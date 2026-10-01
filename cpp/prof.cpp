// Profiles pe7.hpp start-up: rank-multiset enumeration time, hash row statistics for PE_SH, full init().
#include <cstdio>
#include <chrono>
#define private public
#include "pe7.hpp"
using namespace pe;
int main(){ auto t0=std::chrono::steady_clock::now();
 std::vector<std::pair<u32,u32>> nf; int cnt[13]={0};
 auto rec=[&](auto&&self,int r,int n,u32 key)->void{ if(r==13){ if(n>=5) nf.push_back({key,slowc(cnt)}); return;} for(int k=0;k<=4&&n+k<=7;k++){cnt[r]=k; self(self,r+1,n+k,key+k*RK[r]);} cnt[r]=0;};
 rec(rec,0,0,0);
 auto t1=std::chrono::steady_clock::now();
 std::vector<u32> rowsz((MAXK>>SH)+1); for(auto&x:nf) rowsz[x.first>>SH]++;
 int ne=0,mx=0; for(auto c:rowsz){ if(c) ne++; if((int)c>mx) mx=c;}
 printf("multisets %zu in %.2f ms; nonempty rows %d max row %d\n", nf.size(), std::chrono::duration<double>(t1-t0).count()*1e3, ne, mx);
 auto t2=std::chrono::steady_clock::now(); init(); auto t3=std::chrono::steady_clock::now();
 printf("full init %.1f ms\n", std::chrono::duration<double>(t3-t2).count()*1e3);
}

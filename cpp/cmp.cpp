// Cross-check and speed comparison of pe7c.hpp against OMPEval and PHEvaluator (HenryRLee).
// Their sources are not in the repo: ./fetch_third_party.sh, then `make cmp` (native -O3 only).
#include <cstdio>
#include <chrono>
#include "pe7c.hpp"
#include "omp/HandEvaluator.h"
extern "C" { int evaluate_7cards(int a, int b, int c, int d, int e, int f, int g); }
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static uint64_t rs=88172645463325252ull; static uint64_t rnd(){rs^=rs<<7;rs^=rs>>9;return rs;}
int main(){
  double t0=now(); pe::init(); double t1=now(); omp::HandEvaluator oe; double t2=now();
  printf("init: pe7c %.1f ms, OMPEval %.1f ms (offsets precompiled)\n",(t1-t0)*1e3,(t2-t1)*1e3);
  const int N=1<<20; static unsigned char hs[N][7];
  for(int i=0;i<N;i++){uint64_t u=0;for(int j=0;j<7;j++){int c;do c=(int)(((rnd()>>32)*52)>>32);while(u>>c&1);u|=1ull<<c;hs[i][j]=c;}}
  // cross-check ordering on random pairs
  long bad=0; for(int i=0;i+1<N;i+=2){ pe::H a=pe::E,b=pe::E; omp::Hand oa=omp::Hand::empty(),ob=omp::Hand::empty();
    for(int j=0;j<7;j++){a=pe::add(a,hs[i][j]);b=pe::add(b,hs[i+1][j]);oa+=omp::Hand(hs[i][j]);ob+=omp::Hand(hs[i+1][j]);}
    int x=pe::ev(a),y=pe::ev(b),p=oe.evaluate(oa),q=oe.evaluate(ob); int henry=evaluate_7cards(hs[i][0],hs[i][1],hs[i][2],hs[i][3],hs[i][4],hs[i][5],hs[i][6]), henry2=evaluate_7cards(hs[i+1][0],hs[i+1][1],hs[i+1][2],hs[i+1][3],hs[i+1][4],hs[i+1][5],hs[i+1][6]);
    if(((x>y)-(x<y))!=((p>q)-(p<q))) bad++; if(((x>y)-(x<y))!=((henry2>henry)-(henry2<henry))) bad++; }
  printf("ordering disagreements vs OMPEval and PHEvaluator on %d random pairs: %ld\n",N/2,bad);
  const int R=10; unsigned long long s=0;
  t0=now(); for(int r=0;r<R;r++) for(int i=0;i<N;i++){pe::H h=pe::E; for(int j=0;j<7;j++) h=pe::add(h,hs[i][j]); s+=pe::ev(h);} t1=now();
  printf("pe7c     rand: %.0f M/s\n",R*(double)N/(t1-t0)/1e6);
  t0=now(); for(int r=0;r<R;r++) for(int i=0;i<N;i++){omp::Hand h=omp::Hand::empty(); for(int j=0;j<7;j++) h+=omp::Hand(hs[i][j]); s+=oe.evaluate(h);} t1=now();
  printf("OMPEval  rand: %.0f M/s\n",R*(double)N/(t1-t0)/1e6);
  t0=now(); for(int r=0;r<R;r++) for(int i=0;i<N;i++){ s+=evaluate_7cards(hs[i][0],hs[i][1],hs[i][2],hs[i][3],hs[i][4],hs[i][5],hs[i][6]);} t1=now();
  printf("PHEval   rand: %.0f M/s   (%llu)\n",R*(double)N/(t1-t0)/1e6,s);
}

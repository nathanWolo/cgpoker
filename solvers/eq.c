// 169x169 preflop all-in equity matrix by Monte Carlo (class vs class, uniform over non-conflicting combos)
// Usage: eq [trials_per_pair=20000] [out=eq169.bin]
//   writes 169*169 little-endian float64, E[i][j] = P(class i beats class j) (ties count 1/2).
//   Class index: pairs r*13+r; suited hi*13+lo (hi>lo); offsuit lo*13+hi (ranks 0..12 = 2..A).
//   Build/run from the repo root: gcc -O2 -o build/eq solvers/eq.c && build/eq 20000 solvers/eq169.bin
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static uint64_t s=88172645463325252ULL; static inline uint64_t xr(){s^=s<<13;s^=s>>7;s^=s<<17;return s;}
// card = rank*4+suit, rank 0..12 (2..A)
static int eval7(const int*c){
  int rc[13]={0}; int sm[4]={0}; int sc[4]={0}; int rm=0;
  for(int i=0;i<7;i++){int r=c[i]>>2,su=c[i]&3; rc[r]++; sm[su]|=1<<r; sc[su]++; rm|=1<<r;}
  // straight flush / flush
  for(int su=0;su<4;su++) if(sc[su]>=5){ int m=sm[su];
      int mm=m|((m>>12)&1); // wheel: ace as low -> shift
      int ext=(m<<1)|((m>>12)&1); // bit0 = ace low
      for(int hi=12;hi>=3;hi--){ int need=0x1F<<(hi-3); if((ext&(0x1F<<(hi-3+1-1+0)))==0) {} }
      (void)mm;
      // check straight within flush
      int e=(m<<1)|((m>>12)&1); // bits 1..13 ranks, bit0 ace-low
      for(int top=13;top>=4;top--){ int msk=0x1F<<(top-4); if((e&msk)==msk) return (8<<20)|top; }
      int k=0,v=0; for(int r=12;r>=0&&k<5;r--) if(m>>r&1){v=v*16+r;k++;} return (5<<20)|v; }
  int quad=-1,trips[2]={-1,-1},nt=0,pairs[3]={-1,-1,-1},np=0;
  for(int r=12;r>=0;r--){ if(rc[r]==4)quad=r; else if(rc[r]==3){if(nt<2)trips[nt++]=r;} else if(rc[r]==2){if(np<3)pairs[np++]=r;} }
  if(quad>=0){ int k=-1; for(int r=12;r>=0;r--) if(r!=quad&&rc[r]){k=r;break;} return (7<<20)|quad*16+k; }
  if(nt>=1 && (nt>=2||np>=1)){ int p= nt>=2? trips[1]:pairs[0]; if(nt>=2&&np>=1&&pairs[0]>p)p=pairs[0]; return (6<<20)|trips[0]*16+p; }
  int e=(rm<<1)|((rm>>12)&1);
  for(int top=13;top>=4;top--){ int msk=0x1F<<(top-4); if((e&msk)==msk) return (4<<20)|top; }
  if(nt==1){ int v=trips[0],k=0; for(int r=12;r>=0&&k<2;r--) if(r!=trips[0]&&rc[r]){v=v*16+r;k++;} return (3<<20)|v; }
  if(np>=2){ int k=-1; for(int r=12;r>=0;r--) if(r!=pairs[0]&&r!=pairs[1]&&rc[r]){k=r;break;} return (2<<20)|(pairs[0]*16+pairs[1])*16+k; }
  if(np==1){ int v=pairs[0],k=0; for(int r=12;r>=0&&k<3;r--) if(r!=pairs[0]&&rc[r]){v=v*16+r;k++;} return (1<<20)|v; }
  int v=0,k=0; for(int r=12;r>=0&&k<5;r--) if(rc[r]){v=v*16+r;k++;} return v;
}
// class index: pairs r*13+r; suited hi*13+lo (hi>lo) ; offsuit lo*13+hi
static void randcombo(int cls,int*a,int*b,uint64_t used){
  int r1=cls/13,r2=cls%13;
  for(;;){ int s1=xr()&3,s2=xr()&3; int x,y;
    if(r1==r2){ if(s1==s2)continue; x=r1*4+s1;y=r2*4+s2; }
    else if(r1>r2){ x=r1*4+s1;y=r2*4+s1; } // suited
    else { if(s1==s2)continue; x=r2*4+s1;y=r1*4+s2; } // offsuit (r1<r2): hi=r2
    if((used>>x&1)||(used>>y&1)) continue; *a=x;*b=y;return; }
}
static int possible(int c1,int c2){ // any non-conflicting combos?
  return 1; }
int main(int argc,char**argv){
  int N=argc>1?atoi(argv[1]):20000;
  const char*out=argc>2?argv[2]:"eq169.bin";
  static double E[169][169];
  for(int i=0;i<169;i++) for(int j=i;j<169;j++){
    // check feasibility: e.g. AA vs AA ok (2 combos), AKs vs AA fine
    double w=0; int t;
    for(t=0;t<N;t++){
      int a,b,c,d; uint64_t used=0; int tries=0;
      randcombo(i,&a,&b,0); used|=1ULL<<a|1ULL<<b;
      // for j, need combos not conflicting; randcombo loops; guard infinite loops
      int r1=j/13,r2=j%13; int okc=0;
      for(tries=0;tries<200;tries++){ int s1=xr()&3,s2=xr()&3,x,y;
        if(r1==r2){ if(s1==s2)continue; x=r1*4+s1;y=r2*4+s2; } else if(r1>r2){x=r1*4+s1;y=r2*4+s1;} else { if(s1==s2)continue; x=r2*4+s1;y=r1*4+s2; }
        if((used>>x&1)||(used>>y&1)) continue; c=x;d=y;okc=1;break; }
      if(!okc){ t--; if(++tries>1000000) break; continue; }
      used|=1ULL<<c|1ULL<<d;
      int h1[7],h2[7]; h1[0]=a;h1[1]=b;h2[0]=c;h2[1]=d; int k=2;
      while(k<7){ int x=xr()%52; if(used>>x&1)continue; used|=1ULL<<x; h1[k]=h2[k]=x; k++; }
      int v1=eval7(h1),v2=eval7(h2); w+= v1>v2?1.0:(v1==v2?0.5:0.0);
    }
    E[i][j]=w/N; E[j][i]=1-E[i][j];
  }
  FILE*f=fopen(out,"wb"); if(!f){perror(out);return 1;} fwrite(E,sizeof(E),1,f); fclose(f);
  // sanity prints
  int AA=12*13+12, KK=11*13+11, AKo=11*13+12, T2o=0*13+8, s72o=0*13+5, QJs=10*13+9, s22=0;
  printf("AA vs KK %.3f (ref .82)\nAKo vs 22 %.3f (ref ~.47)\nAA vs 72o %.3f (ref ~.88)\nQJs vs 22 %.3f (ref ~.47)\n",E[AA][KK],E[AKo][s22],E[AA][s72o],E[QJs][s22]);
  return 0;
}

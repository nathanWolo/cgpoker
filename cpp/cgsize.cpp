#pragma GCC optimize("O3")
// Size probe: `make cgsize` minifies this file (pe7c.hpp inlined) with crossfish's tools/cg_minify.py,
// reports the character count and checks the minified program prints the same value.
#include <cstdio>
#include "pe7c.hpp"
int main(){ pe::init(); pe::H h=pe::E; for(int c=0;c<7;c++) h=pe::add(h,c*7%52); printf("%d\n",pe::ev(h)); }

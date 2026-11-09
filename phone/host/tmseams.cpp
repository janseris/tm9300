#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "tmphys.h"
int main(int argc,char**argv){ FILE* fi=fopen(argv[1],"rb"); fseek(fi,0,SEEK_END); long sz=ftell(fi); fseek(fi,0,SEEK_SET); unsigned char* d=new unsigned char[sz]; fread(d,1,sz,fi);
 TmTrack t; t.Load(d,sz); TmGround g; g.Init(t);
 int jumps=0, none=0, n=0; double worst=0;
 for (int i=0;i+1<t.iNPath;i++){ const TmPath&a=t.iPath[i]; const TmPath&b=t.iPath[i+1];
   double len=hypot(b.x-a.x,b.z-a.z)/16; int steps=(int)(len/0.05)+1; double prev=-1e9;
   for(int k=0;k<=steps;k++){ double f=(double)k/steps; double x=(a.x+(b.x-a.x)*f)/16, z=(a.z+(b.z-a.z)*f)/16, y=(a.y+(b.y-a.y)*f)/16;
     TmGroundHit h; bool ok=g.Probe((int)(x*256),(int)(z*256),(int)((y+1.25)*256),h); n++;
     if(!ok){ none++; prev=-1e9; continue; }
     double hy=h.y/256.0; if(prev>-1e8 && fabs(hy-prev)>0.08){ jumps++; if(fabs(hy-prev)>worst) worst=fabs(hy-prev); if(jumps<=12) printf("jump %.2f m at (%.1f,%.1f) sample %d: %.2f -> %.2f\n",hy-prev,x,z,i,prev,hy);} prev=hy; }
 }
 printf("%d probes along the path: %d jumps over 8 cm (worst %.2f m), %d without ground\n",n,jumps,worst,none);
}

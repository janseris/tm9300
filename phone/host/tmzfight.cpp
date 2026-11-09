// pairs of collision instances with surfaces at the same height at the same point (they flicker when drawn)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "tmphys.h"
int main(int argc,char**argv){ FILE* fi=fopen(argv[1],"rb"); fseek(fi,0,SEEK_END); long sz=ftell(fi); fseek(fi,0,SEEK_SET); unsigned char* d=new unsigned char[sz]; fread(d,1,sz,fi);
 TmTrack t; t.Load(d,sz); TmGround g; g.Init(t); int bad=0;
 for (int i=0;i<t.iNCInst;i++){ const TmCInst& a=t.iCInst[i];
   for (int j=i+1;j<t.iNCInst;j++){ const TmCInst& b=t.iCInst[j];
     int x0=a.bb[0]>b.bb[0]?a.bb[0]:b.bb[0], x1=a.bb[3]<b.bb[3]?a.bb[3]:b.bb[3], z0=a.bb[2]>b.bb[2]?a.bb[2]:b.bb[2], z1=a.bb[5]<b.bb[5]?a.bb[5]:b.bb[5];
     if (x1-x0<64 || z1-z0<64) continue;
     if (a.bb[1]>b.bb[4]+4 || b.bb[1]>a.bb[4]+4) continue;
     int hits=0;
     for (int k=0;k<9;k++){ int px=(x0+(x1-x0)*(1+2*(k%3))/6)*16, pz=(z0+(z1-z0)*(1+2*(k/3))/6)*16; TmGroundHit ha,hb; bool fa=false,fb=false; int ya=0,yb=0;
       // the highest face of each instance at the point
       for (int f=0;f<t.iColl[a.coll].nf;f++){ if (g.FaceAt(i,f,px,pz,1000*256,ha) && (!fa || ha.y>ya)) { fa=true; ya=ha.y; } }
       for (int f=0;f<t.iColl[b.coll].nf;f++){ if (g.FaceAt(j,f,px,pz,1000*256,hb) && (!fb || hb.y>yb)) { fb=true; yb=hb.y; } }
       if (fa && fb && abs(ya-yb)<40) hits++; }
     if (hits>=3){ bad++; if (bad<=8) printf("instances %d (coll %d at %d,%d,%d) and %d (coll %d at %d,%d,%d): the same height at %d of 9 points\n",i,a.coll,a.x/16,a.y/16,a.z/16,j,b.coll,b.x/16,b.y/16,b.z/16,hits); } } }
 printf("%d overlapping pairs at the same height\n",bad); }

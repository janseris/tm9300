#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "tmphys.h"
int main(int argc,char**argv){ FILE* fi=fopen(argv[1],"rb"); fseek(fi,0,SEEK_END); long sz=ftell(fi); fseek(fi,0,SEEK_SET); unsigned char* d=new unsigned char[sz]; fread(d,1,sz,fi);
 TmTrack t; t.Load(d,sz); TmGround g; g.Init(t);
 // along the replay path: the ground under each sample vs the replay's y
 for (int i=0;i<t.iNPath;i++){ const TmPath&p=t.iPath[i]; TmGroundHit h; bool f=g.Probe(p.x*16,p.z*16,p.y*16+640,h);
   printf("sample %3d (%6.1f,%5.1f,%6.1f): ground %s %6.2f  diff %5.2f surf %d ny %d\n", i, p.x/16.0,p.y/16.0,p.z/16.0, f?"":"NONE", f?h.y/256.0:0, f?(p.y/16.0-h.y/256.0):0, f?h.surf:-1, f?h.ny:0); }
}

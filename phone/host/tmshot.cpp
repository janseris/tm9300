// put the car at (x, z) heading yaw, drive with throttle, save frames: tmshot track.tmb x z yaw out steps every
#include <stdio.h>
#include <stdlib.h>
#include "tmphys.h"
static void Save(const char* name, const R3Pix* f, int w, int h){ FILE* o=fopen(name,"wb"); fprintf(o,"P6 %d %d 255\n",w,h); for(int i=0;i<w*h;i++){ int c=f[i]; unsigned char rgb[3]={(unsigned char)(((c>>11)&31)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)}; fwrite(rgb,1,3,o);} fclose(o); }
int main(int argc,char**argv){ FILE* fi=fopen(argv[1],"rb"); fseek(fi,0,SEEK_END); long sz=ftell(fi); fseek(fi,0,SEEK_SET); unsigned char* d=new unsigned char[sz]; fread(d,1,sz,fi);
 TmTrack t; t.Load(d,sz); TmGround g; g.Init(t); TmCar car; car.Reset(t);
 double x=atof(argv[2]), z=atof(argv[3]); int yaw=atoi(argv[4]); const char* out=argv[5]; int steps=atoi(argv[6]), every=atoi(argv[7]);
 TmGroundHit h; g.Probe((int)(x*256),(int)(z*256),1000*256,h);
 car.iS.x=(int)(x*256); car.iS.z=(int)(z*256); car.iS.y=h.y; car.iS.yaw=yaw; car.iS.yaw16=yaw*16;
 TmFollowCam cam; cam.Reset(car.iS); TmView v; v.Init(320,200,2500); v.iFarCut=180*16; R3Pix* frame=new R3Pix[320*200];
 for (int k=0;k<steps;k++){ TmCarInput in; in.throttle=1; in.brake=0; in.steer=0; car.Step(g,in); int cx,cy,cz,cyaw,cp; cam.Update(g,car.iS,cx,cy,cz,cyaw,cp);
   if (k%every==0){ car.iS.Slopes(v.iCarSlopeF,v.iCarSlopeR); v.Frame(frame,320,t,cx,cy,cz,cyaw,cp,200,car.iS.x>>4,(car.iS.y>>4)+3,car.iS.z>>4,car.iS.yaw,1); char n[256]; sprintf(n,"%s_%03d.ppm",out,k); Save(n,frame,320,200);
     printf("step %d: (%.1f,%.2f,%.1f) speed %.1f %s n (%d,%d,%d)\n",k,car.iS.x/256.0,car.iS.y/256.0,car.iS.z/256.0,car.iS.speed/256.0,car.iS.onGround?"G":"A",car.iS.nx,car.iS.ny,car.iS.nz); } }
}

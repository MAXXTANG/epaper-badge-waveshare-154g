#pragma once
#include "Palette.h"

namespace epaper {
constexpr size_t kRgbBytes=kSide*kSide*3;
constexpr int kToneMin=-40,kToneMax=40,kToneStep=20;
inline bool validTone(int value){return value>=kToneMin&&value<=kToneMax&&value%kToneStep==0;}
inline int clampTone(int value){return value<kToneMin?kToneMin:(value>kToneMax?kToneMax:value);}
inline float toneChannel(float value,int brightness){return fminf(255,fmaxf(0,value+brightness*2.55f));}
inline void resizeRgb(const ImageReceiver& image,uint8_t* output){
  const int side=image.width()<image.height()?image.width():image.height();
  const float ox=(image.width()-side)*.5f,oy=(image.height()-side)*.5f;
  for(int y=0;y<kSide;y++)for(int x=0;x<kSide;x++){
    const float sx=fmaxf(0,ox+(x+.5f)*side/kSide-.5f),sy=fmaxf(0,oy+(y+.5f)*side/kSide-.5f);
    const int x0=int(sx),y0=int(sy),x1=x0+1<image.width()?x0+1:x0,y1=y0+1<image.height()?y0+1:y0;
    const float fx=sx-x0,fy=sy-y0;float c[4][3];
    rgb(image,x0,y0,c[0]);rgb(image,x1,y0,c[1]);rgb(image,x0,y1,c[2]);rgb(image,x1,y1,c[3]);
    for(int ch=0;ch<3;ch++)output[(y*kSide+x)*3+ch]=uint8_t(lroundf((1-fy)*((1-fx)*c[0][ch]+fx*c[1][ch])+fy*((1-fx)*c[2][ch]+fx*c[3][ch])));
  }
}
inline void quantizeRgb(const uint8_t* source,uint8_t* frame,int brightness,bool dither=true){
  static const float palette[4][3]={{0,0,0},{255,255,255},{255,230,0},{225,25,25}};
  float errors[2][kSide+2][3]={};memset(frame,0,kFrameBytes);
  for(int y=0;y<kSide;y++){
    float (*cur)[3]=errors[y%2],(*next)[3]=errors[(y+1)%2];memset(next,0,sizeof(errors[0]));
    const int dir=y%2?-1:1;
    for(int xi=0;xi<kSide;xi++){
      const int x=dir==1?xi:kSide-1-xi,pos=y*kSide+x;float value[3];
      for(int ch=0;ch<3;ch++)value[ch]=fminf(255,fmaxf(0,toneChannel(source[pos*3+ch],brightness)+(dither?cur[x+1][ch]:0)));
      const uint8_t code=nearest(value[0],value[1],value[2]);frame[pos/4]|=code<<((3-pos%4)*2);
      if(dither)for(int ch=0;ch<3;ch++){
        const float e=value[ch]-palette[code][ch];cur[x+1+dir][ch]+=e*7/16;
        next[x+1-dir][ch]+=e*3/16;next[x+1][ch]+=e*5/16;next[x+1+dir][ch]+=e/16;
      }
    }
  }
}
inline void unpackRgb(const uint8_t* packed,uint8_t* output){
  static const uint8_t palette[4][3]={{0,0,0},{255,255,255},{255,230,0},{225,25,25}};
  for(int p=0;p<kSide*kSide;p++){const int c=(packed[p/4]>>((3-p%4)*2))&3;memcpy(output+p*3,palette[c],3);}
}
} // namespace epaper

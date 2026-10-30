#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "ImageReceiver.h"

namespace epaper {
constexpr int kSide = 200;
constexpr int kFrameBytes = kSide * kSide / 4;
// Panel codes: black=00, white=01, yellow=10, red=11, MSB pixel first.
inline uint8_t nearest(float r, float g, float b) {
  static const float colors[4][3] = {{0,0,0}, {255,255,255}, {255,230,0}, {225,25,25}};
  float best = 1e20f;
  uint8_t result = 0;
  for (int i = 0; i < 4; ++i) {
    const float dr = r-colors[i][0], dg = g-colors[i][1], db = b-colors[i][2];
    const float d = dr*dr*0.30f + dg*dg*0.59f + db*db*0.11f;
    if (d < best) { best = d; result = i; }
  }
  return result;
}
inline void rgb(const ImageReceiver& image, int x, int y, float out[3]) {
  const uint32_t i = uint32_t(y) * image.width() + x;
  const uint8_t* p = image.pixels() + i * (image.depth()/8);
  if (image.depth() == 24) { out[0]=p[0]; out[1]=p[1]; out[2]=p[2]; }
  else {
    const uint16_t v = uint16_t(p[0]) | (uint16_t(p[1])<<8);
    out[0] = ((v>>11)&31) * (255.0f/31);
    out[1] = ((v>>5)&63) * (255.0f/63);
    out[2] = (v&31) * (255.0f/31);
  }
}
inline void convert(const ImageReceiver& image, uint8_t* frame) {
  // Bilinear resize, centered square crop, then serpentine error diffusion.
  static const float palette[4][3] = {{0,0,0}, {255,255,255}, {255,230,0}, {225,25,25}};
  float errors[2][kSide+2][3] = {};
  memset(frame, 0x55, kFrameBytes);
  const int srcSide = image.width() < image.height() ? image.width() : image.height();
  const float xOffset = (image.width()-srcSide)*0.5f;
  const float yOffset = (image.height()-srcSide)*0.5f;
  for (int y=0; y<kSide; ++y) {
    float (*cur)[3] = errors[y%2];
    float (*next)[3] = errors[(y+1)%2];
    memset(next, 0, sizeof(errors[0]));
    const int dir = y%2 ? -1 : 1;
    for (int xi=0; xi<kSide; ++xi) {
      const int x = dir==1 ? xi : kSide-1-xi;
      const float sx = fmaxf(0, xOffset+(x+0.5f)*srcSide/kSide-0.5f);
      const float sy = fmaxf(0, yOffset+(y+0.5f)*srcSide/kSide-0.5f);
      const int x0 = int(sx), y0=int(sy);
      const int x1 = x0+1 < image.width() ? x0+1 : x0;
      const int y1 = y0+1 < image.height() ? y0+1 : y0;
      const float fx=sx-x0, fy=sy-y0;
      float c[4][3], value[3];
      rgb(image,x0,y0,c[0]); rgb(image,x1,y0,c[1]);
      rgb(image,x0,y1,c[2]); rgb(image,x1,y1,c[3]);
      for (int ch=0; ch<3; ++ch) {
        value[ch] = (1-fy)*((1-fx)*c[0][ch]+fx*c[1][ch]) +
                    fy*((1-fx)*c[2][ch]+fx*c[3][ch]) + cur[x+1][ch];
        value[ch] = fminf(255, fmaxf(0, value[ch]));
      }
      const uint8_t code = nearest(value[0], value[1], value[2]);
      const int pos=y*kSide+x, shift=(3-pos%4)*2;
      frame[pos/4] = (frame[pos/4] & ~(3<<shift)) | (code<<shift);
      for (int ch=0; ch<3; ++ch) {
        const float error = value[ch]-palette[code][ch];
        cur[x+1+dir][ch] += error*7/16;
        next[x+1-dir][ch] += error*3/16;
        next[x+1][ch] += error*5/16;
        next[x+1+dir][ch] += error/16;
      }
    }
  }
}
}  // namespace epaper

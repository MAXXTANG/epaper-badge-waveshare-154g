#include <assert.h>
#include <stdio.h>
#include <vector>
#include "../firmware/EpaperBLE/Palette.h"

static std::vector<uint8_t> packet(uint16_t w,uint16_t h,uint8_t depth,uint8_t r,uint8_t g,uint8_t b) {
  std::vector<uint8_t> p={'!','I',depth,uint8_t(w),uint8_t(w>>8),uint8_t(h),uint8_t(h>>8)};
  for (size_t i=0;i<size_t(w)*h;++i) {
    if (depth==24) { p.push_back(r);p.push_back(g);p.push_back(b); }
    else { uint16_t v=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);p.push_back(v);p.push_back(v>>8); }
  }
  uint8_t sum=0;for(auto b:p)sum+=b;p.push_back(~sum);
  return p;
}
int main() {
  std::vector<uint8_t> buf(ImageReceiver::kCapacity),frame(epaper::kFrameBytes);
  ImageReceiver image(buf.data());
  for (uint8_t depth:{16,24}) for (size_t chunk:{1,20,244,512,65536}) {
    auto p=packet(240,240,depth,255,255,255);image.reset();
    for(size_t at=0;at<p.size();at+=chunk) {
      const size_t n=std::min(chunk,p.size()-at);
      auto result=image.feed(p.data()+at,n);
      assert(result==(at+n==p.size()?ImageReceiver::Result::Complete:ImageReceiver::Result::Pending));
    }
    assert(image.ready() && image.width()==240 && image.height()==240);
    epaper::convert(image,frame.data());for(auto b:frame)assert(b==0x55);
  }
  for (auto color:{std::vector<uint8_t>{0,0,0,0}, {255,230,0,0xAA}, {225,25,25,0xFF}}) {
    auto p=packet(1,1,24,color[0],color[1],color[2]);image.reset();
    assert(image.feed(p.data(),p.size())==ImageReceiver::Result::Complete);
    epaper::convert(image,frame.data());for(auto b:frame)assert(b==color[3]);
  }
  auto max=packet(512,512,24,0x21,0x49,0x18);image.reset();
  assert(image.feed(max.data(),max.size())==ImageReceiver::Result::Complete);
  epaper::convert(image,frame.data());
  auto corrupt=packet(128,160,16,255,0,0);corrupt.back()^=1;image.reset();
  assert(image.feed(corrupt.data(),corrupt.size())==ImageReceiver::Result::BadChecksum);
  assert(!image.ready());
  const uint8_t invalid[]={'!','I',24,0xFF,0xFF,0xFF,0xFF};
  assert(image.feed(invalid,sizeof(invalid))==ImageReceiver::Result::BadHeader);
  const uint8_t zero[]={'!','I',24,0,0,1,0};
  assert(image.feed(zero,sizeof(zero))==ImageReceiver::Result::BadHeader);
  const uint8_t badDepth[]={'!','I',8,1,0,1,0};
  assert(image.feed(badDepth,sizeof(badDepth))==ImageReceiver::Result::BadHeader);
  for (auto size:{std::vector<int>{4,4}, {128,160}, {512,1}, {1,512}}) {
    auto p=packet(size[0],size[1],24,90,180,220);image.reset();
    const uint8_t junk[]={0,1,'!','!'};image.feed(junk,sizeof(junk));
    assert(image.feed(p.data(),p.size())==ImageReceiver::Result::Complete);
    epaper::convert(image,frame.data());
  }
  // An incomplete/cancelled frame must not become displayable.
  image.reset(); auto p=packet(240,240,24,20,30,40);
  image.feed(p.data(),100); assert(image.active() && !image.ready());
  image.reset();assert(image.feed(p.data(),p.size())==ImageReceiver::Result::Complete);
  puts("PASS: RGB565/RGB888 chunking, CRC, size bounds, interrupted transfers, resize, palette packing (ASan/UBSan)");
}

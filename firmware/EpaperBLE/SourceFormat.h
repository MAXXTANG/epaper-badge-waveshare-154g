#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace epaper {
constexpr size_t kSourceHeaderBytes=12;
inline uint32_t crc32(const uint8_t* data,size_t size){
  uint32_t crc=0xFFFFFFFF;
  for(size_t i=0;i<size;i++){crc^=data[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xEDB88320u&uint32_t(-int32_t(crc&1)));}
  return ~crc;
}
inline void put32(uint8_t* p,uint32_t value){for(int i=0;i<4;i++)p[i]=value>>(8*i);}
inline uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline void sourceHeader(uint8_t* header,const uint8_t* data,size_t size){memcpy(header,"EPB3",4);put32(header+4,size);put32(header+8,crc32(data,size));}
inline bool validSource(const uint8_t* header,const uint8_t* data,size_t size){return !memcmp(header,"EPB3",4)&&get32(header+4)==size&&get32(header+8)==crc32(data,size);}
}

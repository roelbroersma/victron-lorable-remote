#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define LBR_HEADER_SIZE 256u
#define LBR_STM_MAX 0x31000u
#define LBR_ESP_MAX 0xa5000u
#define LBR_META_OFFSET 0xa5000u
#define LBR_ESP_APP_MAX 0xf0000u
#define LBR_STAGE_OFFSET 0xf0000u
#define LBR_STAGE_DATA (LBR_STAGE_OFFSET+0x1000u)
static inline uint32_t lbr_u32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline uint32_t lbr_crc(uint32_t crc,const uint8_t *p,size_t n){crc=~crc;while(n--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;}
static inline int lbr_header_valid(const uint8_t *h,size_t total){
 if(memcmp(h,"LBRUPD1\0",8)||lbr_u32(h+8)!=1||lbr_u32(h+12)!=11162||lbr_u32(h+60))return 0;
 uint32_t stm=lbr_u32(h+48),esp=lbr_u32(h+52),raw=lbr_u32(h+128);
 if(stm<256||stm>LBR_STM_MAX||stm%8||esp<120||esp>LBR_ESP_MAX||raw<288||raw>LBR_ESP_APP_MAX)return 0;
 if(total!=LBR_HEADER_SIZE+(size_t)stm+esp||lbr_crc(0,h,252)!=lbr_u32(h+252))return 0;
 unsigned i=16;while(i<48&&h[i]){if(!((h[i]>='0'&&h[i]<='9')||h[i]=='.'||h[i]=='-'||(h[i]>='a'&&h[i]<='z')))return 0;++i;}
 if(i==16||i==48)return 0;
 for(;i<48;i++)if(h[i])return 0;
 for(i=164;i<252;i++)if(h[i])return 0;
 return 1;
}

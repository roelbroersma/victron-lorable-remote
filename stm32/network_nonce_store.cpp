#include "network_nonce_store.h"
#include <Arduino.h>
#include <board.h>
#include <string.h>
namespace {
struct Entry {uint8_t dev[8],join[8],key[16];uint32_t nonce;};
struct Record {uint32_t magic,generation,count;Entry entries[8];uint32_t crc;};
static const uint32_t A=0x4800,B=0x5000,MAGIC=0x31434e4c;
static_assert(sizeof(Record)<=0x800,"Nonce record exceeds flash page");
uint32_t checksum(const Record &r){uint32_t c=~0UL;const uint8_t *p=(const uint8_t *)&r;
 for(unsigned i=0;i<sizeof(r)-4;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xEDB88320UL&(uint32_t)-(int32_t)(c&1));}return ~c;}
bool read(uint32_t offset,Record &r){return api.system.flash.get(offset,(uint8_t *)&r,sizeof(r))&&r.magic==MAGIC&&r.count<=8&&r.crc==checksum(r);}
bool isErased(const Record &r){const uint8_t *p=(const uint8_t *)&r;for(unsigned i=0;i<sizeof(r);++i)if(p[i]!=0xff)return false;return true;}
void wipe(Record &r){volatile uint8_t *p=(volatile uint8_t *)&r;for(unsigned i=0;i<sizeof(r);++i)p[i]=0;}
bool latest(Record &r,uint32_t &offset){Record other={};bool a=read(A,r),b=read(B,other);
 const bool erased=isErased(r)&&isErased(other);
 if(b&&(!a||(int32_t)(other.generation-r.generation)>0)){r=other;offset=B;}else offset=A;
 wipe(other);if(!a&&!b){memset(&r,0,sizeof(r));r.magic=MAGIC;offset=B;}return a||b||erased;}
int find(const Record &r,const uint8_t *dev,const uint8_t *join,const uint8_t *key){
 for(unsigned i=0;i<r.count;++i)if(!memcmp(r.entries[i].dev,dev,8)&&!memcmp(r.entries[i].join,join,8)&&!memcmp(r.entries[i].key,key,16))return i;return -1;}
}
bool networkNonceLoad(const uint8_t dev[8],const uint8_t join[8],const uint8_t key[16],uint32_t &nonce){
 Record r={};uint32_t offset;if(!latest(r,offset)){wipe(r);return false;}int i=find(r,dev,join,key);
 nonce=i<0?0:r.entries[i].nonce;bool ok=i>=0||r.count<8;wipe(r);return ok;
}
bool networkNonceSave(const uint8_t dev[8],const uint8_t join[8],const uint8_t key[16],uint32_t nonce){
 Record r={};uint32_t offset;if(!latest(r,offset)){wipe(r);return false;}int i=find(r,dev,join,key);
 if(i<0){if(r.count>=8){wipe(r);return false;}i=r.count++;memcpy(r.entries[i].dev,dev,8);memcpy(r.entries[i].join,join,8);memcpy(r.entries[i].key,key,16);}
 else if(r.entries[i].nonce==nonce){wipe(r);return true;}
 r.entries[i].nonce=nonce;++r.generation;r.crc=checksum(r);offset=offset==A?B:A;
 Record verify={};bool ok=api.system.flash.set(offset,(uint8_t *)&r,sizeof(r))&&read(offset,verify)&&!memcmp(&r,&verify,sizeof(r));wipe(r);wipe(verify);return ok;
}

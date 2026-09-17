#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static const unsigned MAX_BLE_FUNCTIONS=10;
// Binary UUIDs/payloads avoid retaining 86 redundant ASCII bytes per function.
struct BleFunction {
    char name[25];
    uint8_t service[16],characteristic[16],value[20],valueLength,kind;
};
static_assert(sizeof(BleFunction)==79,"Function layout must stay compact");
static inline int functionNibble(char c) {
    return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
}
static inline bool functionParseHex(const char *text,uint8_t *out,size_t capacity,size_t &used,bool uuid=false) {
    used=0;int high=-1;
    for(size_t i=0;text[i];++i) {
        if(uuid && (i==8||i==13||i==18||i==23)) {if(text[i]!='-') return false;continue;}
        int v=functionNibble(text[i]);if(v<0)return false;
        if(high<0)high=v;else{if(used>=capacity)return false;out[used++]=(uint8_t)((high<<4)|v);high=-1;}
    }
    return high<0 && (!uuid || !text[0] || (strlen(text)==36 && used==16));
}
static inline void functionHex(const uint8_t *in,size_t n,char *out,bool uuid=false) {
    static const char hex[]="0123456789abcdef";size_t pos=0;
    for(size_t i=0;i<n;++i){if(uuid&&(i==4||i==6||i==8||i==10))out[pos++]='-';out[pos++]=hex[in[i]>>4];out[pos++]=hex[in[i]&15];}
    out[pos]=0;
}
static inline bool functionSetText(BleFunction &f,const char *service,const char *characteristic,const char *value) {
    size_t n;
    memset(f.service,0,16);memset(f.characteristic,0,16);memset(f.value,0,20);
    if(!functionParseHex(service,f.service,16,n,true)||!functionParseHex(characteristic,f.characteristic,16,n,true)||
       !functionParseHex(value,f.value,20,n))return false;
    f.valueLength=(uint8_t)n;return true;
}

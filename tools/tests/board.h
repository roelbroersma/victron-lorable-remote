#pragma once
// Host-only emulation of RUI user flash. Never linked into firmware.
#include "Arduino.h"
#include <algorithm>
struct FakeFlash {
 uint8_t bytes[0x6800];unsigned writes=0;int tear=-1;
 FakeFlash(){memset(bytes,0xff,sizeof(bytes));}
 bool get(uint32_t off,uint8_t *out,size_t n){if(off+n>sizeof(bytes))return false;memcpy(out,bytes+off,n);return true;}
 bool set(uint32_t off,uint8_t *in,size_t n){if(off+n>sizeof(bytes))return false;++writes;memset(bytes+(off&~0x7ff),0xff,0x800);size_t copied=tear<0?n:std::min(n,(size_t)tear);memcpy(bytes+off,in,copied);return tear<0;}
};
struct FakeSystem{FakeFlash flash;};
struct FakeApi{FakeSystem system;};
extern FakeApi api;

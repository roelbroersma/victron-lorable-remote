#define LORABLE_PUBLIC_BUILD 1
#include "board.h"
#include <assert.h>
#include <vector>
#include "../../stm32/config_store.cpp"
#include "../../esp8684/main/victron_frames.h"
FakeApi api;

static void frameTests(){
 uint8_t value[32];size_t n=0;
 const uint8_t mode[]={8,0,0x19,2,0,0x41,3};
 assert(victron_find_value(mode,sizeof(mode),0,0x0200,value,sizeof(value),&n)&&n==1&&value[0]==3);
 assert(!victron_find_value(mode,sizeof(mode),3,0x0200,value,sizeof(value),&n));
 for(size_t i=0;i<sizeof(mode);i++)assert(!victron_find_value(mode,i,0,0x0200,value,sizeof(value),&n));
 const uint8_t combined[]={2,0x9f,0,0,0xff,7,0,3,0,8,0,0x19,0xed,0xa8,0x41,3,8,0,0x19,2,0,0x41,4};
 assert(victron_find_value(combined,sizeof(combined),0,0x0200,value,sizeof(value),&n)&&value[0]==4);
 assert(victron_find_value(combined,sizeof(combined),0,0xeda8,value,sizeof(value),&n)&&value[0]==3);
 std::vector<uint8_t> malformed(mode,mode+sizeof(mode));malformed.push_back(0xff);
 assert(!victron_find_value(malformed.data(),malformed.size(),0,0x0200,value,sizeof(value),&n));
 const uint8_t falsePositive[]={8,0,0x19,1,0,0x47,8,0,0x19,2,0,0x41,3};
 assert(!victron_find_value(falsePositive,sizeof(falsePositive),0,0x0200,value,sizeof(value),&n));
 puts("PASS: bounded CBOR parser, all truncations, malformed tail, wrong instance, no byte-pattern false positives");
}
static void migrationTests(){
 RuntimeConfig c;runtimeConfigDefaults(c);
 uint8_t wire[RUNTIME_CONFIG_WIRE_SIZE];runtimeConfigEncode(c,wire);
 StoredConfigV6 old={};old.header={CONFIG_MAGIC,6,532,17};memcpy(old.payload,wire,166);
 for(unsigned i=0;i<2;i++){
  uint8_t *p=old.payload+166+i*165;memcpy(p,c.bleFunctions[i].name,25);p[164]=c.bleFunctions[i].kind;
 }
 old.crc=crc32((const uint8_t *)&old,sizeof(old)-4);
 api.system.flash.set(CONFIG_SLOT_A_OFFSET,(uint8_t *)&old,sizeof(old));
 RuntimeConfig loaded;assert(runtimeConfigLoad(loaded));assert(runtimeConfigRevision()==17);
 assert(loaded.functionCount==2&&loaded.risingFunction==1&&loaded.fallingFunction==0&&loaded.downlinkFunctions==3);
 assert(loaded.bleFunctions[0].kind==1&&loaded.bleFunctions[1].kind==2);
 assert(runtimeConfigSave(loaded));assert(runtimeConfigRevision()==18);
 unsigned writes=api.system.flash.writes;assert(runtimeConfigSave(loaded));assert(writes==api.system.flash.writes);
 assert(runtimeConfigLoad(loaded)&&runtimeConfigRevision()==18);
 // A torn write erases only the alternate page; the latest committed record survives.
 api.system.flash.tear=31;loaded.statusIntervalMinutes=27;assert(!runtimeConfigSave(loaded));
 assert(runtimeConfigLoad(loaded)&&loaded.statusIntervalMinutes==15&&runtimeConfigRevision()==18);api.system.flash.tear=-1;
 puts("PASS: legacy v6 migration, A/B persistence, unchanged save, torn-write recovery");
}
static void tenFunctionTests(){
 RuntimeConfig c,d;runtimeConfigDefaults(c);c.deviceProfile=2;c.functionCount=10;c.risingFunction=10;c.fallingFunction=9;c.downlinkFunctions=1023;
 for(unsigned i=0;i<10;i++){
  auto &f=c.bleFunctions[i];f.kind=4;
  assert(functionSetText(f,"12345678-1234-1234-1234-123456789abc","abcdef01-1234-1234-1234-123456789abc","0123456789abcdef0123456789abcdef01234567"));
 }
 assert(runtimeConfigValid(c));uint8_t wire[RUNTIME_CONFIG_WIRE_SIZE],again[RUNTIME_CONFIG_WIRE_SIZE];
 runtimeConfigEncode(c,wire);assert(runtimeConfigDecode(wire,sizeof(wire),d));runtimeConfigEncode(d,again);assert(!memcmp(wire,again,sizeof(wire)));
 assert(runtimeConfigSave(c));assert(runtimeConfigLoad(d));assert(d.risingFunction==10&&d.bleFunctions[9].valueLength==20&&d.downlinkFunctions==1023);
 unsigned writes=api.system.flash.writes;assert(runtimeConfigSave(c));assert(writes==api.system.flash.writes);
 c.functionCount=9;assert(!runtimeConfigValid(c));c.functionCount=10;c.bleFunctions[9].valueLength=21;assert(!runtimeConfigValid(c));
 runtimeConfigDefaults(c);c.deviceProfile=3;c.victronDeviceInstance=0;c.bleFunctions[0].kind=11;c.bleFunctions[1].kind=12;assert(runtimeConfigValid(c));
 c.victronUseSmpPin=0;assert(!runtimeConfigValid(c));c.victronUseSmpPin=1;c.bleFunctions[0].kind=1;assert(!runtimeConfigValid(c));
 runtimeConfigDefaults(c);uint8_t dev[8]={1},join[8]={0},key[16]={1};
 assert(runtimeConfigStageLorawanUpdate(c,dev,join,key));PendingLorawanCredentials pending;
 assert(runtimeConfigPendingLorawanUpdate(pending)&&pending.joinEui[0]==0);
 assert(runtimeConfigCommitLorawanUpdate(c));
 puts("PASS: ten full GATT functions, exact roundtrip, stable function IDs, bounds, profile isolation, TTN zero JoinEUI");
}
int main(){frameTests();migrationTests();tenFunctionTests();}

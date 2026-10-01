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
static void settings4120Tests(){
 RuntimeConfig c,d;runtimeConfigDefaults(c);assert(c.loraTxDbm==14&&c.wifiMode==0);
 assert(c.victronDeviceInstance==3&&c.wifiStaDelaySeconds==90&&c.wifiStaTimeoutSeconds==60);
 c.loraTxDbm=0;c.wifiMode=1;strcpy(c.wifiStaSsid,"Boat router");strcpy(c.wifiStaPassword,"a password with spaces");
 c.wifiStaDelaySeconds=0;c.wifiStaTimeoutSeconds=300;
 c.edgeHoldSeconds[0]=5;c.edgeDelaySeconds[0]=10;c.edgeHoldSeconds[1]=3600;c.edgeDelaySeconds[1]=3600;
 uint8_t wire[RUNTIME_CONFIG_WIRE_SIZE],again[RUNTIME_CONFIG_WIRE_SIZE];
 runtimeConfigEncode(c,wire);assert(runtimeConfigDecode(wire,sizeof(wire),d));
 runtimeConfigEncode(d,again);assert(!memcmp(wire,again,sizeof(wire)));
 assert(runtimeConfigSave(c)&&runtimeConfigLoad(d));assert(d.loraTxDbm==0&&d.wifiMode==1&&d.edgeHoldSeconds[0]==5);
 assert(!strcmp(d.wifiStaPassword,c.wifiStaPassword)&&d.victronDeviceInstance==3);
 unsigned writes=api.system.flash.writes;assert(runtimeConfigSave(d)&&api.system.flash.writes==writes);
 c.loraTxDbm=22;assert(runtimeConfigValid(c));c.loraTxDbm=23;assert(!runtimeConfigValid(c));c=d;
 c.edgeDelaySeconds[0]=3601;assert(!runtimeConfigValid(c));c=d;
 c.wifiStaPassword[0]=0;assert(!runtimeConfigValid(c));c.wifiMode=0;assert(runtimeConfigValid(c));c=d;
 memset(c.wifiStaSsid,'x',sizeof(c.wifiStaSsid));assert(!runtimeConfigValid(c));c=d;
 c.wifiStaTimeoutSeconds=0;assert(!runtimeConfigValid(c));c=d;
 runtimeConfigDefaults(c);runtimeConfigEncode(c,wire);
 const uint16_t sizes[]={100,100,133,147,155,496,961,1212};
 const uint16_t payloads[]={100,136,168,184,188,532,997,1248};
 for(unsigned ver=1;ver<=8;++ver){
  if(ver==6)continue; // Historical text-GATT v6 fixture is covered above.
  api.system.flash=FakeFlash();const size_t n=sizeof(StoredHeader)+payloads[ver-1]+4;
  std::vector<uint8_t> record(n,0);StoredHeader h={CONFIG_MAGIC,(uint16_t)ver,payloads[ver-1],41};
  memcpy(record.data(),&h,sizeof(h));memcpy(record.data()+sizeof(h),wire,sizes[ver-1]);
  if(ver>1){auto p=record.data()+sizeof(h)+sizes[ver-1];p[0]=1;p[1]=1;p[17]=2;}
  putU32Le(record.data()+n-4,crc32(record.data(),n-4));
  assert(api.system.flash.set(CONFIG_SLOT_A_OFFSET,record.data(),record.size()));
  assert(runtimeConfigLoad(d)&&runtimeConfigRevision()==41);
  assert(d.loraTxDbm==14&&d.wifiMode==0&&d.edgeHoldSeconds[0]==0&&d.wifiStaDelaySeconds==90);
  PendingLorawanCredentials pending;assert(runtimeConfigPendingLorawanUpdate(pending)==(ver>1));
  assert(runtimeConfigSave(d)&&runtimeConfigLoad(d)&&runtimeConfigRevision()==42);
 }
 puts("PASS: 4.12 settings roundtrip/bounds, exact zero power, stable SmartSolar index, WiFi secret, v1-v8 migration and pending keys");
}
static void batteryProtectProfileTests(){
 for(unsigned profile=3;profile<=4;++profile){
  RuntimeConfig c,d;runtimeConfigDefaults(c);c.deviceProfile=profile;c.victronDeviceInstance=5;
  c.functionCount=10;c.risingFunction=10;c.fallingFunction=9;c.downlinkFunctions=1023;
  for(unsigned i=0;i<10;++i)c.bleFunctions[i].kind=i%2?12:11;
  assert(runtimeConfigValid(c));uint8_t wire[RUNTIME_CONFIG_WIRE_SIZE];runtimeConfigEncode(c,wire);
  assert(runtimeConfigDecode(wire,sizeof(wire),d)&&d.deviceProfile==profile&&d.victronDeviceInstance==5);
  assert(runtimeConfigSave(c)&&runtimeConfigLoad(d)&&d.deviceProfile==profile&&d.bleFunctions[9].kind==12);
  unsigned writes=api.system.flash.writes;assert(runtimeConfigSave(d)&&api.system.flash.writes==writes);
  c.victronUseSmpPin=0;assert(!runtimeConfigValid(c));c=d;
  for(uint8_t kind=1;kind<=10;++kind){c.bleFunctions[0].kind=kind;assert(!runtimeConfigValid(c));}
  c=d;c.deviceProfile=5;assert(!runtimeConfigValid(c));
 }
 assert(batteryProtectProductId(3)==0xa3b1&&batteryProtectProductId(4)==0xa3b3&&batteryProtectProductId(1)==0);
 puts("PASS: A3B1/A3B3 profile persistence, ten routed functions, no-op flash save, pairing and kind isolation");
}
int main(){frameTests();migrationTests();tenFunctionTests();settings4120Tests();batteryProtectProfileTests();}

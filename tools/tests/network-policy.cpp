#define LORABLE_PUBLIC_BUILD 1
#include "board.h"
#include <assert.h>
#include "../../stm32/config_store.cpp"
#include "../../stm32/network_policy.h"
#include "../../stm32/nonce_reservation.h"
#include "../../stm32/network_nonce_store.cpp"
FakeApi api;
static RuntimeConfig make(){RuntimeConfig c;runtimeConfigDefaults(c);c.networksInitialized=1;
 for(unsigned i=0;i<4;++i){auto &p=c.networks[i];p.enabled=1;p.joinEui[0]=i;p.appKey[0]=i+1;p.preemptMinutes=15;}return c;}
static void scheduling(){auto c=make();NetworkPolicy s;
 s.begin(c.networks,c.networkOrder,0);assert(s.slot==0&&s.ready(c.networks,0));s.started(0);s.failed(c.networks,c.networkOrder,8);
 assert(s.slot==1&&s.rank==1);assert(!s.ready(c.networks,67));assert(s.ready(c.networks,68));s.started(68);s.failed(c.networks,c.networkOrder,76);
 assert(s.slot==1&&s.nextAt==376);s.tick(c.networks,c.networkOrder,907,false);assert(s.slot==1);
 s.tick(c.networks,c.networkOrder,908,true);assert(s.slot==1);s.tick(c.networks,c.networkOrder,908,false);
 assert(s.slot==0&&s.probing);s.started(908);s.failed(c.networks,c.networkOrder,916);assert(s.slot==2&&!s.probing);
 s.started(976);s.joined(984);assert(s.state==NetworkPolicy::ONLINE);
 s.tick(c.networks,c.networkOrder,1816,false);assert(s.slot==0&&s.probing);
 s.started(1816);s.failed(c.networks,c.networkOrder,1824);assert(s.slot==1&&s.probing);
 s.started(1884);s.failed(c.networks,c.networkOrder,1892);assert(s.slot==2&&!s.probing); // return to previously working backup
 s.started(1952);s.joined(1960);s.tick(c.networks,c.networkOrder,2792,false);assert(s.slot==0);s.started(2792);s.joined(2800);
 s.tick(c.networks,c.networkOrder,86400,false);assert(s.slot==0&&s.state==NetworkPolicy::ONLINE); // no preemption of preferred
 c.networkOrder[0]=3;c.networkOrder[3]=0;c.networks[3].enabled=0;s.begin(c.networks,c.networkOrder,10);assert(s.slot==1);
 for(auto &p:c.networks)p.enabled=0;s.begin(c.networks,c.networkOrder,0);assert(s.slot==255&&s.state==NetworkPolicy::DISABLED);
 puts("PASS: priority, retry residence, preemption, healthy backup return, lower backup progress, disabled profiles");
}
static void budgetsAndWrap(){auto c=make();c.networks[0].kind=1;NetworkPolicy s;
 assert(nextNonceReservation(0)==16&&nextNonceReservation(123)==139);
 assert(nextNonceReservation(65518)==65534&&nextNonceReservation(65519)==65535&&nextNonceReservation(65535)==65535);
 s.begin(c.networks,c.networkOrder,0);s.ready(c.networks,0);s.started(0);s.joined(8);
 assert(!s.healthDue(c.networks,8+14399,15));assert(s.healthDue(c.networks,8+14400,15));assert(!s.healthDue(c.networks,999999,0));
 s.health(false,14408);assert(s.failures==1);s.health(true,28808);assert(!s.failures);s.health(false,43208);s.health(false,57608);assert(s.failures==2);
 s.state=NetworkPolicy::WAITING;s.nextAt=100;s.joinsToday[0]=6;
 assert(!s.ready(c.networks,100)&&s.state==NetworkPolicy::BUDGET_WAIT);assert(!s.ready(c.networks,86399));assert(s.ready(c.networks,86400));
 s.begin(c.networks,c.networkOrder,0);assert(s.ready(c.networks,0));s.started(0);s.failed(c.networks,c.networkOrder,8);
 s.tick(c.networks,c.networkOrder,908,false);assert(s.slot==0&&s.probing);
 assert(!s.ready(c.networks,908)&&s.state==NetworkPolicy::BUDGET_WAIT&&s.nextAt==3600);
 s.select(c.networks,c.networkOrder,0,1000);assert(!s.ready(c.networks,1000));assert(s.ready(c.networks,3600));
 s.begin(c.networks,c.networkOrder,0xfffffff0);s.started(0xfffffff0);s.select(c.networks,c.networkOrder,0,0x10);
 assert(!s.ready(c.networks,0x10));assert(s.ready(c.networks,3584));
 s.begin(c.networks,c.networkOrder,0xfffffff0);s.started(0xfffffff0);s.failed(c.networks,c.networkOrder,0xfffffff8);assert(s.slot==1);
 assert(!s.ready(c.networks,51));assert(s.ready(c.networks,52));s.tick(c.networks,c.networkOrder,892,false);assert(s.slot==0);
 puts("PASS: TTN join budget and cross-switch cooldown, minimum health interval, ACK misses, 32-bit rollover");
}
static void storage(){auto c=make();assert(runtimeConfigValid(c));uint8_t wire[RUNTIME_CONFIG_WIRE_SIZE];runtimeConfigEncode(c,wire);
 RuntimeConfig r;assert(runtimeConfigDecode(wire,sizeof(wire),r));assert(r.networks[3].appKey[0]==4);
 assert(runtimeConfigSave(c));unsigned writes=api.system.flash.writes;assert(runtimeConfigSave(c)&&api.system.flash.writes==writes);
 std::swap(c.networkOrder[0],c.networkOrder[1]);assert(runtimeConfigSave(c));assert(runtimeConfigLoad(r));assert(r.networkOrder[0]==1&&r.networks[0].appKey[0]==1&&r.networks[1].appKey[0]==2);
 c.networkOrder[0]=c.networkOrder[1];assert(!runtimeConfigValid(c));c=make();c.networks[1].joinEui[0]=0;assert(!runtimeConfigValid(c));
 c=make();c.networks[0].preemptMinutes=14;assert(!runtimeConfigValid(c));c=make();memset(c.networks[1].appKey,0,16);assert(!runtimeConfigValid(c));
 // A v4.9 record migrates without interpreting old pending bytes as profiles.
 c=make();runtimeConfigEncode(c,wire);StoredConfigV7 old={};old.header={CONFIG_MAGIC,7,997,99};memcpy(old.payload,wire,961);old.crc=crc32((const uint8_t *)&old,sizeof(old)-4);
 api.system.flash.set(CONFIG_SLOT_A_OFFSET,(uint8_t *)&old,sizeof(old));assert(runtimeConfigLoad(r)&&!r.networksInitialized&&r.functionCount==2);
 puts("PASS: four keys, stable slot reorder, validation, unchanged save, v7/v4.9 migration");
}
static void nonceStorage(){uint8_t dev[8]={1},join[8]={2},key[16]={3};uint32_t nonce;
 assert(networkNonceLoad(dev,join,key,nonce)&&nonce==0);assert(networkNonceSave(dev,join,key,18));assert(networkNonceLoad(dev,join,key,nonce)&&nonce==18);
 unsigned writes=api.system.flash.writes;assert(networkNonceSave(dev,join,key,18)&&api.system.flash.writes==writes);
 join[0]=4;assert(networkNonceLoad(dev,join,key,nonce)&&nonce==0);assert(networkNonceSave(dev,join,key,3));join[0]=2;assert(networkNonceLoad(dev,join,key,nonce)&&nonce==18);
 api.system.flash.tear=22;assert(!networkNonceSave(dev,join,key,19));api.system.flash.tear=-1;assert(networkNonceLoad(dev,join,key,nonce)&&nonce==18);
 for(unsigned i=0;i<6;++i){join[0]=10+i;assert(networkNonceSave(dev,join,key,i+20));}
 join[0]=99;assert(!networkNonceLoad(dev,join,key,nonce));assert(!networkNonceSave(dev,join,key,1));
 join[0]=2;assert(networkNonceLoad(dev,join,key,nonce)&&nonce==18);
 api.system.flash.bytes[0x4800]=0;api.system.flash.bytes[0x5000]=0;assert(!networkNonceLoad(dev,join,key,nonce));
 memset(api.system.flash.bytes+0x4800,0xff,4);memset(api.system.flash.bytes+0x5000,0xff,4);
 assert(!networkNonceLoad(dev,join,key,nonce)); // erased magic is not an erased ledger
 puts("PASS: independent network JoinNonces, no settings-import rollback, unchanged save, torn-write recovery, corruption fails closed");
}
int main(){scheduling();budgetsAndWrap();storage();nonceStorage();}

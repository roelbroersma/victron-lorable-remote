// Execute the actual integration code with a bounded RUI/LoRaMAC test double.
// Hardware timing and the vendor stack still require separate physical tests.
#define LORABLE_PUBLIC_BUILD 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
class String;
#include "../../stm32/network_manager.cpp"
ManagerFakeApi api;
static uint32_t nowSeconds;
static LoRaMacNvmData_t radioContext;
static uint16_t persistentNonce;
static unsigned reserveWrites,joinCalls,ledgerWrites;
static uint32_t savedJoinNonce[4];
static uint8_t chosenJoin,appRoot,nwkRoot;
static bool radioBusy,failReserve,failLedger;
uint32_t activitySeconds(){return nowSeconds;}
void activityAdd(uint8_t,uint32_t){}
bool networkNonceLoad(const uint8_t*,const uint8_t*join,const uint8_t*,uint32_t &value){value=savedJoinNonce[join[0]];return true;}
bool networkNonceSave(const uint8_t*,const uint8_t*join,const uint8_t*,uint32_t value){
 if(failLedger)return false;savedJoinNonce[join[0]]=value;++ledgerWrites;return true;
}
extern "C" {
uint16_t service_lora_get_DevNonce(){return persistentNonce;}
int32_t service_lora_set_DevNonce(uint16_t value){if(failReserve)return -1;persistentNonce=value;++reserveWrites;return 0;}
bool LoRaMacIsBusy(){return radioBusy;}
LoRaMacStatus_t LoRaMacMibGetRequestConfirm(MibRequestConfirm_t *m){assert(m->Type==MIB_NVM_CTXS);m->Param.Contexts=&radioContext;return LORAMAC_STATUS_OK;}
LoRaMacStatus_t LoRaMacMibSetRequestConfirm(MibRequestConfirm_t *m){
 if(m->Type==MIB_NETWORK_ACTIVATION)api.lorawan.njs.value=0;
 if(m->Type==MIB_JOIN_EUI)chosenJoin=m->Param.JoinEui[0];
 if(m->Type==MIB_APP_KEY)appRoot=m->Param.AppKey[0];
 if(m->Type==MIB_NWK_KEY)nwkRoot=m->Param.NwkKey[0];
 if(m->Type==MIB_RX2_DEFAULT_CHANNEL||m->Type==MIB_RXC_DEFAULT_CHANNEL)assert(m->Param.Rx2DefaultChannel.Frequency==869525000);
 return LORAMAC_STATUS_OK;
}
LoRaMacStatus_t LoRaMacMlmeRequest(MlmeReq_t *m){
 assert(m->Type==MLME_JOIN&&m->Req.Join.NetworkActivation==ACTIVATION_TYPE_OTAA);
 assert(appRoot==nwkRoot&&appRoot==chosenJoin+1);
 assert(persistentNonce>radioContext.Crypto.DevNonce); // reservation precedes RF
 ++radioContext.Crypto.DevNonce;++joinCalls;return LORAMAC_STATUS_OK;
}
}
static RuntimeConfig configuration(){RuntimeConfig c={};c.loraRegion=4;c.networkHealthMinutes=15;c.statusIntervalMinutes=15;
 for(unsigned i=0;i<4;++i){c.networkOrder[i]=i;c.networks[i].joinEui[0]=i;c.networks[i].appKey[0]=i+1;c.networks[i].preemptMinutes=15;}
 c.networks[0].enabled=c.networks[1].enabled=1;return c;
}
static void boot(RuntimeConfig &c,uint16_t nonce){
 api=ManagerFakeApi();radioContext={};radioBusy=failReserve=failLedger=false;
 nowSeconds=0;persistentNonce=nonce;radioContext.Crypto.DevNonce=nonce;
 reserveWrites=joinCalls=ledgerWrites=0;uint8_t dev[8]={42};networkBegin(c,dev);
 assert(api.lorawan.autoJoinDisabled);
}
static void tick(uint32_t time){nowSeconds=time;networkTick(false);}
static void complete(uint32_t time,bool ok,uint32_t nonce=0){
 nowSeconds=time;if(ok){api.lorawan.njs.value=1;radioContext.Crypto.JoinNonce=nonce;}
 networkJoinResult(ok?0:-1);networkTick(false);
}
int main(){
 auto c=configuration();savedJoinNonce[0]=51;savedJoinNonce[1]=7;boot(c,100);
 tick(0);assert(joinCalls==1&&reserveWrites==1&&persistentNonce==116&&radioContext.Crypto.DevNonce==101);
 assert(radioContext.Crypto.JoinNonce==51&&!networkJoined());complete(8,false);
 tick(67);assert(joinCalls==1);tick(68);assert(joinCalls==2&&chosenJoin==1&&reserveWrites==1);
 assert(radioContext.Crypto.JoinNonce==7);complete(76,true,8);
 assert(networkJoined()&&networkActiveSlot()==1&&savedJoinNonce[0]==51&&savedJoinNonce[1]==8);
 tick(908);assert(joinCalls==3&&chosenJoin==0&&!networkJoined()&&radioContext.Crypto.JoinNonce==51);
 complete(916,false);tick(976);assert(joinCalls==4&&chosenJoin==1&&radioContext.Crypto.JoinNonce==8);
 complete(984,true,9);assert(networkJoined());assert(persistentNonce==116&&reserveWrites==1);
 // Reboot skips unused values rather than repeating a previously consumed nonce.
 boot(c,116);tick(0);assert(radioContext.Crypto.DevNonce==117&&persistentNonce==132);
 // A single private profile reserves once per 16 attempts, not every failure.
 c.networks[1].enabled=0;boot(c,200);
 for(unsigned i=0;i<17;++i){tick(i*308);complete(i*308+8,false);}
 assert(joinCalls==17&&reserveWrites==2&&persistentNonce==232&&radioContext.Crypto.DevNonce==217);
 // No transmission without durable reservation, no accepted session without ledger.
 boot(c,300);failReserve=true;tick(0);assert(joinCalls==0&&networkState()==NetworkPolicy::FAULT);
 boot(c,65535);tick(0);assert(joinCalls==0&&networkState()==NetworkPolicy::FAULT);
 boot(c,400);tick(0);failLedger=true;complete(8,true,52);assert(!networkJoined()&&networkState()==NetworkPolicy::FAULT);
 // Real ACK result, not a local send-complete indication, controls health.
 boot(c,500);tick(0);complete(8,true,53);nowSeconds=908;assert(networkHealthDue());
 networkTxComplete(true,false);assert(networkMissedChecks()==1&&networkJoined());
 nowSeconds=1808;networkTxComplete(true,false);assert(!networkJoined()&&networkState()==NetworkPolicy::WAITING);
 c.networks[0].kind=1;boot(c,600);tick(0);complete(8,true,54);assert(networkStatusIntervalMinutes()==240);
 puts("PASS: actual manager integration, RAM key selection, per-network ledger, preempt/rejoin, reserved nonce blocks/reboot/fail-closed, ACK health, TTN status clamp");
}

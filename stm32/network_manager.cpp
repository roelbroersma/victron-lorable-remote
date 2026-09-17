#include "network_manager.h"
#include "network_policy.h"
#include "network_nonce_store.h"
#include "nonce_reservation.h"
#include "region_profile.h"
#include "activity_log.h"
#include <board.h>
extern "C" {
#include "LoRaMac.h"
#include "service_lora.h"
}
namespace {
NetworkPolicy policy;
const RuntimeConfig *settings;
uint8_t identity[8];
volatile bool resultPending;
volatile int32_t joinResult;
bool activated;
uint16_t reservedNonceEnd;

LoRaMacNvmData_t *contexts(){MibRequestConfirm_t m={};m.Type=MIB_NVM_CTXS;
 return LoRaMacMibGetRequestConfirm(&m)==LORAMAC_STATUS_OK?m.Param.Contexts:nullptr;}
bool prepare(const NetworkProfile &p){
    // No RUI persistent credential setters here: selection is RAM state, not a
    // rewrite of settings/keys on every fallback. The common radio region stays.
    MibRequestConfirm_t m={};m.Type=MIB_DEVICE_CLASS;m.Param.Class=CLASS_A;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    m.Type=MIB_NETWORK_ACTIVATION;m.Param.NetworkActivation=ACTIVATION_TYPE_NONE;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    m.Type=MIB_DEV_EUI;m.Param.DevEui=identity;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    m.Type=MIB_JOIN_EUI;m.Param.JoinEui=(uint8_t *)p.joinEui;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    m.Type=MIB_APP_KEY;m.Param.AppKey=(uint8_t *)p.appKey;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    m.Type=MIB_NWK_KEY;m.Param.NwkKey=(uint8_t *)p.appKey;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    const RegionProfile *region=regionProfile(settings->loraRegion);
    m.Type=MIB_RX2_DEFAULT_CHANNEL;
    m.Param.Rx2DefaultChannel.Frequency=p.rx2Custom?p.rx2Frequency:region->rx2Frequency;
    m.Param.Rx2DefaultChannel.Datarate=p.rx2Custom?p.rx2DataRate:region->rx2DataRate;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    m.Type=MIB_RXC_DEFAULT_CHANNEL;
    if(LoRaMacMibSetRequestConfirm(&m)!=LORAMAC_STATUS_OK)return false;
    LoRaMacNvmData_t *n=contexts();uint32_t nonce;
    if(!n||!networkNonceLoad(identity,p.joinEui,p.appKey,nonce)||n->Crypto.DevNonce==65535){policy.state=NetworkPolicy::FAULT;return false;}
    // Restore only this network's JoinNonce, never another server's counter.
    n->Crypto.JoinNonce=nonce;
    // Reserve BEFORE RF. Runtime switches/retries do not reset this global
    // high-water mark. One NVM write per 16 attempts, not one erase per retry.
    if(n->Crypto.DevNonce>=reservedNonceEnd){
        const uint16_t end=nextNonceReservation(n->Crypto.DevNonce);
        if(service_lora_set_DevNonce(end)!=0){policy.state=NetworkPolicy::FAULT;return false;}
        reservedNonceEnd=end;
    }
    return true;
}
}
void networkBegin(const RuntimeConfig &config,const uint8_t devEui[8]){
    settings=&config;memcpy(identity,devEui,8);resultPending=false;activated=false;
    // Disable RUI's autonomous timer; only the policy controls retries.
    api.lorawan.join(0,0,60,0);
    reservedNonceEnd=service_lora_get_DevNonce();
    policy.begin(settings->networks,settings->networkOrder,activitySeconds());
}
void networkJoinResult(int32_t result){joinResult=result;resultPending=true;}
void networkTick(bool txBusy){
    if(!settings)return;const uint32_t now=activitySeconds();
    if(resultPending){
        const int32_t result=joinResult;resultPending=false;
        if(policy.state==NetworkPolicy::JOINING){
            if(result==0){
                const NetworkProfile &p=settings->networks[policy.slot];LoRaMacNvmData_t *n=contexts();
                if(!n||!networkNonceSave(identity,p.joinEui,p.appKey,n->Crypto.JoinNonce)){
                    policy.state=NetworkPolicy::FAULT;activated=false;activityAdd(24,policy.slot+1);
                }else{policy.joined(now);activated=true;activityAdd(21,policy.slot+1);}
            }else{activated=false;activityAdd(22,policy.slot+1);policy.failed(settings->networks,settings->networkOrder,now);}
        }
    }
    const bool busy=txBusy||LoRaMacIsBusy();
    if(activated&&policy.state==NetworkPolicy::ONLINE&&!api.lorawan.njs.get()) {
        activated=false;policy.failed(settings->networks,settings->networkOrder,now);
    }
    if(policy.state==NetworkPolicy::JOINING&&!busy&&(int32_t)(now-policy.nextAt)>=0){
        activated=false;policy.failed(settings->networks,settings->networkOrder,now);
    }
    const uint8_t previous=policy.slot;const bool wasOnline=policy.state==NetworkPolicy::ONLINE;
    policy.tick(settings->networks,settings->networkOrder,now,busy);
    if(previous!=policy.slot||(wasOnline&&policy.state!=NetworkPolicy::ONLINE)){activated=false;activityAdd(23,policy.slot+1);}
    if(busy)return;
    if(!policy.ready(settings->networks,now)){
        if(policy.state==NetworkPolicy::BUDGET_WAIT&&(policy.probing||(policy.rank==policy.first(settings->networks,settings->networkOrder)&&
            policy.first(settings->networks,settings->networkOrder,policy.rank+1)!=255)))
            policy.failed(settings->networks,settings->networkOrder,now);
        return;
    }
    activated=false;
    if(!prepare(settings->networks[policy.slot])){if(policy.state!=NetworkPolicy::FAULT)policy.deferred(now);return;}
    policy.started(now);
    // Use the bundled RUI 4.2.4 LoRaMAC entry point: the RUI join wrapper would
    // replace our reserved high-water mark with the just-consumed value.
    // RUI's normal MLME callbacks, regional duty-cycle checks and radio remain.
    MlmeReq_t join={};join.Type=MLME_JOIN;join.Req.Join.NetworkActivation=ACTIVATION_TYPE_OTAA;
    join.Req.Join.Datarate=regionProfile(settings->loraRegion)->uplinkDataRate;
    if(LoRaMacMlmeRequest(&join)!=LORAMAC_STATUS_OK)policy.deferred(now);
    else activityAdd(20,policy.slot+1);
}
void networkTxComplete(bool healthProbe,bool acknowledged){
    if(!settings||!healthProbe||policy.state!=NetworkPolicy::ONLINE)return;
    policy.health(acknowledged,activitySeconds());activityAdd(acknowledged?25:26,policy.slot+1);
    if(policy.failures>=2){activated=false;policy.failed(settings->networks,settings->networkOrder,activitySeconds());}
}
void networkDownlinkReceived(){if(settings&&policy.state==NetworkPolicy::ONLINE)policy.health(true,activitySeconds());}
bool networkJoined(){return settings&&activated&&policy.state==NetworkPolicy::ONLINE&&api.lorawan.njs.get()!=0;}
bool networkHealthDue(){return settings&&policy.healthDue(settings->networks,activitySeconds(),settings->networkHealthMinutes);}
uint8_t networkActiveSlot(){return settings?policy.slot:255;}
uint8_t networkState(){return settings?policy.state:NetworkPolicy::DISABLED;}
uint8_t networkMissedChecks(){return policy.failures;}
uint32_t networkPreemptRemaining(){return settings?policy.remaining(settings->networks,settings->networkOrder,activitySeconds()):0;}
uint32_t networkRetryRemaining(){return (policy.state==NetworkPolicy::WAITING||policy.state==NetworkPolicy::BUDGET_WAIT)&&
    (int32_t)(policy.nextAt-activitySeconds())>0?policy.nextAt-activitySeconds():0;}
uint32_t networkStatusIntervalMinutes(){if(!settings)return 240;const uint32_t minutes=settings->statusIntervalMinutes;
 return policy.slot<MAX_NETWORKS&&settings->networks[policy.slot].kind==1&&minutes<240?240:minutes;}

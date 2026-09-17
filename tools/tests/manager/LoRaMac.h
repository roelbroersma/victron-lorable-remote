#pragma once
#include <stdint.h>
typedef enum {LORAMAC_STATUS_OK,LORAMAC_STATUS_ERROR} LoRaMacStatus_t;
enum {CLASS_A=0,ACTIVATION_TYPE_NONE=0,ACTIVATION_TYPE_OTAA=1,MLME_JOIN=0};
enum {MIB_NVM_CTXS,MIB_DEVICE_CLASS,MIB_NETWORK_ACTIVATION,MIB_DEV_EUI,
      MIB_JOIN_EUI,MIB_APP_KEY,MIB_NWK_KEY,MIB_RX2_DEFAULT_CHANNEL,MIB_RXC_DEFAULT_CHANNEL};
typedef struct {struct {uint16_t DevNonce;uint32_t JoinNonce;} Crypto;} LoRaMacNvmData_t;
typedef struct {int Type;union {
    LoRaMacNvmData_t *Contexts;int Class,NetworkActivation;
    uint8_t *DevEui,*JoinEui,*AppKey,*NwkKey;
    struct {uint32_t Frequency;uint8_t Datarate;} Rx2DefaultChannel;
} Param;} MibRequestConfirm_t;
typedef struct {int Type;struct {struct {uint8_t NetworkActivation,Datarate;} Join;} Req;} MlmeReq_t;
LoRaMacStatus_t LoRaMacMibGetRequestConfirm(MibRequestConfirm_t *m);
LoRaMacStatus_t LoRaMacMibSetRequestConfirm(MibRequestConfirm_t *m);
LoRaMacStatus_t LoRaMacMlmeRequest(MlmeReq_t *m);
bool LoRaMacIsBusy();

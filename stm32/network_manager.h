#pragma once
#include "config_store.h"
void networkBegin(const RuntimeConfig &config,const uint8_t devEui[8]);
void networkTick(bool transmissionInFlight);
void networkJoinResult(int32_t result);
void networkTxComplete(bool healthProbe,bool acknowledged);
void networkDownlinkReceived();
bool networkJoined();
bool networkHealthDue();
uint8_t networkActiveSlot();
uint8_t networkState();
uint8_t networkMissedChecks();
uint32_t networkPreemptRemaining();
uint32_t networkRetryRemaining();
uint32_t networkStatusIntervalMinutes();

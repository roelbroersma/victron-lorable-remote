#pragma once
#include <stdint.h>
#include <string.h>

static const unsigned MAX_NETWORKS = 4;
struct NetworkProfile {
    char name[25];
    uint8_t enabled, kind; // 0 private/custom, 1 TTN Sandbox (conservative retries)
    uint8_t joinEui[8], appKey[16];
    uint8_t rx2Custom, rx2DataRate;
    uint32_t rx2Frequency, preemptMinutes;
};
static const unsigned NETWORK_WIRE_SIZE = 61;
inline bool networkHasKey(const NetworkProfile &p) {
    uint8_t any=0;for(unsigned i=0;i<16;++i)any|=p.appKey[i];return any!=0;
}
// Slot identities never change when priority is moved. Keys are never returned
// by the HTTP config endpoint and blank password fields retain their own slot.


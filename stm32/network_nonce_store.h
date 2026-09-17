#pragma once
#include <stdint.h>
// Separate from settings/backups: importing old settings must not roll security
// counters backwards. Eight historical OTAA identities, on two independent pages.
bool networkNonceLoad(const uint8_t dev[8],const uint8_t join[8],const uint8_t key[16],uint32_t &nonce);
bool networkNonceSave(const uint8_t dev[8],const uint8_t join[8],const uint8_t key[16],uint32_t nonce);

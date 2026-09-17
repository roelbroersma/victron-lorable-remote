#pragma once
#include <stdint.h>
// Persist a high-water mark, then consume at most 16 nonces in RAM. A restart
// skips unused reservations, never reuses one. Do not reset on profile changes.
inline uint16_t nextNonceReservation(uint16_t current) {
    return current>65519?65535:(uint16_t)(current+16);
}

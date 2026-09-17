#pragma once
// Distinct kinds prevent an old MPPT action being reinterpreted after changing profile.
static inline unsigned char smartBatteryProtectMode(unsigned char kind) {
    return kind==11?3:kind==12?4:255;
}
#include <stdint.h>

// Persistent function kinds keep v4.7 values stable (1 ON, 2 OFF, 3/4 GATT).
// MPPT EDAB values: Victron BlueSolar/SmartSolar HEX protocol, section 5.2.
// This documents register semantics, NOT verification of our BLE transport.
#ifdef __cplusplus
#define LR_INLINE static constexpr
#else
#define LR_INLINE static inline
#endif
LR_INLINE uint8_t smartMpptMode(uint8_t kind) {
    // A single return expression also permits compile-time checks in RUI C++11.
    return kind==1 ? 4 : kind==2 ? 0 : kind==5 ? 1 : kind==6 ? 2 :
           kind==7 ? 3 : kind==8 ? 5 : kind==9 ? 6 : kind==10 ? 7 : 255;
}
LR_INLINE uint8_t genericGattKind(uint8_t kind) {
    return kind==3 || kind==4;
}
// EDAB upper bits include the streetlight timer. Never discard them.
LR_INLINE uint8_t smartMpptControl(uint8_t previous, uint8_t mode) {
    return (previous & 0xF0u) | (mode & 0x0Fu);
}
#ifdef __cplusplus
static_assert(smartMpptMode(1)==4 && smartMpptMode(2)==0 && smartMpptMode(5)==1 &&
              smartMpptMode(6)==2 && smartMpptMode(7)==3 && smartMpptMode(8)==5 &&
              smartMpptMode(9)==6 && smartMpptMode(10)==7, "MPPT mode mapping changed");
static_assert(smartMpptMode(0)==255 && smartMpptMode(3)==255 && smartMpptMode(4)==255,
              "Disabled/Generic functions must never become MPPT writes");
static_assert(smartMpptControl(0xA4,7)==0xA7 && smartMpptControl(0x84,0)==0x80,
              "Upper EDAB bits must be preserved");
#endif
#undef LR_INLINE

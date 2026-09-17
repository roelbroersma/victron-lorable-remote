#pragma once
#include <stdint.h>

// Defaults taken from the LoRaMac regional tables bundled with RUI 4.2.4.
// RAK11160(H): high-band profiles only. Hardware and antenna must match.
struct RegionProfile {
    const char *name;
    uint32_t rx2Frequency;
    uint8_t rx2DataRate;
    uint8_t uplinkDataRate;
    uint32_t minFrequency;
    uint32_t maxFrequency;
};

inline const RegionProfile *regionProfile(uint8_t region)
{
    static const RegionProfile profiles[] = {
        {"RU864", 869100000, 0, 0, 864000000, 870000000},
        {"IN865", 866550000, 2, 0, 865000000, 867000000},
        {"EU868", 869525000, 0, 0, 863000000, 870000000},
        {"US915", 923300000, 8, 0, 923300000, 927500000},
        {"AU915", 923300000, 8, 2, 923300000, 927500000},
        {"KR920", 921900000, 0, 0, 920900000, 923300000},
        {"AS923-1", 923200000, 2, 2, 915000000, 928000000},
        {"AS923-2", 921400000, 2, 2, 915000000, 928000000},
        {"AS923-3", 916600000, 2, 2, 915000000, 928000000},
        {"AS923-4", 917300000, 2, 2, 915000000, 928000000}
    };
    return region >= 2 && region <= 11 ? &profiles[region-2] : nullptr;
}

inline bool validRx2(uint8_t region, uint32_t frequency, uint8_t dr)
{
    const RegionProfile *p = regionProfile(region);
    if (!p || frequency < p->minFrequency || frequency > p->maxFrequency) return false;
    if (region == 5 || region == 6)
        return dr >= 8 && dr <= 13 && (frequency - 923300000UL) % 600000UL == 0;
    const uint8_t maxDr = (region == 4 || region == 2 || region >= 8) ? 7 : 5;
    return dr <= maxDr;
}

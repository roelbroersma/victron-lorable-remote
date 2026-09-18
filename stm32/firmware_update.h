#pragma once
#include <stdint.h>
bool firmwareUpdateReady();
void firmwareUpdateLaunch(uint32_t size,uint32_t crc,uint32_t mode);

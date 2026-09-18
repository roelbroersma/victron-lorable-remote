#include <board.h>
#include <udrv_serial.h>
#include "esp_companion.h"

// RUI's custom serial mode still scans every incoming byte for AT+BOOT/AT+ATM.
// A binary firmware image is data, never a request to change modes or enter DFU.
// Linker wrapping keeps the upstream SDK intact and restores normal recovery
// commands immediately outside the bounded USB exchange. No NVM writes.
extern "C" void __real_serial_fallback_handler(SERIAL_PORT port,uint8_t ch);
extern "C" void __attribute__((used)) __wrap_serial_fallback_handler(SERIAL_PORT port,uint8_t ch){
    if(port==DEFAULT_SERIAL_CONSOLE && companionUsbActive())return;
    __real_serial_fallback_handler(port,ch);
}

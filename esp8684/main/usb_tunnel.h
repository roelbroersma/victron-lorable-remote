#pragma once
#include <stdbool.h>
#include "protocol.h"
bool usb_tunnel_receive(const protocol_frame_t *frame);
bool usb_tunnel_busy(void);
void usb_tunnel_diagnostics(unsigned values[6]);

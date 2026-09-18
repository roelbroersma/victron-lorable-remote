#pragma once
#include <stdbool.h>
#include "esp_http_server.h"
#include "protocol.h"
esp_err_t bundle_upload(httpd_req_t *request);
esp_err_t bundle_status(httpd_req_t *request);
bool bundle_busy(void);
bool bundle_receive_frame(const protocol_frame_t *frame);
void bundle_update_start(void);

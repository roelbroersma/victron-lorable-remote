#include "victron_ble.h"
#include "bluetooth_modes.h"
#include "victron_frames.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

#define VIC_SCAN_MS               7000u
#define VIC_CONNECT_MS           20000u
#define VIC_SECURITY_WAIT_MS     15000u
#define VIC_GATT_WAIT_MS         12000u
#define VIC_WRITE_WAIT_MS         5000u
#define VIC_VALUE_WAIT_MS         3000u
#define VIC_DISCONNECT_WAIT_MS    3000u
#define VIC_ATTEMPT_DEADLINE_MS  60000u
#define VIC_NOTIFICATION_BYTES     512u

typedef struct {
    uint16_t def_handle;
    uint16_t val_handle;
    uint16_t end_handle;
    uint16_t cccd_handle;
    bool found;
} characteristic_slot_t;

typedef struct {
    SemaphoreHandle_t sync_done;
    SemaphoreHandle_t scan_done;
    SemaphoreHandle_t connect_done;
    SemaphoreHandle_t security_done;
    SemaphoreHandle_t gatt_done;
    SemaphoreHandle_t write_done;
    SemaphoreHandle_t value_done;
    SemaphoreHandle_t disconnect_done;

    uint8_t own_address_type;
    int sync_status;
    int connect_status;
    int security_status;
    int gatt_status;
    int write_status;
    bool stack_reset;
    bool connected;
    uint16_t connection_handle;

    uint8_t target_address[6];
    ble_addr_t peer_address;
    bool target_seen;
    uint32_t advertisements;

    uint16_t service_start;
    uint16_t service_end;
    characteristic_slot_t control;
    characteristic_slot_t last_data;
    characteristic_slot_t data;

    uint8_t instance;
    const victron_request_t *request;
    uint8_t read_value[32];size_t read_length;
    bool pin_available;
    uint32_t pin;

    uint8_t notification[VIC_NOTIFICATION_BYTES];
    size_t notification_length;
    bool notification_overflow;
    uint16_t awaited_register;
    uint8_t register_value[32];
    size_t register_length;
    unsigned received_chunks;
    bool load_value_available;
    uint8_t load_value;
    int64_t attempt_deadline_us;
} victron_context_t;

static victron_context_t context;
static portMUX_TYPE notification_lock = portMUX_INITIALIZER_UNLOCKED;

static void give_if_created(SemaphoreHandle_t semaphore)
{
    if (semaphore != NULL) {
        xSemaphoreGive(semaphore);
    }
}

static void signal_all_waiters(void)
{
    give_if_created(context.sync_done);
    give_if_created(context.scan_done);
    give_if_created(context.connect_done);
    give_if_created(context.security_done);
    give_if_created(context.gatt_done);
    give_if_created(context.write_done);
    give_if_created(context.value_done);
    give_if_created(context.disconnect_done);
}

static void drain(SemaphoreHandle_t semaphore)
{
    if (semaphore != NULL) {
        while (xSemaphoreTake(semaphore, 0) == pdTRUE) {
        }
    }
}

static bool take_before_attempt_deadline(SemaphoreHandle_t semaphore,
                                         uint32_t operation_maximum_ms)
{
    const int64_t remaining_us = context.attempt_deadline_us - esp_timer_get_time();
    if (semaphore == NULL || remaining_us <= 0) {
        return false;
    }
    uint32_t remaining_ms = (uint32_t)((remaining_us + 999) / 1000);
    if (remaining_ms > operation_maximum_ms) {
        remaining_ms = operation_maximum_ms;
    }
    TickType_t ticks = pdMS_TO_TICKS(remaining_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    return xSemaphoreTake(semaphore, ticks) == pdTRUE;
}

static bool delay_before_attempt_deadline(uint32_t delay_ms)
{
    const int64_t remaining_us = context.attempt_deadline_us - esp_timer_get_time();
    if (remaining_us < (int64_t)delay_ms * 1000) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return esp_timer_get_time() <= context.attempt_deadline_us;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool parse_mac(const char *text, uint8_t little_endian[6])
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    uint8_t network_order[6] = {0};
    size_t nibble = 0;
    bool any_nonzero = false;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == ':' || *cursor == '-' || *cursor == ' ') {
            continue;
        }
        const int value = hex_value(*cursor);
        if (value < 0 || nibble >= 12) {
            return false;
        }
        if ((nibble & 1u) == 0u) {
            network_order[nibble / 2] = (uint8_t)(value << 4);
        } else {
            network_order[nibble / 2] |= (uint8_t)value;
        }
        any_nonzero = any_nonzero || value != 0;
        ++nibble;
    }
    if (nibble != 12 || !any_nonzero) {
        return false;
    }
    for (size_t index = 0; index < 6; ++index) {
        little_endian[index] = network_order[5 - index];
    }
    return true;
}

static bool pin_to_number(const char pin[7], uint32_t *number)
{
    if (pin == NULL || number == NULL || strlen(pin) != 6) {
        return false;
    }
    uint32_t parsed = 0;
    for (size_t index = 0; index < 6; ++index) {
        if (!isdigit((unsigned char)pin[index])) {
            return false;
        }
        parsed = parsed * 10u + (uint32_t)(pin[index] - '0');
    }
    *number = parsed;
    return true;
}

bool victron_ble_request_valid(const victron_request_t *request)
{
    uint8_t address[6] = {0};
    uint32_t pin = 0;
    if (request == NULL || !parse_mac(request->mac, address) ||
        request->address_type < -1 || request->address_type > 1 ||
        request->instance > 23 ||
          request->desired_value > 7 ||
        (!request->generic_kind && !request->pairing) ||
        request->max_attempts < 1 || request->max_attempts > 3) {
        return false;
    }
    if (!request->generic_kind && request->driver!=1 && request->driver!=3) return false;
    if (request->driver==3 && (request->generic_kind || request->instance!=0 ||
        (request->desired_value!=3 && request->desired_value!=4))) return false;
    uint8_t address_or=0;
    for(unsigned i=0;i<6;++i)address_or|=address[i];
    if(!address_or) return false;
    if(request->generic_kind) {
        if((request->generic_kind!=3 && request->generic_kind!=4) ||
           !request->value_length || request->value_length>20 ||
           strlen(request->service_uuid)!=36 || strlen(request->characteristic_uuid)!=36) return false;
        ble_uuid_any_t uuid;
        if(ble_uuid_from_str(&uuid,request->service_uuid) || ble_uuid_from_str(&uuid,request->characteristic_uuid)) return false;
    }
    if (request->pairing && !pin_to_number(request->pin, &pin)) {
        return false;
    }
    memset(address, 0, sizeof(address));
    pin = 0;
    return true;
}

const char *victron_ble_result_name(victron_result_code_t code)
{
    switch (code) {
        case VICTRON_RESULT_OK: return "ok";
        case VICTRON_RESULT_BAD_SETTINGS: return "bad_settings";
        case VICTRON_RESULT_STACK_ERROR: return "stack_error";
        case VICTRON_RESULT_TARGET_NOT_FOUND: return "target_not_found";
        case VICTRON_RESULT_CONNECT_FAILED: return "connect_failed";
        case VICTRON_RESULT_GATT_NOT_FOUND: return "gatt_not_found";
        case VICTRON_RESULT_INITIAL_READ_FAILED: return "initial_read_failed";
        case VICTRON_RESULT_WRITE_FAILED: return "write_failed";
        case VICTRON_RESULT_VERIFY_FAILED: return "verify_failed";
        case VICTRON_RESULT_SECURITY_FAILED: return "security_failed";
        case VICTRON_RESULT_BUSY: return "busy";
        default: return "not_run";
    }
}

void victron_ble_clear_request(victron_request_t *request)
{
    if (request == NULL) {
        return;
    }
    volatile uint8_t *bytes = (volatile uint8_t *)request;
    for (size_t index = 0; index < sizeof(*request); ++index) {
        bytes[index] = 0;
    }
}

static bool uuid_prefix(const ble_uuid_t *uuid, const char *prefix)
{
    char text[BLE_UUID_STR_LEN] = {0};
    ble_uuid_to_str(uuid, text);
    const size_t length = strlen(prefix);
    for (size_t index = 0; index < length; ++index) {
        if (tolower((unsigned char)text[index]) !=
            tolower((unsigned char)prefix[index])) {
            return false;
        }
    }
    return true;
}

static void reset_notification_capture(void)
{
    portENTER_CRITICAL(&notification_lock);
    memset(context.notification, 0, sizeof(context.notification));
    context.notification_length = 0;
    context.load_value_available = false;
    context.load_value = VICTRON_VALUE_UNKNOWN;
    portEXIT_CRITICAL(&notification_lock);
    drain(context.value_done);
}

static void inspect_notification(uint16_t handle, struct os_mbuf *buffer)
{
    if (buffer == NULL || (handle!=context.last_data.val_handle && handle!=context.data.val_handle)) {
        return;
    }
    const uint16_t packet_length = OS_MBUF_PKTLEN(buffer);
    if (packet_length == 0) {
        return;
    }

    uint8_t chunk[VIC_NOTIFICATION_BYTES];
    bool found = false;
    const bool copied=packet_length<=sizeof(chunk) && os_mbuf_copydata(buffer,0,packet_length,chunk)==0;
    portENTER_CRITICAL(&notification_lock);
    ++context.received_chunks;
    if(!copied || context.notification_length+packet_length>sizeof(context.notification))
        context.notification_overflow=true;
    else if(!context.notification_overflow) {
        memcpy(context.notification+context.notification_length,chunk,packet_length);
        context.notification_length+=packet_length;
    }
    if(handle==context.last_data.val_handle) {
        if(!context.notification_overflow && context.awaited_register &&
           victron_find_value(context.notification,context.notification_length,context.instance,
               context.awaited_register,context.register_value,sizeof(context.register_value),&context.register_length)) {
            context.load_value=context.register_length==1?context.register_value[0]:VICTRON_VALUE_UNKNOWN;
            context.load_value_available=true;found=true;
        }
        context.notification_length=0;context.notification_overflow=false;
    }
    portEXIT_CRITICAL(&notification_lock);
    memset(chunk, 0, sizeof(chunk));
    if (found) {
        give_if_created(context.value_done);
    }
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            ++context.advertisements;
            if (memcmp(event->disc.addr.val, context.target_address, 6) == 0) {
                context.target_seen = true;
                context.peer_address = event->disc.addr;
                // NimBLE cancellation completes synchronously and does not
                // deliver DISC_COMPLETE. Wake the connecting task ourselves.
                if(ble_gap_disc_cancel()==0) give_if_created(context.scan_done);
            }
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            give_if_created(context.scan_done);
            return 0;

        case BLE_GAP_EVENT_CONNECT:
            context.connect_status = event->connect.status;
            if (event->connect.status == 0) {
                context.connected = true;
                context.connection_handle = event->connect.conn_handle;
            }
            give_if_created(context.connect_done);
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            context.connected = false;
            context.connect_status = event->disconnect.reason;
            context.security_status = event->disconnect.reason;
            context.gatt_status = event->disconnect.reason;
            context.write_status = event->disconnect.reason;
            give_if_created(context.connect_done);
            give_if_created(context.security_done);
            give_if_created(context.gatt_done);
            give_if_created(context.write_done);
            give_if_created(context.value_done);
            give_if_created(context.disconnect_done);
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            context.security_status = event->enc_change.status;
            give_if_created(context.security_done);
            return 0;

        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            if (!context.pin_available) {
                return 0;
            }
            struct ble_sm_io input = {0};
            input.action = event->passkey.params.action;
            if (input.action == BLE_SM_IOACT_INPUT ||
                input.action == BLE_SM_IOACT_STATIC ||
                input.action == BLE_SM_IOACT_DISP) {
                input.passkey = context.pin;
                (void)ble_sm_inject_io(event->passkey.conn_handle, &input);
            } else if (input.action == BLE_SM_IOACT_NUMCMP) {
                input.numcmp_accept = 0;
                (void)ble_sm_inject_io(event->passkey.conn_handle, &input);
            }
            return 0;
        }

        case BLE_GAP_EVENT_NOTIFY_RX:
            if (event->notify_rx.conn_handle == context.connection_handle) {
                inspect_notification(event->notify_rx.attr_handle,event->notify_rx.om);
            }
            return 0;

        default:
            return 0;
    }
}

static void host_sync(void)
{
    int status = ble_hs_util_ensure_addr(0);
    if (status == 0) {
        status = ble_hs_id_infer_auto(0, &context.own_address_type);
    }
    context.sync_status = status;
    give_if_created(context.sync_done);
}

static void host_reset(int reason)
{
    context.stack_reset = true;
    context.sync_status = reason == 0 ? BLE_HS_ECONTROLLER : reason;
    signal_all_waiters();
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool create_semaphores(void)
{
    context.sync_done = xSemaphoreCreateBinary();
    context.scan_done = xSemaphoreCreateBinary();
    context.connect_done = xSemaphoreCreateBinary();
    context.security_done = xSemaphoreCreateBinary();
    context.gatt_done = xSemaphoreCreateBinary();
    context.write_done = xSemaphoreCreateBinary();
    context.value_done = xSemaphoreCreateBinary();
    context.disconnect_done = xSemaphoreCreateBinary();
    return context.sync_done != NULL && context.scan_done != NULL &&
           context.connect_done != NULL && context.security_done != NULL &&
           context.gatt_done != NULL && context.write_done != NULL &&
           context.value_done != NULL && context.disconnect_done != NULL;
}

static void delete_semaphores(void)
{
    SemaphoreHandle_t semaphores[] = {
        context.sync_done, context.scan_done, context.connect_done,
        context.security_done, context.gatt_done, context.write_done,
        context.value_done, context.disconnect_done
    };
    for (size_t index = 0; index < sizeof(semaphores) / sizeof(semaphores[0]); ++index) {
        if (semaphores[index] != NULL) {
            vSemaphoreDelete(semaphores[index]);
        }
    }
}

static void require_stack_restart(victron_result_t *result)
{
    result->code = VICTRON_RESULT_STACK_ERROR;
    /* Every failure frame must remain compatible with the STM invariant. */
    result->verified = false;
    result->restart_required = true;
}

/*
 * nimble_port_stop() only returns success after the host task has left its
 * event loop. Until that happens, callbacks may still reference context and
 * its semaphores. On any stop/deinit failure the caller must therefore leave
 * those objects alive and reboot before WiFi is allowed to start again.
 */
static bool stop_and_deinit_stack(victron_result_t *result)
{
    const int stop_status = nimble_port_stop();
    if (stop_status != 0) {
        require_stack_restart(result);
        return false;
    }
    if (nimble_port_deinit() != ESP_OK) {
        require_stack_restart(result);
        return false;
    }
    return true;
}

static void reset_gatt_state(void)
{
    context.service_start = 0;
    context.service_end = 0;
    memset(&context.control, 0, sizeof(context.control));
    memset(&context.last_data, 0, sizeof(context.last_data));
    memset(&context.data, 0, sizeof(context.data));
    context.gatt_status = BLE_HS_EUNKNOWN;
    context.write_status = BLE_HS_EUNKNOWN;
    reset_notification_capture();
}

static bool uuid_matches_text(const ble_uuid_t *uuid,const char *expected)
{
    char text[37];
    if(uuid->type==BLE_UUID_TYPE_16)
        snprintf(text,sizeof(text),"0000%04x-0000-1000-8000-00805f9b34fb",BLE_UUID16(uuid)->value);
    else if(uuid->type==BLE_UUID_TYPE_32)
        snprintf(text,sizeof(text),"%08lx-0000-1000-8000-00805f9b34fb",(unsigned long)BLE_UUID32(uuid)->value);
    else ble_uuid_to_str(uuid,text);
    return !strcasecmp(text,expected);
}
static int service_discovered(uint16_t connection_handle,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *service,
                              void *argument)
{
    (void)connection_handle;
    (void)argument;
    if (error->status == 0 && service != NULL) {
        if(context.request->generic_kind) {
            if(uuid_matches_text(&service->uuid.u,context.request->service_uuid)) {
                context.service_start=service->start_handle;context.service_end=service->end_handle;
            }
            return 0;
        }
        if (uuid_prefix(&service->uuid.u, "306b0001-b081-4037-83dc-e59fcc3cdfd0") ||
            (context.request->driver!=3 && uuid_prefix(&service->uuid.u, "306b0001-b081-4037-83dc-e59fcc3cdfd1"))) {
            context.service_start = service->start_handle;
            context.service_end = service->end_handle;
        }
        return 0;
    }
    context.gatt_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    give_if_created(context.gatt_done);
    return 0;
}

static void close_previous_slot(characteristic_slot_t *slot, uint16_t next_def_handle)
{
    if (slot->found && slot->end_handle == 0 && next_def_handle > slot->def_handle) {
        slot->end_handle = (uint16_t)(next_def_handle - 1u);
    }
}

static int characteristic_discovered(uint16_t connection_handle,
                                     const struct ble_gatt_error *error,
                                     const struct ble_gatt_chr *characteristic,
                                     void *argument)
{
    (void)connection_handle;
    (void)argument;
    if (error->status == 0 && characteristic != NULL) {
        close_previous_slot(&context.control, characteristic->def_handle);
        close_previous_slot(&context.last_data, characteristic->def_handle);
        close_previous_slot(&context.data, characteristic->def_handle);

        characteristic_slot_t *slot = NULL;
        if(context.request->generic_kind) {
            if(uuid_matches_text(&characteristic->uuid.u,context.request->characteristic_uuid)) {
                context.control.val_handle=characteristic->val_handle;
                context.control.found=(characteristic->properties & BLE_GATT_CHR_PROP_WRITE) &&
                    (context.request->generic_kind!=4 || (characteristic->properties & BLE_GATT_CHR_PROP_READ));
            }
            return 0;
        }
        if (uuid_prefix(&characteristic->uuid.u,
                        "306b0002-b081-4037-83dc-e59fcc3cdfd")) {
            slot = &context.control;
        } else if (uuid_prefix(&characteristic->uuid.u,
                               "306b0003-b081-4037-83dc-e59fcc3cdfd")) {
            slot = &context.last_data;
        } else if (uuid_prefix(&characteristic->uuid.u,
                               "306b0004-b081-4037-83dc-e59fcc3cdfd")) {
            slot = &context.data;
        }
        if (slot != NULL) {
            slot->def_handle = characteristic->def_handle;
            slot->val_handle = characteristic->val_handle;
            slot->found = true;
        }
        return 0;
    }

    if (context.control.found && context.control.end_handle == 0) {
        context.control.end_handle = context.service_end;
    }
    if (context.last_data.found && context.last_data.end_handle == 0) {
        context.last_data.end_handle = context.service_end;
    }
    if (context.data.found && context.data.end_handle == 0) {
        context.data.end_handle = context.service_end;
    }
    context.gatt_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    give_if_created(context.gatt_done);
    return 0;
}

static int descriptor_discovered(uint16_t connection_handle,
                                 const struct ble_gatt_error *error,
                                 uint16_t characteristic_value_handle,
                                 const struct ble_gatt_dsc *descriptor,
                                 void *argument)
{
    (void)connection_handle;
    characteristic_slot_t *slot = (characteristic_slot_t *)argument;
    if (error->status == 0 && descriptor != NULL) {
        if (ble_uuid_u16(&descriptor->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16 &&
            slot != NULL && characteristic_value_handle == slot->val_handle) {
            slot->cccd_handle = descriptor->handle;
        }
        return 0;
    }
    context.gatt_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    give_if_created(context.gatt_done);
    return 0;
}

static int write_complete(uint16_t connection_handle,
                          const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attribute,
                          void *argument)
{
    (void)connection_handle;
    (void)attribute;
    (void)argument;
    context.write_status = error->status;
    give_if_created(context.write_done);
    return 0;
}

static bool wait_gatt(void)
{
    return take_before_attempt_deadline(context.gatt_done, VIC_GATT_WAIT_MS) &&
           context.gatt_status == 0 && context.connected && !context.stack_reset;
}

static bool discover_service_and_characteristics(void)
{
    reset_gatt_state();
    drain(context.gatt_done);
    int status = ble_gattc_disc_all_svcs(context.connection_handle,
                                         service_discovered,
                                         NULL);
    if (status != 0 || !wait_gatt() || context.service_start == 0 ||
        context.service_end < context.service_start) {
        return false;
    }

    context.gatt_status = BLE_HS_EUNKNOWN;
    drain(context.gatt_done);
    status = ble_gattc_disc_all_chrs(context.connection_handle,
                                     context.service_start,
                                     context.service_end,
                                     characteristic_discovered,
                                     NULL);
    if (status != 0 || !wait_gatt() || !context.control.found ||
        (!context.request->generic_kind && (!context.last_data.found || !context.data.found))) {
        return false;
    }

    if(context.request->generic_kind) return true;
    characteristic_slot_t *slots[] = {
        &context.control, &context.last_data, &context.data
    };
    for (size_t index = 0; index < sizeof(slots) / sizeof(slots[0]); ++index) {
        characteristic_slot_t *slot = slots[index];
        if (slot->end_handle <= slot->val_handle) {
            return false;
        }
        context.gatt_status = BLE_HS_EUNKNOWN;
        drain(context.gatt_done);
        status = ble_gattc_disc_all_dscs(context.connection_handle,
                                         slot->val_handle,
                                         slot->end_handle,
                                         descriptor_discovered,
                                         slot);
        if (status != 0 || !wait_gatt() || slot->cccd_handle == 0) {
            return false;
        }
    }
    return true;
}

static bool write_attribute(uint16_t handle, const uint8_t *bytes, size_t length)
{
    if (!context.connected || handle == 0 || bytes == NULL || length == 0) {
        return false;
    }
    context.write_status = BLE_HS_EUNKNOWN;
    drain(context.write_done);
    const int status = ble_gattc_write_flat(context.connection_handle,
                                            handle,
                                            bytes,
                                            (uint16_t)length,
                                            write_complete,
                                            NULL);
    if (status != 0) {
        return false;
    }
    return take_before_attempt_deadline(context.write_done, VIC_WRITE_WAIT_MS) &&
           context.write_status == 0 && context.connected && !context.stack_reset;
}

static int generic_read_complete(uint16_t connection_handle,const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attribute,void *argument)
{
    (void)connection_handle;(void)argument;
    context.gatt_status=error->status;
    context.read_length=0;
    if(!error->status && attribute && attribute->om) {
        const size_t n=OS_MBUF_PKTLEN(attribute->om);
        if(n<=sizeof(context.read_value) && os_mbuf_copydata(attribute->om,0,n,context.read_value)==0)
            context.read_length=n;
        else context.gatt_status=BLE_HS_EBADDATA;
    }
    give_if_created(context.gatt_done);return 0;
}
static bool protocol_write(uint16_t handle,const uint8_t *data,size_t n)
{
    if(context.request->driver!=3) return write_attribute(handle,data,n);
    if(!context.connected || context.stack_reset || esp_timer_get_time()>=context.attempt_deadline_us) return false;
    unsigned received;
    portENTER_CRITICAL(&notification_lock);
    received=context.received_chunks;
    if(received>=32) context.received_chunks=0;
    portEXIT_CRITICAL(&notification_lock);
    if(received>=32) {
        const uint8_t credits[]={0xf9,0x41};
        if(ble_gattc_write_no_rsp_flat(context.connection_handle,context.control.val_handle,credits,sizeof(credits))) return false;
    }
    return ble_gattc_write_no_rsp_flat(context.connection_handle,handle,data,n)==0;
}
static bool request_register(uint16_t reg,uint8_t *value,size_t size)
{
    const uint8_t get_frame[]={0x05,context.instance,0x81,0x19,(uint8_t)(reg>>8),(uint8_t)reg};
    drain(context.value_done);
    portENTER_CRITICAL(&notification_lock);
    context.awaited_register=reg;context.load_value_available=false;context.register_length=0;
    portEXIT_CRITICAL(&notification_lock);
    if(!protocol_write(context.last_data.val_handle,get_frame,sizeof(get_frame)) ||
       !take_before_attempt_deadline(context.value_done,VIC_VALUE_WAIT_MS)) return false;
    portENTER_CRITICAL(&notification_lock);
    bool available=context.load_value_available && context.register_length==size;
    if(available) memcpy(value,context.register_value,size);
    context.awaited_register=0;
    portEXIT_CRITICAL(&notification_lock);
    return available && context.connected && !context.stack_reset;
}
static bool request_load_value(uint8_t *value)
{
    return request_register(0xedab,value,1);
}
static victron_result_code_t run_batteryprotect(const victron_request_t *request,victron_result_t *result)
{
    /* Dedicated, deliberately narrow driver. Never send MPPT EDAB/0093 writes. */
    drain(context.gatt_done);context.gatt_status=BLE_HS_EUNKNOWN;
    if(ble_gattc_read(context.connection_handle,context.control.val_handle,generic_read_complete,NULL) ||
       !wait_gatt() || !context.read_length) return VICTRON_RESULT_SECURITY_FAILED;
    const uint8_t init1[]={0xfa,0x80,0xff}, init2[]={0xf9,0x80}, devices[]={1}, subscribe[]={3,0};
    if(!protocol_write(context.control.val_handle,init1,sizeof(init1)) || !delay_before_attempt_deadline(200) ||
       !protocol_write(context.control.val_handle,init2,sizeof(init2)) || !delay_before_attempt_deadline(200) ||
       !protocol_write(context.last_data.val_handle,devices,sizeof(devices)) || !delay_before_attempt_deadline(300) ||
       !protocol_write(context.last_data.val_handle,subscribe,sizeof(subscribe)) || !delay_before_attempt_deadline(300))
        return VICTRON_RESULT_INITIAL_READ_FAILED;
    uint8_t product[4],mode=255,output=255;
    /* Only A3B1 was physically verified. Refuse other products rather than
       silently applying a family-wide register assumption. */
    if(!request_register(0x0100,product,4) || product[0]!=0 || product[1]!=0xb1 || product[2]!=0xa3 ||
       !request_register(0x0200,&mode,1) || (mode!=3 && mode!=4) ||
       !request_register(0xeda8,&output,1)) return VICTRON_RESULT_INITIAL_READ_FAILED;
    result->initial_value=mode;result->load_value=output;
    if(mode!=request->desired_value) {
        const uint8_t frame[]={6,0,0x82,0x19,2,0,0x41,request->desired_value};
        if(!protocol_write(context.last_data.val_handle,frame,sizeof(frame))) return VICTRON_RESULT_WRITE_FAILED;
        result->changed=true;
        if(!delay_before_attempt_deadline(300)) return VICTRON_RESULT_VERIFY_FAILED;
    }
    if(!request_register(0x0200,&mode,1)) return VICTRON_RESULT_VERIFY_FAILED;
    result->verified_value=mode;
    if(mode!=request->desired_value) return VICTRON_RESULT_VERIFY_FAILED;
    const uint8_t expected_output=mode==3?1:0;
    /* Re-enable can be delayed by the device. Do not disable protections or
       mistake transient state 3 for ON. Bound the wait and retain readback. */
    for(unsigned i=0;i<30;++i) {
        if(!request_register(0xeda8,&output,1)) return VICTRON_RESULT_VERIFY_FAILED;
        result->load_value=output;
        if(output==expected_output) {result->verified=true;return VICTRON_RESULT_OK;}
        if(!delay_before_attempt_deadline(1000)) break;
    }
    return VICTRON_RESULT_VERIFY_FAILED;
}

static void disconnect_if_needed(void)
{
    if (!context.connected) {
        return;
    }
    drain(context.disconnect_done);
    if (ble_gap_terminate(context.connection_handle,
                          BLE_ERR_REM_USER_CONN_TERM) == 0) {
        (void)xSemaphoreTake(context.disconnect_done,
                             pdMS_TO_TICKS(VIC_DISCONNECT_WAIT_MS));
    }
    context.connected = false;
}

static victron_result_code_t run_attempt(const victron_request_t *request,
                                         victron_result_t *result)
{
    context.attempt_deadline_us = esp_timer_get_time() +
                                  (int64_t)VIC_ATTEMPT_DEADLINE_MS * 1000;
    context.target_seen = false;
    context.peer_address.type = request->address_type >= 0
                                    ? (uint8_t)request->address_type
                                    : BLE_ADDR_PUBLIC;
    memcpy(context.peer_address.val, context.target_address, 6);
    drain(context.scan_done);

    const struct ble_gap_disc_params scan_parameters = {
        .itvl = 0,
        .window = 0,
        .filter_policy = 0,
        .limited = 0,
        .passive = 1,
        .filter_duplicates = 1,
    };
    int status = ble_gap_disc(context.own_address_type,
                              VIC_SCAN_MS,
                              &scan_parameters,
                              gap_event,
                              NULL);
    if (status != 0 ||
        !take_before_attempt_deadline(context.scan_done, VIC_SCAN_MS + 1000u) ||
        context.stack_reset) {
        (void)ble_gap_disc_cancel();
        (void)xSemaphoreTake(context.scan_done, pdMS_TO_TICKS(1000));
        return VICTRON_RESULT_STACK_ERROR;
    }
    result->target_seen = result->target_seen || context.target_seen;
    if (!context.target_seen && request->address_type < 0) {
        return VICTRON_RESULT_TARGET_NOT_FOUND;
    }

    context.connect_status = BLE_HS_EUNKNOWN;
    drain(context.connect_done);
    status = ble_gap_connect(context.own_address_type,
                             &context.peer_address,
                             VIC_CONNECT_MS,
                             NULL,
                             gap_event,
                             NULL);
    if (status != 0 ||
        !take_before_attempt_deadline(context.connect_done, VIC_CONNECT_MS + 1000u) ||
        context.connect_status != 0 || !context.connected || context.stack_reset) {
        if (!context.connected) {
            (void)ble_gap_conn_cancel();
            (void)xSemaphoreTake(context.connect_done, pdMS_TO_TICKS(1000));
        }
        disconnect_if_needed();
        return VICTRON_RESULT_CONNECT_FAILED;
    }
    /* A successful direct-address fallback also proves the target is present. */
    result->target_seen = true;

    if (request->pairing) {
        context.security_status = BLE_HS_EUNKNOWN;
        drain(context.security_done);
        status = ble_gap_security_initiate(context.connection_handle);
        if (status != 0 ||
            !take_before_attempt_deadline(context.security_done, VIC_SECURITY_WAIT_MS) ||
            context.security_status != 0 || !context.connected || context.stack_reset) {
            disconnect_if_needed();
            return VICTRON_RESULT_SECURITY_FAILED;
        }
    }

    if (!discover_service_and_characteristics()) {
        disconnect_if_needed();
        return VICTRON_RESULT_GATT_NOT_FOUND;
    }

    if(request->generic_kind) {
        if(!write_attribute(context.control.val_handle,request->value,request->value_length)) {
            disconnect_if_needed();return VICTRON_RESULT_WRITE_FAILED;
        }
        result->changed=true;
        if(request->generic_kind==4) {
            drain(context.gatt_done);context.gatt_status=BLE_HS_EUNKNOWN;
            const int status=ble_gattc_read(context.connection_handle,context.control.val_handle,generic_read_complete,NULL);
            result->verified=status==0 && wait_gatt() && context.read_length==request->value_length &&
                !memcmp(context.read_value,request->value,request->value_length);
            if(!result->verified) {disconnect_if_needed();return VICTRON_RESULT_VERIFY_FAILED;}
        }
        disconnect_if_needed();return VICTRON_RESULT_OK;
    }
    const uint8_t notify_enable[] = {0x01, 0x00};
    if (!write_attribute(context.control.cccd_handle, notify_enable, sizeof(notify_enable)) ||
        !write_attribute(context.last_data.cccd_handle, notify_enable, sizeof(notify_enable)) ||
        !write_attribute(context.data.cccd_handle, notify_enable, sizeof(notify_enable))) {
        disconnect_if_needed();
        return VICTRON_RESULT_GATT_NOT_FOUND;
    }
    if(request->driver==3) {
        victron_result_code_t code=run_batteryprotect(request,result);
        disconnect_if_needed();return code;
    }

    const uint8_t control_init_1[] = {0xFA, 0x80, 0xFF};
    const uint8_t control_init_2[] = {0xF9, 0x80};
    const uint8_t session_1[] = {0x01};
    const uint8_t session_2[] = {0x03, 0x00};
    const uint8_t session_3[] = {
        0x06, 0x00, 0x82, 0x18, 0x93, 0x42, 0x10, 0x27,
        0x05, 0x00, 0x82, 0x19, 0xEC, 0x66, 0x19, 0xEC, 0x65,
        0x03, 0x01, 0x03, 0x03
    };

    bool session_ok = write_attribute(context.control.val_handle,
                                      control_init_1,
                                      sizeof(control_init_1));
    session_ok = session_ok && write_attribute(context.control.val_handle,
                                               control_init_2,
                                               sizeof(control_init_2));
    session_ok = session_ok && write_attribute(context.last_data.val_handle,
                                               session_1,
                                               sizeof(session_1));
    session_ok = session_ok && delay_before_attempt_deadline(200);
    session_ok = session_ok && write_attribute(context.last_data.val_handle,
                                               session_2,
                                               sizeof(session_2));
    session_ok = session_ok && delay_before_attempt_deadline(200);
    session_ok = session_ok && write_attribute(context.last_data.val_handle,
                                               session_3,
                                               sizeof(session_3));
    session_ok = session_ok && delay_before_attempt_deadline(700);

    uint8_t current_value = VICTRON_VALUE_UNKNOWN;
    if (!session_ok || !request_load_value(&current_value)) {
        disconnect_if_needed();
        return VICTRON_RESULT_INITIAL_READ_FAILED;
    }
    result->initial_value = current_value;
    result->load_value = current_value;

    const uint8_t target_value = smartMpptControl(current_value, request->desired_value);
    if (current_value != target_value) {
        const uint8_t set_frame[] = {
            0x06, context.instance, 0x82, 0x19,
            0xED, 0xAB, 0x41, target_value
        };
        if (!write_attribute(context.last_data.val_handle,
                             set_frame,
                             sizeof(set_frame))) {
            disconnect_if_needed();
            return VICTRON_RESULT_WRITE_FAILED;
        }
        result->changed = true;
        if (!delay_before_attempt_deadline(500)) {
            disconnect_if_needed();
            return VICTRON_RESULT_VERIFY_FAILED;
        }
    }

    uint8_t verified_value = VICTRON_VALUE_UNKNOWN;
    const bool read_back = request_load_value(&verified_value);
    if (read_back) {
        result->verified_value = verified_value;
        result->load_value = verified_value;
    }
    result->verified = read_back && verified_value == target_value;
    disconnect_if_needed();
    return result->verified ? VICTRON_RESULT_OK : VICTRON_RESULT_VERIFY_FAILED;
}

esp_err_t victron_ble_run(const victron_request_t *request,
                          victron_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    result->initial_value = VICTRON_VALUE_UNKNOWN;
    result->verified_value = VICTRON_VALUE_UNKNOWN;
    result->load_value = VICTRON_VALUE_UNKNOWN;
    result->code = VICTRON_RESULT_BAD_SETTINGS;
    if (!victron_ble_request_valid(request)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* A valid request has made its first BLE attempt once stack setup starts. */
    result->attempts = 1;

    memset(&context, 0, sizeof(context));
    context.connection_handle = BLE_HS_CONN_HANDLE_NONE;
    context.instance = request->instance;
    context.request = request;
    if (!parse_mac(request->mac, context.target_address) || !create_semaphores()) {
        delete_semaphores();
        memset(&context, 0, sizeof(context));
        result->code = VICTRON_RESULT_STACK_ERROR;
        return ESP_ERR_NO_MEM;
    }
    if (request->pairing) {
        context.pin_available = pin_to_number(request->pin, &context.pin);
    }

    esp_err_t stack_status = nimble_port_init();
    if (stack_status != ESP_OK) {
        require_stack_restart(result);
        return stack_status;
    }

    ble_hs_cfg.reset_cb = host_reset;
    ble_hs_cfg.sync_cb = host_sync;
    ble_hs_cfg.sm_io_cap = request->pairing
                               ? BLE_HS_IO_KEYBOARD_ONLY
                               : BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = request->pairing ? 1 : 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 0;
    if (request->pairing &&
        ble_sm_configure_static_passkey(context.pin, true) != 0) {
        (void)ble_sm_configure_static_passkey(0, false);
        result->code = VICTRON_RESULT_STACK_ERROR;
        if (nimble_port_deinit() != ESP_OK) {
            require_stack_restart(result);
            return ESP_FAIL;
        }
        delete_semaphores();
        memset(&context, 0, sizeof(context));
        return ESP_FAIL;
    }

    context.sync_status = BLE_HS_EUNKNOWN;
    nimble_port_freertos_init(host_task);
    if (xSemaphoreTake(context.sync_done, pdMS_TO_TICKS(3000)) != pdTRUE ||
        context.sync_status != 0 || context.stack_reset) {
        (void)ble_sm_configure_static_passkey(0, false);
        result->code = VICTRON_RESULT_STACK_ERROR;
        if (!stop_and_deinit_stack(result)) {
            return ESP_FAIL;
        }
        delete_semaphores();
        memset(&context, 0, sizeof(context));
        return ESP_FAIL;
    }

    bool changed_any = false;
    uint8_t last_known_load_value = VICTRON_VALUE_UNKNOWN;
    for (uint8_t attempt = 1; attempt <= request->max_attempts; ++attempt) {
        result->attempts = attempt;
        result->initial_value = VICTRON_VALUE_UNKNOWN;
        result->verified_value = VICTRON_VALUE_UNKNOWN;
        result->load_value = VICTRON_VALUE_UNKNOWN;
        result->changed = false;
        result->verified = false;
        result->code = run_attempt(request, result);
        changed_any = changed_any || result->changed;
        if (result->load_value != VICTRON_VALUE_UNKNOWN) {
            last_known_load_value = result->load_value;
        }
        result->advertisements = context.advertisements;
        if (result->code == VICTRON_RESULT_OK ||
            result->code == VICTRON_RESULT_STACK_ERROR) {
            break;
        }
        if (attempt < request->max_attempts) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    result->changed = changed_any;
    if (result->load_value == VICTRON_VALUE_UNKNOWN) {
        result->load_value = last_known_load_value;
    }

    disconnect_if_needed();
    (void)ble_sm_configure_static_passkey(0, false);
    if (!stop_and_deinit_stack(result)) {
        return ESP_FAIL;
    }
    delete_semaphores();
    volatile uint8_t *bytes = (volatile uint8_t *)&context;
    for (size_t index = 0; index < sizeof(context); ++index) {
        bytes[index] = 0;
    }
    return result->code == VICTRON_RESULT_OK ? ESP_OK : ESP_FAIL;
}

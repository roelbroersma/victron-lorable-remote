/* Replay the actual production BLE state machine, not a parallel reimplementation.
 * Build with -Itools/tests/stubs -Istm32. No radio or credentials are needed.
 * The SDK shim models event arrival and failure injection, not RF performance.
 */
#include "../../esp8684/main/victron_ble.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
enum { FAKE_NONE, FAKE_READ_TIMEOUT, FAKE_VALUE_TIMEOUT, FAKE_UNAUTHENTICATED,
       FAKE_UNENCRYPTED, FAKE_MTU_UNSUPPORTED, FAKE_MTU_TIMEOUT,
       FAKE_EARLY_CREDIT, FAKE_MISSING_CREDIT, FAKE_OUTPUT_MISMATCH,
       FAKE_SECURITY_IN_PROGRESS, FAKE_ALREADY_AUTHENTICATED,
       FAKE_MULTIPLE_SERVICES, FAKE_SUBSCRIBE_NACK, FAKE_MPPT_WRONG_INSTANCE,
       FAKE_BATTERYPROTECT_ON, FAKE_BATTERYPROTECT_OFF, FAKE_BATTERYPROTECT_WRONG_PID,
       FAKE_GENERIC_READBACK, FAKE_BATTERYPROTECT_OUTPUT_MISMATCH,
       FAKE_BATTERYPROTECT_ALREADY_ON, FAKE_BATTERYPROTECT_ALREADY_OFF };
struct fake_pending { int64_t due; uint16_t handle,length; uint8_t bytes[80]; };
static struct {
    int failure,invalid;
    int64_t now;
    uint16_t mtu,preferred_mtu;
    unsigned sem_used,protocol_writes,mode_writes,readbacks,rx_returned;
    unsigned cccd_writes,mtu_exchanges,security_checks,disconnects,generic_writes;
    uint8_t mode,tx_credit;
    uint16_t product;
    uint8_t generic_value[20],generic_size;
    bool connected,authenticated,encrypted,batteryprotect,generic;
    test_semaphore semaphores[16];
    struct fake_pending pending[64];
    ble_gap_event_fn *gap; void *gap_arg;
} fake;
struct test_hs_cfg ble_hs_cfg;

static void fake_notify(uint16_t handle,const uint8_t *bytes,uint16_t length)
{
    struct os_mbuf om={bytes,length};
    struct ble_gap_event e={.type=BLE_GAP_EVENT_NOTIFY_RX};
    e.notify_rx.conn_handle=1;e.notify_rx.attr_handle=handle;e.notify_rx.om=&om;
    if(handle==33 && length==2 && bytes[0]==0xf9) fake.tx_credit+=bytes[1];
    fake.gap(&e,fake.gap_arg);
}
static void fake_queue(uint16_t handle,const uint8_t *bytes,uint16_t length,unsigned delay)
{
    if(length>sizeof(fake.pending[0].bytes)) {fake.invalid=1;return;}
    for(unsigned i=0;i<COUNT(fake.pending);i++) if(!fake.pending[i].length) {
        fake.pending[i].handle=handle;fake.pending[i].length=length;
        fake.pending[i].due=fake.now+(int64_t)delay*1000;
        memcpy(fake.pending[i].bytes,bytes,length);return;
    }
    fake.invalid=2;
}
static void fake_advance(unsigned milliseconds)
{
    int64_t until=fake.now+(int64_t)milliseconds*1000;
    for(;;) {
        unsigned chosen=COUNT(fake.pending);int64_t earliest=until+1;
        for(unsigned i=0;i<COUNT(fake.pending);i++)
            if(fake.pending[i].length && fake.pending[i].due<earliest) {
                earliest=fake.pending[i].due;chosen=i;
            }
        if(chosen==COUNT(fake.pending)) break;
        struct fake_pending row=fake.pending[chosen];fake.pending[chosen].length=0;
        fake.now=row.due;fake_notify(row.handle,row.bytes,row.length);
    }
    fake.now=until;
}
static void fake_frame(const uint8_t *data,size_t size)
{
    unsigned payload=fake.mtu-3;
    while(size>payload) {
        fake_queue(39,data,payload,10);data+=payload;size-=payload;
    }
    fake_queue(36,data,(uint16_t)size,10);
}
static void fake_credit(void)
{
    const uint8_t credit[]={0xf9,1};fake_queue(33,credit,sizeof(credit),20);
}
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    if(fake.sem_used==COUNT(fake.semaphores)) return NULL;
    return &fake.semaphores[fake.sem_used++];
}
int xSemaphoreGive(SemaphoreHandle_t s) {if(!s || s->deleted){fake.invalid=3;return 0;}s->available=true;return 1;}
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t ticks)
{
    if(!s || s->deleted){fake.invalid=4;return 0;}
    if(!s->available && ticks) {
        /* Wake promptly when a delayed host callback signals the semaphore. */
        for(TickType_t i=0;i<ticks && !s->available;i++) fake_advance(1);
    }
    if(!s->available) return 0;
    s->available=false;return 1;
}
void vSemaphoreDelete(SemaphoreHandle_t s) {s->deleted=true;}
void vTaskDelay(TickType_t ticks) {fake_advance(ticks);}
int64_t esp_timer_get_time(void) {return fake.now;}
int os_mbuf_copydata(const struct os_mbuf *m,int offset,int n,void *dst)
{if(offset<0 || n<0 || offset+n>m->len)return -1;memcpy(dst,m->data+offset,(size_t)n);return 0;}
int ble_uuid_from_str(ble_uuid_any_t *u,const char *s)
{memset(u,0,sizeof(*u));u->u.type=BLE_UUID_TYPE_128;snprintf(u->u.text,37,"%s",s);return strlen(s)==36?0:1;}
char *ble_uuid_to_str(const ble_uuid_t *u,char *s) {memcpy(s,u->text,37);return s;}
uint16_t ble_uuid_u16(const ble_uuid_t *u) {return u->type==16?BLE_UUID16(u)->value:0;}
int ble_hs_util_ensure_addr(int x) {(void)x;return 0;}
int ble_hs_id_infer_auto(int x,uint8_t *t) {(void)x;*t=0;return 0;}
int ble_gap_disc(uint8_t type,int32_t duration,const struct ble_gap_disc_params *p,ble_gap_event_fn *cb,void *arg)
{
    (void)type;(void)duration;(void)p;fake.gap=cb;fake.gap_arg=arg;
    struct ble_gap_event e={.type=BLE_GAP_EVENT_DISC};e.disc.addr.type=BLE_ADDR_RANDOM;
    const uint8_t address[]={1,0,0,0,0,0xc0};memcpy(e.disc.addr.val,address,6);
    e.disc.rssi=-60;cb(&e,arg);return 0;
}
int ble_gap_disc_cancel(void) {return 0;}
int ble_gap_connect(uint8_t type,const ble_addr_t *address,int32_t duration,const void *params,ble_gap_event_fn *cb,void *arg)
{
    (void)type;(void)duration;(void)params;fake.gap=cb;fake.gap_arg=arg;
    if(address->type!=BLE_ADDR_RANDOM)fake.invalid=5;
    fake.connected=true;
    fake.encrypted=fake.failure==FAKE_ALREADY_AUTHENTICATED;
    fake.authenticated=fake.encrypted;
    struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT};e.connect.conn_handle=1;cb(&e,arg);return 0;
}
int ble_gap_conn_cancel(void) {return 0;}
int ble_gap_terminate(uint16_t handle,uint8_t reason)
{
    (void)reason;if(handle!=1)fake.invalid=6;
    fake.connected=false;fake.disconnects++;
    memset(fake.pending,0,sizeof(fake.pending));
    struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT};e.disconnect.reason=0x213;e.disconnect.conn.conn_handle=1;
    fake.gap(&e,fake.gap_arg);return 0;
}
int ble_gap_security_initiate(uint16_t handle)
{
    (void)handle;fake.encrypted=fake.failure!=FAKE_UNENCRYPTED;
    fake.authenticated=fake.failure!=FAKE_UNAUTHENTICATED;
    struct ble_gap_event e={.type=BLE_GAP_EVENT_ENC_CHANGE};e.enc_change.conn_handle=1;
    fake.gap(&e,fake.gap_arg);return fake.failure==FAKE_SECURITY_IN_PROGRESS?BLE_HS_EALREADY:0;
}
int ble_gap_conn_find(uint16_t handle,struct ble_gap_conn_desc *d)
{
    fake.security_checks++;memset(d,0,sizeof(*d));d->conn_handle=handle;
    d->sec_state.encrypted=fake.encrypted;d->sec_state.authenticated=fake.authenticated;
    d->sec_state.key_size=16;return fake.connected?0:BLE_HS_ENOTCONN;
}
int ble_sm_inject_io(uint16_t handle,struct ble_sm_io *io) {(void)handle;(void)io;return 0;}
int ble_sm_configure_static_passkey(uint32_t pin,bool enabled) {(void)pin;(void)enabled;return 0;}
int nimble_port_init(void) {return 0;}
int nimble_port_stop(void) {return 0;}
int nimble_port_deinit(void) {return 0;}
void nimble_port_run(void) {}
void nimble_port_freertos_init(void (*task)(void *)) {(void)task;ble_hs_cfg.sync_cb();}
void nimble_port_freertos_deinit(void) {}
int ble_att_set_preferred_mtu(uint16_t mtu) {fake.preferred_mtu=mtu;return 0;}
uint16_t ble_att_mtu(uint16_t handle) {(void)handle;return fake.mtu;}
int ble_gattc_exchange_mtu(uint16_t handle,ble_gatt_mtu_fn *cb,void *arg)
{
    fake.mtu_exchanges++;
    if(fake.failure==FAKE_MTU_TIMEOUT) return 0;
    struct ble_gatt_error e={0};
    if(fake.failure==FAKE_MTU_UNSUPPORTED) {e.status=BLE_HS_ATT_ERR(BLE_ATT_ERR_REQ_NOT_SUPPORTED);fake.mtu=23;}
    else fake.mtu=77;
    cb(handle,&e,fake.mtu,arg);return 0;
}
int ble_gattc_disc_all_svcs(uint16_t handle,ble_gatt_disc_svc_fn *cb,void *arg)
{
    struct ble_gatt_error e={0};struct ble_gatt_svc s={.start_handle=31,.end_handle=40};
    ble_uuid_from_str(&s.uuid,"306b0001-b081-4037-83dc-e59fcc3cdfd0");cb(handle,&e,&s,arg);
    /* The real SmartSolar inventory has dfd0 only. A separate regression case
     * checks that an additional later dfd1 cannot steal that service. */
    if(fake.failure==FAKE_MULTIPLE_SERVICES) {
        s.start_handle=41;s.end_handle=50;
        ble_uuid_from_str(&s.uuid,"306b0001-b081-4037-83dc-e59fcc3cdfd1");cb(handle,&e,&s,arg);
    }
    e.status=BLE_HS_EDONE;cb(handle,&e,NULL,arg);return 0;
}
int ble_gattc_disc_all_chrs(uint16_t handle,uint16_t first,uint16_t last,ble_gatt_chr_fn *cb,void *arg)
{
    struct ble_gatt_error e={0};
    if(first!=31 || last!=40) {fake.invalid=7;e.status=BLE_HS_EBADDATA;cb(handle,&e,NULL,arg);return 0;}
    for(unsigned i=0;i<3;i++) {
        struct ble_gatt_chr c={.def_handle=32+3*i,.val_handle=33+3*i,
            .properties=BLE_GATT_CHR_PROP_NOTIFY|BLE_GATT_CHR_PROP_WRITE_NO_RSP|BLE_GATT_CHR_PROP_READ};
        if(fake.generic)c.properties|=BLE_GATT_CHR_PROP_WRITE;
        char uuid[37];snprintf(uuid,sizeof(uuid),"306b000%u-b081-4037-83dc-e59fcc3cdfd0",i+2);
        ble_uuid_from_str(&c.uuid,uuid);cb(handle,&e,&c,arg);
    }
    e.status=BLE_HS_EDONE;cb(handle,&e,NULL,arg);return 0;
}
int ble_gattc_disc_all_dscs(uint16_t handle,uint16_t val,uint16_t last,ble_gatt_dsc_fn *cb,void *arg)
{
    if(last!=val+1)fake.invalid=8;
    struct ble_gatt_error e={0};struct ble_gatt_dsc d={.handle=val+1};
    d.uuid.u16.u.type=16;d.uuid.u16.value=0x2902;cb(handle,&e,val,&d,arg);
    e.status=BLE_HS_EDONE;cb(handle,&e,val,NULL,arg);return 0;
}
int ble_gattc_write_flat(uint16_t handle,uint16_t attribute,const void *bytes,uint16_t size,ble_gatt_attr_fn *cb,void *arg)
{
    const uint8_t *p=bytes;
    if(fake.generic) {
        if(attribute!=33 || !size || size>sizeof(fake.generic_value)) {fake.invalid=16;return BLE_HS_EBADDATA;}
        memcpy(fake.generic_value,p,size);fake.generic_size=size;fake.generic_writes++;
        struct ble_gatt_error e={0};cb(handle,&e,NULL,arg);return 0;
    }
    if((attribute!=34 && attribute!=37 && attribute!=40) || size!=2 || p[0]!=1 || p[1]!=0)fake.invalid=9;
    fake.cccd_writes++;
    if(attribute==34 && fake.failure==FAKE_EARLY_CREDIT) {
        const uint8_t credit[]={0xf9,1};fake_notify(33,credit,sizeof(credit));
    }
    struct ble_gatt_error e={0};cb(handle,&e,NULL,arg);return 0;
}
int ble_gattc_read(uint16_t handle,uint16_t attribute,ble_gatt_attr_fn *cb,void *arg)
{
    if(attribute!=33)fake.invalid=10;
    if(fake.generic) {
        struct os_mbuf om={fake.generic_value,fake.generic_size};
        struct ble_gatt_attr a={attribute,&om};struct ble_gatt_error e={0};
        cb(handle,&e,&a,arg);return 0;
    }
    if(fake.cccd_writes)fake.invalid=15; /* Preserve the first asynchronous F9. */
    if(fake.failure==FAKE_READ_TIMEOUT)return 0;
    uint8_t bytes[]={0,4,0,1,222,(uint8_t)(fake.mtu-3),0};
    struct os_mbuf om={bytes,sizeof(bytes)};struct ble_gatt_attr a={attribute,&om};struct ble_gatt_error e={0};
    cb(handle,&e,&a,arg);return 0;
}
int ble_gattc_write_no_rsp_flat(uint16_t handle,uint16_t attribute,const void *bytes,uint16_t size)
{
    const uint8_t *p=bytes;(void)handle;
    if(!fake.connected || !size || size>fake.mtu-3) {fake.invalid=11;return BLE_HS_EBADDATA;}
    if(attribute==33) {
        if(p[0]==0xfa && size==3) {
            /* Model either permitted arrival point for the initial grant,
             * never grant the one-chunk window twice. */
            if(fake.failure!=FAKE_MISSING_CREDIT && fake.failure!=FAKE_EARLY_CREDIT)fake_credit();
            return 0;
        }
        if(p[0]==0xf9 && size==2) {fake.rx_returned+=p[1];return 0;}
        fake.invalid=12;return BLE_HS_EBADDATA;
    }
    if(attribute!=36 || !fake.tx_credit){fake.invalid=13;return BLE_HS_EBADDATA;}
    fake.tx_credit--;fake.protocol_writes++;fake_credit();
    if(p[0]==1 && size==1) {
        if(fake.batteryprotect) {
            const uint8_t r[]={2,0x9f,0,0,0xff};fake_frame(r,sizeof(r));return 0;
        }
        const uint8_t r[]={2,0x9f,0,0,1,0,3,1,0xff};fake_frame(r,sizeof(r));return 0;
    }
    if(p[0]==3 && size==2) {
        uint8_t r[]={7,0,3,0};
        if(fake.failure==FAKE_SUBSCRIBE_NACK)r[3]=1;
        fake_frame(r,sizeof(r));return 0;
    }
    if(fake.batteryprotect) {
        if(p[0]==5 && size==6 && p[1]==0 && p[3]==0x19) {
            uint16_t reg=((uint16_t)p[4]<<8)|p[5];
            if(reg==0x0100) {
                uint8_t r[]={8,0,0x19,1,0,0x44,0,(uint8_t)fake.product,(uint8_t)(fake.product>>8),0xfe};
                fake_frame(r,sizeof(r));fake.readbacks++;return 0;
            }
            if(reg==0x0200 || reg==0xeda8) {
                uint8_t r[]={8,0,0x19,p[4],p[5],0x41,reg==0x0200?fake.mode:(fake.mode==3)};
                if(reg==0xeda8 && fake.failure==FAKE_BATTERYPROTECT_OUTPUT_MISMATCH)r[6]=0;
                fake_frame(r,sizeof(r));fake.readbacks++;return 0;
            }
        }
        if(p[0]==6 && size==8 && p[1]==0 && p[3]==0x19 && p[4]==2 && p[5]==0 && p[6]==0x41 && (p[7]==3 || p[7]==4)) {
            fake.mode=p[7];fake.mode_writes++;return 0;
        }
        /* The BP profile may never send MPPT EDAB or session 0093 writes. */
        fake.invalid=17;return BLE_HS_EBADDATA;
    }
    if(p[0]==6 && size==8 && p[1]==0 && p[3]==0x18 && p[4]==0x93) {
        const uint8_t r[]={8,0,0x18,0x93,0x42,0x10,0x27};fake_frame(r,sizeof(r));return 0;
    }
    if(p[0]==6 && size==8 && p[1]==3 && p[3]==0x19 && p[4]==0xed && p[5]==0xab) {
        fake.mode=p[7];fake.mode_writes++;return 0;
    }
    if(fake.failure==FAKE_MPPT_WRONG_INSTANCE && p[0]==5 && size==6 && p[1]==0 && p[3]==0x19 && p[4]==0xed && p[5]==0xab) {
        /* GetDevices lists both0 and3, but only3 owns MPPT LOAD registers.
         * Explicit instance0 must fail closed, not silently select3. */
        const uint8_t r[]={9,0,0x19,0xed,0xab,1};fake_frame(r,sizeof(r));return 0;
    }
    if(p[0]==5 && size==6 && p[1]==3 && p[3]==0x19 && p[4]==0xed && (p[5]==0xab || p[5]==0xa8)) {
        if(fake.failure==FAKE_VALUE_TIMEOUT)return 0;
        uint8_t frame[210];unsigned at=0;
        /* Unrelated subscription records force DATA/LAST reassembly at both
         * MTU23 and77, while the requested register ends the same message. */
        for(unsigned i=0;i<20;i++) {
            const uint8_t extra[]={8,3,0x19,0xed,0xad,0x42,0,0};
            memcpy(frame+at,extra,sizeof(extra));at+=sizeof(extra);
        }
        uint8_t value=p[5]==0xab?fake.mode:((fake.mode&15)==4);
        if(p[5]==0xa8 && fake.failure==FAKE_OUTPUT_MISMATCH)value=0;
        uint8_t r[]={8,3,0x19,0xed,p[5],0x41,value};memcpy(frame+at,r,sizeof(r));at+=sizeof(r);
        fake_frame(frame,at);fake.readbacks++;return 0;
    }
    fake.invalid=14;return BLE_HS_EBADDATA;
}

static int replay(int failure,victron_result_code_t expected,unsigned stage,uint8_t bp_driver,uint16_t product)
{
    memset(&fake,0,sizeof(fake));fake.failure=failure;fake.mtu=23;fake.mode=0xa0;
    victron_request_t req={.address_type=-1,.instance=3,.pairing=true,.desired_value=4,.max_attempts=1,.driver=1};
    memcpy(req.mac,"C0:00:00:00:00:01",18);memcpy(req.pin,"000000",7);
    if(failure==FAKE_MPPT_WRONG_INSTANCE)req.instance=0;
    if(bp_driver) {
        fake.batteryprotect=true;fake.product=product;req.driver=bp_driver;req.instance=0;
        req.desired_value=(failure==FAKE_BATTERYPROTECT_OFF || failure==FAKE_BATTERYPROTECT_ALREADY_OFF)?4:3;
        fake.mode=req.desired_value==3?4:3;
        if(failure==FAKE_BATTERYPROTECT_ALREADY_ON || failure==FAKE_BATTERYPROTECT_ALREADY_OFF)fake.mode=req.desired_value;
    }
    if(failure==FAKE_GENERIC_READBACK) {
        fake.generic=true;req.driver=0;req.instance=0;req.generic_kind=4;req.pairing=false;
        memcpy(req.service_uuid,"306b0001-b081-4037-83dc-e59fcc3cdfd0",37);
        memcpy(req.characteristic_uuid,"306b0002-b081-4037-83dc-e59fcc3cdfd0",37);
        const uint8_t command[]={0x81,0x22,0x00,0xff};
        memcpy(req.value,command,sizeof(command));req.value_length=sizeof(command);
    }
    victron_result_t result;
    int status=victron_ble_run(&req,&result);
    CHECK(!fake.invalid);CHECK(result.code==expected);CHECK(!fake.connected);
    CHECK(result.stage==stage);
    CHECK(fake.disconnects==1);CHECK(result.target_seen);
    CHECK(fake.now<70000000);
    if(expected==VICTRON_RESULT_OK) {
        const bool already=failure==FAKE_BATTERYPROTECT_ALREADY_ON || failure==FAKE_BATTERYPROTECT_ALREADY_OFF;
        CHECK(status==ESP_OK && result.verified && result.changed==!already);
        if(fake.generic) {
            CHECK(fake.generic_writes==1 && fake.generic_size==req.value_length);
            CHECK(!memcmp(fake.generic_value,req.value,req.value_length));
            CHECK(fake.protocol_writes==0 && fake.mode_writes==0 && fake.cccd_writes==0);
            CHECK(fake.mtu_exchanges==0 && fake.security_checks==0);
        } else {
            if(fake.batteryprotect) {
                CHECK(result.initial_value==(already?req.desired_value:(req.desired_value==3?4:3)));
                CHECK(result.verified_value==req.desired_value && result.load_value==(req.desired_value==3));
                CHECK(fake.mode_writes==(already?0u:1u) && fake.mode==req.desired_value && fake.readbacks==5);
            } else {
                CHECK(result.initial_value==0xa0 && result.verified_value==0xa4 && result.load_value==0xa4);
                CHECK(fake.mode_writes==1 && fake.mode==0xa4 && fake.readbacks>=3);
            }
            CHECK(fake.cccd_writes==3 && fake.mtu_exchanges==1 && fake.security_checks);
            CHECK(fake.preferred_mtu==77);
            CHECK(fake.mtu==(failure==FAKE_MTU_UNSUPPORTED?23:77));
        }
    } else {
        CHECK(status!=ESP_OK && !result.verified);
        if(expected!=VICTRON_RESULT_VERIFY_FAILED)CHECK(fake.mode_writes==0);
        if(failure==FAKE_READ_TIMEOUT || failure==FAKE_MTU_TIMEOUT || failure==FAKE_MISSING_CREDIT)
            CHECK(result.detail==BLE_HS_ETIMEOUT);
        if(failure==FAKE_UNAUTHENTICATED || failure==FAKE_UNENCRYPTED)
            CHECK(result.detail==BLE_HS_EAUTHEN);
        if(failure==FAKE_BATTERYPROTECT_WRONG_PID)CHECK(fake.readbacks==1 && !result.changed);
        if(failure==FAKE_MPPT_WRONG_INSTANCE)CHECK(fake.readbacks==0 && fake.mode==0xa0 && !result.changed);
    }
    return 0;
}
int main(void)
{
    const struct {int failure;victron_result_code_t expected;unsigned stage;} cases[]={
        {FAKE_NONE,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_MTU_UNSUPPORTED,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_EARLY_CREDIT,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_READ_TIMEOUT,VICTRON_RESULT_INITIAL_READ_FAILED,BLE_STAGE_CONTROL_INFO},
        {FAKE_VALUE_TIMEOUT,VICTRON_RESULT_INITIAL_READ_FAILED,BLE_STAGE_INITIAL_READ},
        {FAKE_MISSING_CREDIT,VICTRON_RESULT_INITIAL_READ_FAILED,BLE_STAGE_TX_CREDIT},
        {FAKE_UNAUTHENTICATED,VICTRON_RESULT_SECURITY_FAILED,BLE_STAGE_SECURITY},
        {FAKE_UNENCRYPTED,VICTRON_RESULT_SECURITY_FAILED,BLE_STAGE_SECURITY},
        {FAKE_MTU_TIMEOUT,VICTRON_RESULT_GATT_NOT_FOUND,BLE_STAGE_MTU},
        {FAKE_OUTPUT_MISMATCH,VICTRON_RESULT_VERIFY_FAILED,BLE_STAGE_OUTPUT},
        {FAKE_SECURITY_IN_PROGRESS,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_ALREADY_AUTHENTICATED,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_MULTIPLE_SERVICES,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_SUBSCRIBE_NACK,VICTRON_RESULT_INITIAL_READ_FAILED,BLE_STAGE_SUBSCRIBE},
        {FAKE_MPPT_WRONG_INSTANCE,VICTRON_RESULT_INITIAL_READ_FAILED,BLE_STAGE_INITIAL_READ},
        {FAKE_BATTERYPROTECT_ON,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_BATTERYPROTECT_OFF,VICTRON_RESULT_OK,BLE_STAGE_OUTPUT},
        {FAKE_BATTERYPROTECT_WRONG_PID,VICTRON_RESULT_INITIAL_READ_FAILED,BLE_STAGE_INITIAL_READ},
        {FAKE_GENERIC_READBACK,VICTRON_RESULT_OK,BLE_STAGE_READBACK},
    };
    for(unsigned i=0;i<COUNT(cases);i++) {
        bool bp=cases[i].failure>=FAKE_BATTERYPROTECT_ON && cases[i].failure<=FAKE_BATTERYPROTECT_WRONG_PID;
        int line=replay(cases[i].failure,cases[i].expected,cases[i].stage,bp?3:0,
                        cases[i].failure==FAKE_BATTERYPROTECT_WRONG_PID?0xa3b2:0xa3b1);
        if(line){printf("FAIL session case %u at line %d (invalid=%d)\n",i,line,fake.invalid);return line;}
    }
    for(uint8_t driver=3;driver<=4;++driver) {
        const uint16_t product=driver==3?0xa3b1:0xa3b3;
        const int actions[]={FAKE_BATTERYPROTECT_ON,FAKE_BATTERYPROTECT_OFF,FAKE_BATTERYPROTECT_ALREADY_ON,FAKE_BATTERYPROTECT_ALREADY_OFF,FAKE_BATTERYPROTECT_OUTPUT_MISMATCH};
        for(unsigned i=0;i<COUNT(actions);++i) {
            int line=replay(actions[i],i==4?VICTRON_RESULT_VERIFY_FAILED:VICTRON_RESULT_OK,BLE_STAGE_OUTPUT,driver,product);
            if(line){printf("FAIL BP driver %u action %u line %d\n",driver,i,line);return line;}
        }
        const uint16_t wrong_products[]={0xa3b1,0xa3b2,0xa3b3,0x1234};
        for(unsigned i=0;i<COUNT(wrong_products);++i) if(wrong_products[i]!=product) {
            int line=replay(FAKE_BATTERYPROTECT_WRONG_PID,VICTRON_RESULT_INITIAL_READ_FAILED,BLE_STAGE_INITIAL_READ,driver,wrong_products[i]);
            if(line){printf("FAIL BP driver %u wrong PID %04x line %d\n",driver,wrong_products[i],line);return line;}
        }
        victron_request_t req={.address_type=-1,.instance=0,.pairing=true,.desired_value=3,.max_attempts=1,.driver=driver};
        memcpy(req.mac,"C0:00:00:00:00:01",18);memcpy(req.pin,"000000",7);
        CHECK(victron_ble_request_valid(&req));req.desired_value=4;CHECK(victron_ble_request_valid(&req));
        req.desired_value=0;CHECK(!victron_ble_request_valid(&req));req.desired_value=3;
        req.instance=3;CHECK(!victron_ble_request_valid(&req));req.instance=0;
        req.pairing=false;CHECK(!victron_ble_request_valid(&req));req.pairing=true;
        req.generic_kind=3;CHECK(!victron_ble_request_valid(&req));req.generic_kind=0;
        req.driver=5;CHECK(!victron_ble_request_valid(&req));
    }
    puts("PASS: production BLE session replay (SmartSolar, A3B1/A3B3 ON/OFF, unchanged mode, cross-model PID guards, output mismatch, request validation, Generic GATT, security and transport failures).");
    return 0;
}

#include "settings.h"
#include "esp_migration.h"
#ifdef ESP_MIGRATION_TO_NATIVE
#include <board.h>
#include <udrv_serial.h>
static const RuntimeConfig *saved;
static volatile uint8_t command;
static bool active=true,prepared=false,checked=false,attempted=false;
static char answer[1024];
static bool waitToken(const char *token,uint32_t timeout) {
    size_t used=0;answer[0]=0;const uint32_t start=millis();
    while(millis()-start<timeout) {
        while(Serial1.available()) {
            char c=Serial1.read();
            if(used+1<sizeof(answer)){answer[used++]=c;answer[used]=0;}
            if(strstr(answer,token))return true;
            if(strstr(answer,"\r\nERROR\r\n"))return false;
        }
        delay(1);
    }
    return false;
}
static bool at(const String &cmd,const char *token="\r\nOK\r\n",uint32_t timeout=5000) {
    while(Serial1.available())Serial1.read();Serial1.print(cmd);Serial1.print("\r\n");
    return waitToken(token,timeout);
}
static void uartBaud(uint32_t baud) {
    udrv_serial_deinit(SERIAL_UART1);
    udrv_serial_init(SERIAL_UART1,baud,SERIAL_WORD_LEN_8,SERIAL_STOP_BIT_1,SERIAL_PARITY_DISABLE,SERIAL_TWO_WIRE_NORMAL_MODE);
}
static String escaped(const char *s){String out;while(*s){if(*s=='"'||*s=='\\'||*s==',')out+='\\';out+=*s++;}return out;}
static int handle(SERIAL_PORT port,char *cmd,stParam *param) {
    (void)port;(void)cmd;
    if(param->argc!=1 || strlen(param->argv[0])!=1 || param->argv[0][0]<'1'||param->argv[0][0]>'4'||command)return AT_PARAM_ERROR;
    command=param->argv[0][0]-'0';return AT_OK;
}
void migrationBegin(const RuntimeConfig &config) {
    saved=&config;
    api.system.atMode.add("MIGRATE","1 prepare; 2 network check; 3 INSTALL; 4 native run","MIGRATE",handle,RAK_ATCMD_PERM_WRITE);
    Serial.println("Bootstrap waits for ATC+MIGRATE=1. No ESP writes without step 3.");
}
bool migrationService() {
    if(!active)return false;
    const uint8_t step=command;command=0;
    if(step==1 && !attempted) {
        setEspPowerMode(POWER_OFF);delay(300);uartBaud(115200);
        setEspPowerMode(POWER_ON);delay(1800);
        bool ok=at("AT")&&at("ATE0")&&at("AT+SYSSTORE=0");
        if(ok){Serial1.print("AT+UART_CUR=9600,8,1,0,0\r\n");Serial1.flush();delay(200);uartBaud(9600);ok=at("AT");}
        if(ok){ok=at("AT+GMR");Serial.println(answer);}
        if(ok){ok=at("AT+SYSFLASH?");Serial.println(answer);}
        ok=ok&&at("AT+CWMODE=2,0");
        String ap="AT+CWSAP=\""+escaped(saved->wifiApSsid)+"\",\""+escaped(saved->wifiApPassword)+"\",6,3,2,0";
        ok=ok&&at(ap);ap="";
        (void)at("AT+CIPSERVER=0");
        ok=ok&&at("AT+CIPMUX=0")&&at("AT+CIPRECVMODE=0");
        prepared=ok;checked=false;
        Serial.printf("Bootstrap prepared=%u; ESP NVS unchanged; connect laptop to saved WiFi.\r\n",ok);
    } else if(step==2 && prepared && !attempted) {
        bool ok=at("AT+CIPSTART=\"TCP\",\"192.168.4.2\",8765","\r\nOK\r\n",12000);
        const char request[]="GET /health HTTP/1.1\r\nHost: 192.168.4.2\r\nConnection: close\r\n\r\n";
        ok=ok&&at(String("AT+CIPSEND=")+String(sizeof(request)-1),">");
        if(ok){Serial1.print(request);ok=waitToken("LORABLE_OTA_READY",15000);}
        Serial.println(answer);(void)at("AT+CIPCLOSE");
        checked=ok;Serial.printf("Bootstrap network checked=%u\r\n",ok);
    } else if(step==3 && prepared && checked && !attempted) {
        const char url[]="http://192.168.4.2:8765/esp.packed";
        if(at(String("AT+USEROTA=")+String(sizeof(url)-1),">")) {
            attempted=true;Serial1.print(url);Serial1.flush();
            Serial.println("ESP installation requested. Keep power connected. Do not repeat.");
        } else Serial.println("USEROTA handshake failed; no URL submitted.");
    } else if(step==4) {
        uartBaud(115200);active=false;Serial.println("Native companion mode selected.");return false;
    } else if(step) Serial.println("Bootstrap step refused: check required preceding steps.");
    // Forward only bounded, printable diagnostics. Never send commands/credentials.
    unsigned n=0;while(attempted&&Serial1.available()&&n++<128){int c=Serial1.read();if(c==10||c==13||(c>=32&&c<127))Serial.write((uint8_t)c);}
    delay(5);return true;
}
#endif

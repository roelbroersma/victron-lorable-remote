// Temporary first-install host, RAK11162/RUI 4.2.4. No project settings writes.
// No LoRa joins, relay actions, erase-all, bootloader or partition changes.
#include <board.h>
#include <udrv_serial.h>
#include <ctype.h>

static char eui[17], token[33], password[25], ssid[33], host[16], url[112];
static char response[1536], nativeVersion[32];
static volatile unsigned pending;
static unsigned subnet, serverPort;
static bool prepared, checked, requested;

static bool hexString(const char *s, size_t n) {
  if(strlen(s)!=n)return false;
  for(size_t i=0;i<n;i++)if(!isxdigit((unsigned char)s[i]))return false;
  return true;
}
static bool decimal(const char *s,unsigned low,unsigned high,unsigned &out){
  if(!*s)return false;unsigned value=0;
  for(;*s;s++){if(*s<'0'||*s>'9'||value>65535)return false;value=value*10+*s-'0';}
  if(value<low||value>high)return false;out=value;return true;
}
static void baud(uint32_t value){
  udrv_serial_deinit(SERIAL_UART1);
  udrv_serial_init(SERIAL_UART1,value,SERIAL_WORD_LEN_8,SERIAL_STOP_BIT_1,SERIAL_PARITY_DISABLE,SERIAL_TWO_WIRE_NORMAL_MODE);
}
static bool waitFor(const char *wanted,uint32_t timeout){
  size_t used=0;response[0]=0;uint32_t start=millis();
  while(millis()-start<timeout){
    while(Serial1.available()){
      char c=Serial1.read();
      if(used+1>=sizeof(response)){memmove(response,response+512,used-512);used-=512;}
      response[used++]=c;response[used]=0;
      if(strstr(response,wanted))return true;
      if(strstr(response,"\r\nERROR\r\n"))return false;
    }
    delay(1);
  }
  return false;
}
static bool at(const String &value,const char *wanted="\r\nOK\r\n",uint32_t timeout=5000){
  while(Serial1.available())Serial1.read();
  Serial1.print(value);Serial1.print("\r\n");return waitFor(wanted,timeout);
}
static uint16_t crc16(const char *p,size_t n){
  uint16_t c=0xffff;while(n--){c^=(uint16_t)(uint8_t)*p++<<8;
    for(unsigned b=0;b<8;b++)c=c&0x8000?(c<<1)^0x1021:c<<1;}return c;
}
static bool readNative(uint32_t timeout){
  baud(115200);char line[256];size_t used=0;uint32_t start=millis();nativeVersion[0]=0;
  while(millis()-start<timeout){
    while(Serial1.available()){
      char c=Serial1.read();if(c=='@'){used=0;line[used++]=c;continue;}
      if(!used)continue;
      if(c=='\n'){
        line[used]=0;while(used&&line[used-1]=='\r')line[--used]=0;
        char *last=strrchr(line,'|');
        if(last&&strlen(last+1)==4&&hexString(last+1,4)&&
           crc16(line+1,last-line-1)==strtoul(last+1,NULL,16)&&
           !strncmp(line,"@1|READY|0000|",14)){
          *last=0;char decoded[160];size_t count=0;uint32_t acc=0;unsigned bits=0;bool valid=true;
          const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
          for(const char *p=line+14;*p&&*p!='=';p++){
            const char *v=strchr(alphabet,*p);if(!v){valid=false;break;}
            acc=(acc<<6)|(v-alphabet);bits+=6;
            if(bits>=8){bits-=8;if(count+1>=sizeof(decoded)){valid=false;break;}decoded[count++]=(acc>>bits)&255;}
          }
          decoded[count]=0;char *fw=strstr(decoded,"&fw=");
          if(valid&&!strncmp(decoded,"role=esp8684&proto=1&",20)&&fw){
            fw+=4;char *end=strchr(fw,'&');if(end)*end=0;
            size_t n=strlen(fw);bool ok=n>0&&n<sizeof(nativeVersion);
            for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)fw[i])&&fw[i]!='.'&&fw[i]!='-')ok=false;
            if(ok){strcpy(nativeVersion,fw);return true;}
          }
        }
        used=0;
      }else if(used+1<sizeof(line))line[used++]=c;else used=0;
    }
    delay(1);
  }
  return false;
}
static int command(SERIAL_PORT,char *,stParam *p){
  if(pending||p->argc<1)return AT_PARAM_ERROR;
  unsigned step=0;if(!decimal(p->argv[0],0,4,step))return AT_PARAM_ERROR;
  if(step==0&&p->argc==1){
    Serial.printf("LBR_SETUP_V1 eui=%s prepared=%u checked=%u requested=%u\r\n",eui,prepared,checked,requested);return AT_OK;
  }
  if(step==1&&p->argc==4&&!requested&&hexString(p->argv[1],32)&&hexString(p->argv[3],24)&&decimal(p->argv[2],20,250,subnet)){
    strcpy(token,p->argv[1]);strcpy(password,p->argv[3]);pending=1;return AT_OK;
  }
  unsigned last=0;
  if(step==2&&p->argc==3&&prepared&&!requested&&decimal(p->argv[1],2,254,last)&&decimal(p->argv[2],49152,65535,serverPort)){
    snprintf(host,sizeof(host),"192.168.%u.%u",subnet,last);pending=2;return AT_OK;
  }
  if(step==3&&p->argc==1&&checked&&!requested){pending=3;return AT_OK;}
  if(step==4&&p->argc==1){pending=4;return AT_OK;}
  return AT_PARAM_ERROR;
}
void setup(){
  Serial.begin(115200,RAK_AT_MODE);Serial1.begin(115200,RAK_CUSTOM_MODE);
  setCurrentATMode(LORA_AT_MODE);
  digitalWrite(WB_IO4,LOW);pinMode(WB_IO4,OUTPUT);digitalWrite(WB_IO4,LOW);
  if(api.system.lpm.get())api.system.lpm.set(0);
  uint8_t bytes[8];api.lorawan.deui.get(bytes,8);
  for(unsigned i=0;i<8;i++)snprintf(eui+2*i,3,"%02X",bytes[i]);
  setEspPowerMode(POWER_OFF);delay(300);setEspPowerMode(POWER_ON);
  api.system.atMode.add("SETUP","LoRaBLE first-install V1","SETUP",command,RAK_ATCMD_PERM_WRITE);
  Serial.printf("LBR_SETUP_V1 eui=%s prepared=0 checked=0 requested=0\r\n",eui);
}
void loop(){
  unsigned step=pending;pending=0;
  if(step==1){
    prepared=checked=false;
    // Recognize a converted device without issuing factory AT commands to it.
    if(readNative(4500)){Serial.printf("LBR_SETUP_NATIVE fw=%s\r\n",nativeVersion);return;}
    bool ok=at("AT")&&at("ATE0")&&at("AT+SYSSTORE=0");
    if(ok){Serial1.print("AT+UART_CUR=9600,8,1,0,0\r\n");Serial1.flush();delay(200);baud(9600);ok=at("AT");}
    ok=ok&&at("AT+GMR");
    // Exact supported factory family; unknown firmware must not receive USEROTA.
    ok=ok&&strstr(response,"AT version:3.3.0.0")&&strstr(response,"ESP32C2-2MB")&&strstr(response,"v5.0.6");
    ok=ok&&at("AT+SYSFLASH?")&&strstr(response,"mfg_nvs");
    if(!ok){Serial.println("LBR_SETUP_ERROR unsupported_factory");return;}
    snprintf(ssid,sizeof(ssid),"LoRaBLE-Setup-%.6s-%.4s",eui+10,token);
    String ap="192.168."+String(subnet)+".1";
    ok=at("AT+CWMODE=2,0")&&at("AT+CWDHCP=0,2")&&
       at("AT+CIPAP=\""+ap+"\",\""+ap+"\",\"255.255.255.0\"")&&
       at("AT+CWDHCPS=1,10,\"192.168."+String(subnet)+".2\",\"192.168."+String(subnet)+".20\"")&&
       at("AT+CWDHCP=1,2")&&
       at("AT+CWSAP=\""+String(ssid)+"\",\""+String(password)+"\",6,3,1,0");
    (void)at("AT+CIPSERVER=0");
    ok=ok&&at("AT+CIPMUX=0")&&at("AT+CIPRECVMODE=0");
    memset(password,0,sizeof(password));prepared=ok;
    if(ok)Serial.printf("LBR_SETUP_AP ssid=%s ip=%s\r\n",ssid,ap.c_str());
    else Serial.println("LBR_SETUP_ERROR ap_prepare");
  }else if(step==2){
    checked=false;
    bool ok=at("AT+CIPSTART=\"TCP\",\""+String(host)+"\","+String(serverPort),"\r\nOK\r\n",12000);
    String request="GET /"+String(token)+"/health HTTP/1.1\r\nHost: "+String(host)+"\r\nConnection: close\r\n\r\n";
    ok=ok&&at("AT+CIPSEND="+String(request.length()),">");
    if(ok){Serial1.print(request);ok=waitFor(("LORABLE_OTA_READY:"+String(token)).c_str(),15000);}
    (void)at("AT+CIPCLOSE");checked=ok;
    snprintf(url,sizeof(url),"http://%s:%u/%s/firmware",host,serverPort,token);
    Serial.println(ok?"LBR_SETUP_CHECKED":"LBR_SETUP_ERROR local_server");
  }else if(step==3){
    if(!at("AT+USEROTA="+String(strlen(url)),">")){Serial.println("LBR_SETUP_ERROR ota_handshake");return;}
    requested=true;Serial1.print(url);Serial1.flush();Serial.println("LBR_SETUP_DOWNLOADING");
    bool ok=waitFor("\r\nOK\r\n",180000);
    Serial.println(ok?"LBR_SETUP_DOWNLOADED":"LBR_SETUP_OTA_UNCONFIRMED");
    // No reset, power-cycle or automatic retry on uncertain download outcomes.
    if(readNative(45000))Serial.printf("LBR_SETUP_NATIVE fw=%s\r\n",nativeVersion);
    else Serial.println("LBR_SETUP_ERROR native_not_seen");
  }else if(step==4){
    if(readNative(12000))Serial.printf("LBR_SETUP_NATIVE fw=%s\r\n",nativeVersion);
    else Serial.println("LBR_SETUP_ERROR native_not_seen");
  }
  delay(5);
}

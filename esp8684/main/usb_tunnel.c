// Local USB-to-portal transport. No remote addresses or credentials are accepted.
// The same authenticated HTTP updater is used over WiFi and the internal UART.
#include "usb_tunnel.h"
#include "uart_link.h"
#include "portal.h"
#include "bundle_update.h"
#include "update_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
static atomic_bool active,overflow;
static StreamBufferHandle_t incoming;
static bool checked_transport;
static atomic_uint counts[6]; // mode, serial bytes, TCP bytes, reply bytes, blocks, rejects
void usb_tunnel_diagnostics(unsigned values[6]){for(unsigned i=0;i<6;i++)values[i]=atomic_load(&counts[i]);}
static void byte_received(uint8_t b){if(xStreamBufferSend(incoming,&b,1,0)!=1)atomic_store(&overflow,true);}
bool usb_tunnel_busy(void){return atomic_load(&active);}
static void tunnel(void *unused){
 (void)unused;int sock=-1;bool raw=false;
 // Stop-and-wait permits only one <=528-byte packet, plus bounded retries.
 // Reserving8KB here starves the install worker of its6KB stack after upload.
 if(!incoming)incoming=xStreamBufferCreate(2048,1);
 if(!incoming)goto done;
 xStreamBufferReset(incoming);atomic_store(&overflow,false);
 sock=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);if(sock<0)goto done;
 struct timeval timeout={.tv_sec=3};setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(80),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 if(connect(sock,(struct sockaddr*)&address,sizeof(address)))goto done;
 // Arm the byte receiver BEFORE advertising readiness. The PC may answer as
 // soon as STM sees the newline; framed-send pacing previously left a window
 // in which the first HTTP bytes were consumed by the normal frame parser.
 char ready[96];
 if(protocol_encode(ready,sizeof(ready),"USB_READY",0,checked_transport?"2":"1")!=ESP_OK)goto done;
 uart_link_raw_mode(byte_received);raw=true;
 if(uart_link_raw_write(ready,strlen(ready))!=ESP_OK)goto done;
 int64_t last=esp_timer_get_time(),deadline=last+600000000;
 uint8_t buffer[512];
 uint8_t packet[528];size_t packet_used=0;uint32_t next_sequence=0;
 char header[512]={0};size_t header_used=0,response_bytes=0,expected_response=0;
 while(!atomic_load(&overflow)&&esp_timer_get_time()<deadline&&esp_timer_get_time()-last<15000000){
  size_t n=xStreamBufferReceive(incoming,buffer,sizeof(buffer),0);
  if(n){
   atomic_fetch_add(&counts[1],n);
   if(!checked_transport){size_t off=0;while(off<n){int sent=send(sock,buffer+off,n-off,0);if(sent<=0)goto done;off+=sent;}}
   else for(size_t i=0;i<n;i++){
    packet[packet_used++]=buffer[i];
    if(packet_used>=4&&lbr_u32(packet)!=0x3255424cu){memmove(packet,packet+1,--packet_used);continue;}
    if(packet_used<16)continue;
    uint32_t seq=lbr_u32(packet+4),length=lbr_u32(packet+8);
    if(!length||length>512){memmove(packet,packet+1,--packet_used);continue;}
    if(packet_used<16+length)continue;
    uint32_t crc=lbr_crc(lbr_crc(0,packet,12),packet+16,length),code=0;
    if(crc!=lbr_u32(packet+12))code=1;
    else if(seq==next_sequence){
     size_t off=0;while(off<length){int sent=send(sock,packet+16+off,length-off,0);if(sent<=0)goto done;off+=sent;}
     next_sequence++;atomic_fetch_add(&counts[2],length);atomic_fetch_add(&counts[4],1);
    }else if(!next_sequence||seq!=next_sequence-1)code=2;
    uint32_t ack[4]={0x3241424cu,seq,code,0};ack[3]=lbr_crc(0,(uint8_t*)ack,12);
    if(code)atomic_fetch_add(&counts[5],1);
    if(uart_link_raw_write(ack,sizeof(ack))!=ESP_OK)goto done;
    packet_used=0;
   }
   last=esp_timer_get_time();
  }
  else if(checked_transport&&packet_used&&esp_timer_get_time()-last>500000)packet_used=0;
  fd_set read_set;FD_ZERO(&read_set);FD_SET(sock,&read_set);struct timeval wait={.tv_usec=10000};
  if(select(sock+1,&read_set,NULL,NULL,&wait)>0){
   int count=recv(sock,buffer,sizeof(buffer),0);if(count<=0)break;
   atomic_fetch_add(&counts[3],count);
   for(int i=0;i<count&&!expected_response;i++){
    if(header_used+1>=sizeof(header))goto done;
    header[header_used++]=(char)buffer[i];header[header_used]=0;
    if(header_used>=4&&!memcmp(header+header_used-4,"\r\n\r\n",4)){
     const char *length=strstr(header,"\r\nContent-Length: ");
     if(!length)goto done;
     char *end;unsigned long body=strtoul(length+18,&end,10);
     if(end==length+18||strncmp(end,"\r\n",2)||body>32768)goto done;
     expected_response=header_used+body;
    }
   }
   // RUI application RX ring is 512 bytes: leave its loop time to forward.
   // FreeRTOS uses 10-ms ticks: 5 ms rounds to zero, losing the intended pacing.
   for(int i=0;i<count;i+=32){int bytes=count-i>32?32:count-i;if(uart_link_raw_write(buffer+i,bytes)!=ESP_OK)goto done;vTaskDelay(pdMS_TO_TICKS(20));}
   response_bytes+=(size_t)count;
   // HTTPD may retain the socket despite the request's Connection: close.
   // End the serial exchange immediately after its complete bounded response.
   if(expected_response&&response_bytes>=expected_response)break;
   last=esp_timer_get_time();
  }
 }
done:
 if(sock>=0)close(sock);
 if(raw){uart_link_raw_write("~LBR-END~",9);uart_link_raw_mode(NULL);}
 else uart_link_send("USB_READY",0,"0");
 atomic_store(&active,false);vTaskDelete(NULL);
}
bool usb_tunnel_receive(const protocol_frame_t *f){
 if(strcmp(f->type,"USB_OPEN"))return false;
 bool expected=false;
 if(bundle_busy()||!portal_running()||!atomic_compare_exchange_strong(&active,&expected,true))return true;
 checked_transport=!strcmp(f->payload,"2");
 for(unsigned i=0;i<6;i++)atomic_store(&counts[i],0);
 atomic_store(&counts[0],checked_transport?2:1);
 if(xTaskCreate(tunnel,"usb_portal",6144,NULL,5,NULL)!=pdPASS){atomic_store(&active,false);uart_link_send("USB_READY",0,"0");}
 return true;
}

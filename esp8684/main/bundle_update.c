#include "bundle_update.h"
#include "update_bundle.h"
#include "uart_link.h"
#include "portal.h"
#include "usb_tunnel.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "esp_flash_internal.h"
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_md5.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>

static atomic_bool busy, flash_ready;
static atomic_uint phase, progress, error_code;
static uint8_t bundle_header[LBR_HEADER_SIZE];
static const esp_partition_t *application,*storage;
static QueueHandle_t requests;
static uint8_t raw_packet[16];
static unsigned raw_used;
static void dry_run(void *arg);
static void install(void *arg);
static void staged_job(void *arg);
static size_t running_image_end;
static bool layout(void){
 application=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,NULL);
 storage=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x22,"storage");
 if(!application||application->address!=0xd0000||application->size!=0x130000||!storage||storage->address!=0x2a000||storage->size!=0xa6000)return false;
 esp_image_metadata_t meta={0};esp_partition_pos_t pos={.offset=application->address,.size=application->size};
 if(esp_image_verify(ESP_IMAGE_VERIFY_SILENT,&pos,&meta)!=ESP_OK||meta.image_len>LBR_ESP_APP_MAX)return false;
 running_image_end=(meta.image_len+4095u)&~4095u;
 return running_image_end<=LBR_STAGE_OFFSET;
}
// IDF protects the entire running partition, including its unused tail. Narrowly
// unlock only a checked operation in that tail, never executable bytes. Keep the
// default bootloader/table/application protection enabled everywhere else.
static esp_err_t stage_operation(size_t offset,const void *data,size_t length){
 if(!busy||!application||!running_image_end||offset<running_image_end||offset<LBR_STAGE_OFFSET||
    !length||offset>application->size||length>application->size-offset)return ESP_ERR_INVALID_ARG;
 if(!data&&((offset|length)&4095u))return ESP_ERR_INVALID_ARG;
 esp_err_t result=esp_flash_set_dangerous_write_protection(esp_flash_default_chip,false);
 if(result!=ESP_OK)return result;
 result=data?esp_partition_write(application,offset,data,length):esp_partition_erase_range(application,offset,length);
 esp_err_t restored=esp_flash_set_dangerous_write_protection(esp_flash_default_chip,true);
 return result==ESP_OK?restored:result;
}
static esp_err_t reply(httpd_req_t *r,const char *status,const char *code){
 httpd_resp_set_status(r,status);httpd_resp_set_type(r,"application/json");
 char b[128];snprintf(b,sizeof(b),"{\"ok\":false,\"code\":\"%s\"}",code);return httpd_resp_sendstr(r,b);
}
static bool receive(httpd_req_t *r,uint8_t *p,size_t n){while(n){int got=httpd_req_recv(r,(char*)p,n);if(got<=0)return false;p+=got;n-=got;}return true;}
static bool hash_flash(const esp_partition_t *part,size_t offset,size_t n,const uint8_t expected[32]){
 uint8_t b[1024],hash[32];mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);
 bool ok=mbedtls_sha256_starts(&ctx,0)==0;
 while(ok&&n){size_t count=n>sizeof(b)?sizeof(b):n;ok=esp_partition_read(part,offset,b,count)==ESP_OK&&mbedtls_sha256_update(&ctx,b,count)==0;offset+=count;n-=count;}
 ok=ok&&mbedtls_sha256_finish(&ctx,hash)==0&&!memcmp(hash,expected,32);mbedtls_sha256_free(&ctx);return ok;
}
static bool esp_header_valid(const uint8_t *h,uint32_t size){
 return !memcmp(h,"ESP\0",4)&&h[4]==2&&h[5]==1&&!h[6]&&!h[7]&&!memcmp(h+8,"LoRaBLE-C2-26M-",14)&&
  lbr_u32(h+40)==size-88&&!lbr_u32(h+76)&&!lbr_u32(h+80)&&lbr_crc(0,h,84)==lbr_u32(h+84);
}
esp_err_t bundle_upload(httpd_req_t *r){
 bool expected=false;if(!atomic_compare_exchange_strong(&busy,&expected,true))return reply(r,"409 Conflict","update_busy");
 TaskHandle_t job=NULL;
 const char *failure="incompatible_firmware";uint8_t h[LBR_HEADER_SIZE],packed[88],b[1024],digest[32];
 char dry_header[4]={0};bool dry=httpd_req_get_hdr_value_str(r,"X-LoRaBLE-Dry-Run",dry_header,sizeof(dry_header))==ESP_OK&&!strcmp(dry_header,"1");
 mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
 if(!layout()||r->content_len<LBR_HEADER_SIZE||r->content_len>LBR_HEADER_SIZE+LBR_STM_MAX+LBR_ESP_MAX)goto failed;
 if(!receive(r,h,sizeof(h))||!lbr_header_valid(h,r->content_len))goto failed;
 // Reserve installation resources BEFORE erasing/staging or accepting a long
 // upload. The worker sleeps until both images have passed all checksums.
 if(xTaskCreate(staged_job,"update_job",6144,(void*)(uintptr_t)(dry?1:0),5,&job)!=pdPASS){failure="update_memory";goto failed;}
 // Do not start while a Bluetooth action is using the radio. Portal HTTP is only
 // reachable in WiFi mode; busy prevents manager transitions from this point.
 uart_link_send("OTA_STATE",0,"1");atomic_store(&phase,1);atomic_store(&progress,0);atomic_store(&error_code,0);
 uint32_t stm=lbr_u32(h+48),esp=lbr_u32(h+52);
 failure="flash_write_failed";
 // Metadata remains erased until BOTH components pass their full checksums.
 if(stage_operation(LBR_STAGE_OFFSET,NULL,0x1000+((stm+4095)&~4095u))!=ESP_OK||
    esp_partition_erase_range(storage,0,(esp+4095)&~4095u)!=ESP_OK||
    esp_partition_erase_range(storage,LBR_META_OFFSET,4096)!=ESP_OK)goto failed;
 for(uint32_t off=0;off<stm;){size_t n=stm-off>sizeof(b)?sizeof(b):stm-off;
  if(!receive(r,b,n)){failure="upload_interrupted";goto failed;}
  if(!off){uint32_t sp=lbr_u32(b),pc=lbr_u32(b+4);if(sp<0x20000000||sp>0x20010000||sp%8||!(pc&1)||pc<0x08006000||pc>=0x08006000+stm){failure="wrong_control_firmware";goto failed;}}
  if(stage_operation(LBR_STAGE_DATA+off,b,n)!=ESP_OK)goto failed;
  off+=n;
 }
 if(!hash_flash(application,LBR_STAGE_DATA,stm,h+64)){failure="control_checksum_failed";goto failed;}
 if(!receive(r,packed,88)||!esp_header_valid(packed,esp)){failure="wrong_connectivity_firmware";goto failed;}
 if(mbedtls_sha256_starts(&hash,0)||mbedtls_sha256_update(&hash,packed,88))goto failed;
 md5_context_t md5;esp_rom_md5_init(&md5);
 for(uint32_t off=88;off<esp;){size_t n=esp-off>sizeof(b)?sizeof(b):esp-off;
  if(!receive(r,b,n)){failure="upload_interrupted";goto failed;}
  if(esp_partition_write(storage,off,b,n)!=ESP_OK||mbedtls_sha256_update(&hash,b,n))goto failed;
  esp_rom_md5_update(&md5,b,n);off+=n;
 }
 if(mbedtls_sha256_finish(&hash,digest)||memcmp(digest,h+96,32)){failure="connectivity_checksum_failed";goto failed;}
 esp_rom_md5_final(digest,&md5);if(memcmp(digest,packed+44,16)){failure="compressed_checksum_failed";goto failed;}
 // Verify staged compressed bytes from flash with the withheld header.
 if(mbedtls_sha256_starts(&hash,0)||mbedtls_sha256_update(&hash,packed,88))goto failed;
 for(uint32_t off=88;off<esp;){size_t n=esp-off>sizeof(b)?sizeof(b):esp-off;
  if(esp_partition_read(storage,off,b,n)!=ESP_OK||mbedtls_sha256_update(&hash,b,n))goto failed;
  off+=n;}
 if(mbedtls_sha256_finish(&hash,digest)||memcmp(digest,h+96,32)){failure="flash_verify_failed";goto failed;}
 if(dry){
  memcpy(bundle_header,h,sizeof(h));mbedtls_sha256_free(&hash);
  httpd_resp_set_type(r,"application/json");httpd_resp_sendstr(r,"{\"ok\":true,\"code\":\"checking\"}");
  xTaskNotifyGive(job);
  return ESP_OK;
 }
 if(esp_partition_write(storage,LBR_META_OFFSET+512,packed,88)!=ESP_OK||
    esp_partition_write(storage,LBR_META_OFFSET,h,sizeof(h))!=ESP_OK)goto failed;
 memcpy(bundle_header,h,sizeof(h));mbedtls_sha256_free(&hash);atomic_store(&phase,3);
 httpd_resp_set_type(r,"application/json");httpd_resp_sendstr(r,"{\"ok\":true,\"code\":\"installing\"}");
 xTaskNotifyGive(job);
 return ESP_OK;
failed:
 if(job)vTaskDelete(job); // Not notified: no active-image operation has begun.
 // Stable diagnostic numbers, containing no credentials or payload data.
 if(!strcmp(failure,"upload_interrupted"))atomic_store(&error_code,40);
 else if(!strcmp(failure,"control_checksum_failed"))atomic_store(&error_code,41);
 else if(!strcmp(failure,"connectivity_checksum_failed"))atomic_store(&error_code,42);
 else if(!strcmp(failure,"wrong_connectivity_firmware"))atomic_store(&error_code,43);
 else if(!strcmp(failure,"flash_write_failed"))atomic_store(&error_code,44);
 else if(!strcmp(failure,"update_memory"))atomic_store(&error_code,30);
 else atomic_store(&error_code,45);
 mbedtls_sha256_free(&hash);atomic_store(&phase,5);atomic_store(&busy,false);uart_link_send("OTA_STATE",0,"0");return reply(r,"400 Bad Request",failure);
}
bool bundle_busy(void){return atomic_load(&busy);}
esp_err_t bundle_status(httpd_req_t *r){
 unsigned usb[6];usb_tunnel_diagnostics(usb);
 char b[384];snprintf(b,sizeof(b),"{\"phase\":%u,\"progress\":%u,\"error\":%u,\"version\":\"%s\",\"heap_free\":%u,\"heap_largest\":%u,\"heap_min\":%u,\"usb\":[%u,%u,%u,%u,%u,%u]}",atomic_load(&phase),atomic_load(&progress),atomic_load(&error_code),esp_app_get_description()->version,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),usb[0],usb[1],usb[2],usb[3],usb[4],usb[5]);
 httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,b);
}
bool bundle_receive_frame(const protocol_frame_t *f){
 if(strcmp(f->type,"UPDATE_READY"))return false;
 if(!strcmp(f->payload,"1"))atomic_store(&flash_ready,true);
 return true;
}
static void raw_byte(uint8_t b){
 // Sliding synchronization accepts only complete CRC-protected 16-byte requests.
 if(raw_used<16)raw_packet[raw_used++]=b;
 if(raw_used==16){uint32_t m=lbr_u32(raw_packet);
  if((m==0x3152424c||m==0x314b424c||m==0x3145424c||m==0x3156424c)&&lbr_crc(0,raw_packet,12)==lbr_u32(raw_packet+12)){
   (void)xQueueSend(requests,raw_packet,0);raw_used=0;
  }else{memmove(raw_packet,raw_packet+1,15);raw_used=15;}
 }
}
static bool run_control(bool dry){
 const uint32_t size=lbr_u32(bundle_header+48),crc=lbr_u32(bundle_header+56);
 char request[80];snprintf(request,sizeof(request),"%lu,%lu,%u",(unsigned long)size,(unsigned long)crc,dry?0:1);
 atomic_store(&flash_ready,false);
 for(unsigned i=0;i<6&&!atomic_load(&flash_ready);i++){
  uart_link_send("UPDATE_PREPARE",0,request);vTaskDelay(pdMS_TO_TICKS(1500));
 }
 if(!atomic_load(&flash_ready)){atomic_store(&error_code,20);return false;}
 if(!requests)requests=xQueueCreate(4,16);
 if(!requests){atomic_store(&error_code,21);return false;}
 xQueueReset(requests);
 raw_used=0;
 // COMMIT merely enters the validated RAM program. The RAM program reads the
 // entire staged image before permitting writes; dry mode never erases anything.
 uart_link_send("UPDATE_EXEC",0,request);uart_link_raw_mode(raw_byte);
 bool success=false;int64_t deadline=esp_timer_get_time()+180000000;
 uint32_t packet[4];uint8_t data[2048];
 while(esp_timer_get_time()<deadline){
  if(xQueueReceive(requests,packet,pdMS_TO_TICKS(5000))!=pdTRUE)continue;
  if(packet[0]==0x3152424c){
   uint32_t off=packet[1],n=packet[2];if(off>=size||!n||n>sizeof(data)||n>size-off){atomic_store(&error_code,22);break;}
   if(esp_partition_read(application,LBR_STAGE_DATA+off,data,n)!=ESP_OK){atomic_store(&error_code,23);break;}
   uint32_t response[4]={0x3144424c,off,n,lbr_crc(0,data,n)};
   if(uart_link_raw_write(response,16)!=ESP_OK||uart_link_raw_write(data,n)!=ESP_OK){atomic_store(&error_code,24);break;}
   atomic_store(&progress,(off+n)*100/size);
  }else if(packet[0]==(dry?0x3156424cu:0x314b424cu)&&packet[1]==crc&&packet[2]==(dry?0u:1u)){
   if(dry){success=true;break;}
   uint32_t control_done=0xfffffffeu;
   success=esp_partition_write(storage,LBR_META_OFFSET+256,&control_done,4)==ESP_OK;
   if(!success)atomic_store(&error_code,25);
   break;
  }else if(packet[0]==0x3145424c){atomic_store(&error_code,100+packet[1]);break;}
 }
 uart_link_raw_mode(NULL);
 // Keep the queue allocated: the UART receive task may finish one callback which
 // already observed raw mode. One update per boot; no dangling queue pointer.
 if(!success&&!atomic_load(&error_code))atomic_store(&error_code,26);
 return success;
}
static void discard_byte(uint8_t b){(void)b;}
static void release_control(void){
 uint32_t reply[4]={0x3141424c,lbr_u32(bundle_header+56),1,0};reply[3]=lbr_crc(0,(uint8_t*)reply,12);
 uart_link_raw_mode(discard_byte);
 for(unsigned i=0;i<4;i++){uart_link_raw_write(reply,16);vTaskDelay(pdMS_TO_TICKS(250));}
 uart_link_raw_mode(NULL);
}
static bool finish_connectivity(void){
 if(hash_flash(application,0,lbr_u32(bundle_header+128),bundle_header+132)){
  uint32_t complete=0;
  if(esp_partition_write(storage,LBR_META_OFFSET+256,&complete,4)!=ESP_OK){atomic_store(&error_code,25);release_control();return false;}
  atomic_store(&phase,4);release_control();return true;
 }
 uint8_t packed[88],data[1024],digest[32];uint32_t size=lbr_u32(bundle_header+52);
 if(esp_partition_read(storage,LBR_META_OFFSET+512,packed,88)!=ESP_OK||!esp_header_valid(packed,size)){atomic_store(&error_code,31);release_control();return false;}
 mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
 bool ok=mbedtls_sha256_starts(&hash,0)==0&&mbedtls_sha256_update(&hash,packed,88)==0;
 for(uint32_t off=88;ok&&off<size;){size_t n=size-off>sizeof(data)?sizeof(data):size-off;ok=esp_partition_read(storage,off,data,n)==ESP_OK&&mbedtls_sha256_update(&hash,data,n)==0;off+=n;}
 ok=ok&&mbedtls_sha256_finish(&hash,digest)==0&&!memcmp(digest,bundle_header+96,32);mbedtls_sha256_free(&hash);
 if(!ok){atomic_store(&error_code,31);release_control();return false;}
 // Control is already verified and holding ESP power. Stock bootloader may now
 // erase the complete application slot, including the no-longer-needed stage.
 atomic_store(&phase,2);
 if(esp_partition_write(storage,0,packed,88)!=ESP_OK){atomic_store(&error_code,32);release_control();return false;}
 vTaskDelay(pdMS_TO_TICKS(1000));esp_restart();return true;
}
static void install(void *arg){
 (void)arg;
 for(unsigned i=0;i<30&&usb_tunnel_busy();i++)vTaskDelay(pdMS_TO_TICKS(500));
 if(usb_tunnel_busy())atomic_store(&error_code,33);
 bool ok=!usb_tunnel_busy()&&run_control(false);
 if(ok)ok=finish_connectivity();else release_control();
 atomic_store(&phase,ok?4:5);atomic_store(&busy,false);uart_link_send("OTA_STATE",0,"0");vTaskDelete(NULL);
}
static void resume(void *arg){
 (void)arg;
 if(!layout()||esp_partition_read(storage,LBR_META_OFFSET,bundle_header,sizeof(bundle_header))!=ESP_OK)goto done;
 size_t total=256u+(size_t)lbr_u32(bundle_header+48)+lbr_u32(bundle_header+52);
 if(!lbr_header_valid(bundle_header,total))goto done;
 uint32_t complete;if(esp_partition_read(storage,LBR_META_OFFSET+256,&complete,4)!=ESP_OK)goto done;
 if(!complete){if(hash_flash(application,0,lbr_u32(bundle_header+128),bundle_header+132))atomic_store(&phase,4);goto done;}
 if(complete==0xfffffffeu){
  atomic_store(&busy,true);bool ok=finish_connectivity();atomic_store(&phase,ok?4:5);atomic_store(&busy,false);goto done;
 }
 if(complete!=0xffffffffu){atomic_store(&phase,5);atomic_store(&error_code,27);goto done;}
 if(!hash_flash(application,LBR_STAGE_DATA,lbr_u32(bundle_header+48),bundle_header+64)){atomic_store(&phase,5);atomic_store(&error_code,28);goto done;}
 for(unsigned i=0;i<90&&!portal_running();i++)vTaskDelay(pdMS_TO_TICKS(1000));
 if(!portal_running()){atomic_store(&phase,5);atomic_store(&error_code,29);goto done;}
 atomic_store(&busy,true);atomic_store(&phase,3);uart_link_send("OTA_STATE",0,"1");
 install(NULL); // never returns
done:vTaskDelete(NULL);
}
static void dry_run(void *arg){
 (void)arg;
 for(unsigned i=0;i<30&&usb_tunnel_busy();i++)vTaskDelay(pdMS_TO_TICKS(500));
 if(usb_tunnel_busy())atomic_store(&error_code,33);
 atomic_store(&phase,3);bool ok=!usb_tunnel_busy()&&run_control(true);atomic_store(&phase,ok?6:5);atomic_store(&busy,false);uart_link_send("OTA_STATE",0,"0");vTaskDelete(NULL);
}
static void staged_job(void *arg){
 ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
 if((uintptr_t)arg)dry_run(NULL);else install(NULL); // Each path deletes this task.
}
void bundle_update_start(void){if(xTaskCreate(resume,"update_resume",6144,NULL,5,NULL)!=pdPASS){atomic_store(&phase,5);atomic_store(&error_code,30);}}

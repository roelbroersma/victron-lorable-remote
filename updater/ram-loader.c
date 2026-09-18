// SRAM-only application updater. Never changes bootloader code, option bytes,
// radio NVM, configuration, security counters or the ESP power GPIO.
// Enter with interrupts/DMA disabled, existing USART1 115200 8N1, 48 MHz.
#include "stm32wlxx.h"
#include <stdint.h>
#define APP_BASE 0x08006000u
#define APP_LIMIT 0x08037000u
#define PAGE 2048u
#define ERRORS (FLASH_SR_OPERR|FLASH_SR_PROGERR|FLASH_SR_WRPERR|FLASH_SR_PGAERR|FLASH_SR_SIZERR|FLASH_SR_PGSERR|FLASH_SR_MISERR|FLASH_SR_FASTERR|FLASH_SR_OPTVERR|FLASH_SR_RDERR)
static uint8_t page[PAGE] __attribute__((aligned(8)));
static uint8_t control[PAGE] __attribute__((aligned(8)));
static uint32_t vectors[64] __attribute__((aligned(256)));
static uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
 crc=~crc;while(n--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;
}
static void kick(void){IWDG->KR=0xaaaau;}
static uint32_t clock_now(void){return DWT->CYCCNT;}
static int elapsed(uint32_t start,uint32_t ms){return (uint32_t)(clock_now()-start)>=ms*48000u;}
static void reset(void) __attribute__((noreturn));
static void reset(void){__DSB();SCB->AIRCR=0x05fa0004u;__DSB();for(;;){} }
static int tx(uint8_t b){uint32_t t=clock_now();while(!(USART1->ISR&USART_ISR_TXE_TXFNF)){kick();if(elapsed(t,1000))return 0;}USART1->TDR=b;return 1;}
static int rx(void){uint32_t t=clock_now();for(;;){kick();if(USART1->ISR&USART_ISR_RXNE_RXFNE)return USART1->RDR&255;if(USART1->ISR&(USART_ISR_ORE|USART_ISR_FE|USART_ISR_NE))USART1->ICR=USART_ICR_ORECF|USART_ICR_FECF|USART_ICR_NECF;if(elapsed(t,3000))return -1;}}
static void flush_rx(void){while(USART1->ISR&USART_ISR_RXNE_RXFNE)(void)USART1->RDR;USART1->ICR=USART_ICR_ORECF|USART_ICR_FECF|USART_ICR_NECF;}
static int packet(uint32_t magic,uint32_t a,uint32_t b){uint32_t v[4]={magic,a,b,0};v[3]=crc32(0,(const uint8_t*)v,12);for(unsigned i=0;i<16;i++)if(!tx(((uint8_t*)v)[i]))return 0;return 1;}
static void finish(uint32_t code,uint32_t value,uint32_t mode) __attribute__((noreturn));
static void finish(uint32_t code,uint32_t value,uint32_t mode){
 if(code==0x314b424cu){
  // Keep ESP enable high while its stock bootloader replaces its application.
  // Only the running replacement application acknowledges that reset is safe.
  uint32_t h[4];unsigned used=0;packet(code,value,mode);
  for(;;){
   int c=rx();if(c<0){used=0;packet(code,value,mode);continue;}
   ((uint8_t*)h)[used++]=(uint8_t)c;
   if(used<16)continue;
   if(h[0]==0x3141424cu&&h[1]==value&&h[2]==1&&h[3]==crc32(0,(uint8_t*)h,12))reset();
   // Ignore boot messages and framed startup traffic before the binary ACK.
   for(unsigned i=0;i<15;i++)((uint8_t*)h)[i]=((uint8_t*)h)[i+1];used=15;
  }
 }
 packet(code,value,mode);uint32_t t=clock_now();while(!elapsed(t,2000)){kick();}reset();
}
static void fault(void){finish(0x3145424cu,99,0);}
static int get_page(uint32_t offset,uint32_t length){
 for(unsigned attempt=0;attempt<3;attempt++){
  flush_rx();if(!packet(0x3152424cu,offset,length))continue; // LBR1
  uint32_t h[4];int ok=1;
  for(unsigned i=0;i<16;i++){int c=rx();if(c<0){ok=0;break;}((uint8_t*)h)[i]=(uint8_t)c;}
  if(!ok||h[0]!=0x3144424cu||h[1]!=offset||h[2]!=length)continue;
  for(uint32_t i=0;i<length;i++){int c=rx();if(c<0){ok=0;break;}page[i]=(uint8_t)c;}
  if(ok&&crc32(0,page,length)==h[3])return 1;
 }return 0;
}
static int wait_flash(void){uint32_t t=clock_now();while(FLASH->SR&(FLASH_SR_BSY|FLASH_SR_CFGBSY)){kick();if(elapsed(t,2000))return 0;}return !(FLASH->SR&ERRORS);}
static int erase(uint32_t address){
 if(address!=0x08004000u&&(address<APP_BASE||address>=APP_LIMIT))return 0;
 if(address&(PAGE-1))return 0;
 FLASH->SR=ERRORS|FLASH_SR_EOP;
 FLASH->CR=FLASH_CR_PER|(((address-0x08000000u)/PAGE)<<FLASH_CR_PNB_Pos);
 FLASH->CR|=FLASH_CR_STRT;int ok=wait_flash();FLASH->CR=0;return ok;
}
static int program(uint32_t address,const uint8_t *data,uint32_t n){
 if(n%8||address%8||!n)return 0;
 if(address!=0x08004000u&&(address<APP_BASE||address+n>APP_LIMIT))return 0;
 if(address==0x08004000u&&n!=PAGE)return 0;
 for(uint32_t i=0;i<n;i+=8){
  const uint32_t *p=(const uint32_t *)(data+i);
  FLASH->SR=ERRORS|FLASH_SR_EOP;FLASH->CR=FLASH_CR_PG;
  *(volatile uint32_t *)(address+i)=p[0];__ISB();*(volatile uint32_t *)(address+i+4)=p[1];
  int ok=wait_flash();FLASH->CR=0;if(!ok)return 0;
  if(*(volatile uint32_t *)(address+i)!=p[0]||*(volatile uint32_t *)(address+i+4)!=p[1])return 0;
 }return 1;
}
static int boot_state(uint32_t state){
 ((uint32_t*)control)[0]=0x5a5a5a5au;((uint32_t*)control)[1]=state;
 return erase(0x08004000u)&&program(0x08004000u,control,PAGE);
}
void __attribute__((section(".text.entry"),noreturn)) loader_main(uint32_t size,uint32_t expected,uint32_t mode){
 CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=1;
 for(unsigned i=0;i<64;i++)vectors[i]=(uint32_t)fault;
 SCB->VTOR=(uint32_t)vectors;__DSB();__ISB();
 USART1->CR3&=~(USART_CR3_DMAR|USART_CR3_DMAT);
 USART1->CR1&=~(USART_CR1_RXNEIE_RXFNEIE|USART_CR1_TXEIE_TXFNFIE|USART_CR1_TCIE|USART_CR1_IDLEIE);
 flush_rx();
 if(size<256||size>APP_LIMIT-APP_BASE||size%8||mode>1)finish(0x3145424cu,1,mode);
 // First pass never writes flash. Catch transmission errors before any erase.
 uint32_t crc=0,vector0=0,vector1=0;
 for(uint32_t off=0;off<size;off+=PAGE){
  uint32_t n=size-off>PAGE?PAGE:size-off;
  if(!get_page(off,n))finish(0x3145424cu,2,mode);
  if(!off){vector0=((uint32_t*)page)[0];vector1=((uint32_t*)page)[1];}
  crc=crc32(crc,page,n);
 }
 if(crc!=expected||vector0<0x20000000u||vector0>0x20010000u||vector0%8||!(vector1&1)||vector1<APP_BASE||vector1>=APP_BASE+size)finish(0x3145424cu,3,mode);
 if(!mode)finish(0x3156424cu,crc,mode); // LBV1: full dry-run verified
 // USB installation may already have installed this exact control image.
 if(crc32(0,(const uint8_t*)APP_BASE,size)==expected)finish(0x314b424cu,crc,mode);
 for(uint32_t i=0;i<PAGE;i++)control[i]=*(volatile uint8_t *)(0x08004000u+i);
 FLASH->ACR&=~(FLASH_ACR_ICEN|FLASH_ACR_DCEN);
 if(FLASH->CR&FLASH_CR_LOCK){FLASH->KEYR=0x45670123u;FLASH->KEYR=0xcdef89abu;}
 if(FLASH->CR&FLASH_CR_LOCK)finish(0x3145424cu,4,mode);
 // RAK bootloader download-in-progress marker: interrupted writes stay in USB DFU.
 if(!boot_state(0x01010101u))finish(0x3145424cu,5,mode);
 if(!erase(APP_BASE))finish(0x3145424cu,6,mode);
 crc=0;
 for(uint32_t off=0;off<size;off+=PAGE){
  uint32_t n=size-off>PAGE?PAGE:size-off;
  if(!get_page(off,n))finish(0x3145424cu,7,mode);
  crc=crc32(crc,page,n);
  for(uint32_t i=n;i<PAGE;i++)page[i]=0xff;
  // Keep initial stack/reset vector erased until every other byte is verified.
  uint32_t skip=off?0:8;
  if((off&&!erase(APP_BASE+off))||!program(APP_BASE+off+skip,page+skip,PAGE-skip))finish(0x3145424cu,8,mode);
 }
 if(crc!=expected)finish(0x3145424cu,9,mode);
 ((uint32_t*)page)[0]=vector0;((uint32_t*)page)[1]=vector1;
 if(!program(APP_BASE,page,8)||crc32(0,(const uint8_t*)APP_BASE,size)!=expected)finish(0x3145424cu,10,mode);
 if(!boot_state(0x02020202u))finish(0x3145424cu,11,mode);
 FLASH->CR=FLASH_CR_LOCK;finish(0x314b424cu,crc,mode); // LBK1 complete
}

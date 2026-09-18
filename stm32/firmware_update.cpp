#include <Arduino.h>
#include <board.h>
#include <stm32wlxx_hal.h>
#include "firmware_update.h"
#include "ram_loader_asset.h"
#include "update_bundle.h"
// Fixed SRAM address is safe only after interrupts, DMA and the scheduler have
// stopped. This naked trampoline never reads globals or returns after the copy.
static void __attribute__((naked,noreturn,noinline,noipa)) launch(const uint8_t *src,uint32_t n,uint32_t size,uint32_t crc,uint32_t mode){
 __asm volatile(
  "ldr r7, [sp]\n"
  "mov r5, r2\nmov r6, r3\n"
  "mov.w r2, #0x20000000\n"
  "1: ldrb r3, [r0], #1\nstrb r3, [r2], #1\nsubs r1, #1\nbne 1b\n"
  "movw r3, #0xfc00\nmovt r3, #0x2000\nmsr msp, r3\n"
  "movs r3, #0\nmsr control, r3\nisb\n"
  "mov r0, r5\nmov r1, r6\nmov r2, r7\n"
  "movw r3, #1\nmovt r3, #0x2000\ndsb\nisb\nbx r3\n"
 );
}
bool firmwareUpdateReady(){
 // The application and stock RUI bootloader layouts must match this updater.
 return SystemCoreClock==48000000u && *(volatile uint32_t*)0x08004000u==0x5a5a5a5au &&
   !memcmp((const void*)0x08005000u,"RUI_BOOT_0.8_STM32WLE5CC",sizeof("RUI_BOOT_0.8_STM32WLE5CC"));
}
void firmwareUpdateLaunch(uint32_t size,uint32_t crc,uint32_t mode){
 if(!firmwareUpdateReady()||size<256||size>LBR_STM_MAX||size%8||mode>1)return;
 digitalWrite(WB_IO4,LOW);
 Serial1.flush();Serial.flush();delay(200);
 __disable_irq();SysTick->CTRL=0;
 for(unsigned i=0;i<8;i++){NVIC->ICER[i]=0xffffffffu;NVIC->ICPR[i]=0xffffffffu;}
 // All DMA destinations refer to application RAM, which the stub takes over.
 DMA1_Channel1->CCR=0;DMA1_Channel2->CCR=0;DMA1_Channel3->CCR=0;DMA1_Channel4->CCR=0;
 DMA1_Channel5->CCR=0;DMA1_Channel6->CCR=0;DMA1_Channel7->CCR=0;
 DMA2_Channel1->CCR=0;DMA2_Channel2->CCR=0;DMA2_Channel3->CCR=0;DMA2_Channel4->CCR=0;
 DMA2_Channel5->CCR=0;DMA2_Channel6->CCR=0;DMA2_Channel7->CCR=0;
 launch(RAM_LOADER,sizeof(RAM_LOADER),size,crc,mode);
}

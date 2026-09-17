#include "activity_log.h"
static ActivityEntry entries[24];
static uint8_t nextEntry, entryCount;
static uint32_t seconds, lastMs;
void activityTick() {
    const uint32_t delta=millis()-lastMs;
    seconds+=delta/1000;lastMs+=delta/1000*1000;
}
uint32_t activitySeconds(){return seconds;}
void activityAdd(uint8_t code,uint32_t value) {
    activityTick();entries[nextEntry]={seconds,value,code};
    nextEntry=(nextEntry+1)%24;if(entryCount<24)++entryCount;
}
String activityJson(){
    String out("{\"entries\":[");
    out.reserve(1400);
    for(unsigned n=0;n<entryCount;++n){
        const ActivityEntry &e=entries[(nextEntry+24-entryCount+n)%24];
        if(n)out+=',';
        out+="[";out+=String(e.seconds);out+=',';out+=String(e.code);out+=',';out+=String(e.value);out+=']';
    }
    out+="]}";return out;
}

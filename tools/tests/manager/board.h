#pragma once
#include <stdint.h>
struct FakeNjs { uint8_t value=0; uint8_t get(){return value;} };
struct FakeLorawan {
    FakeNjs njs;
    bool autoJoinDisabled=false;
    bool join(int run,int automatic,int interval,int count){
        autoJoinDisabled=run==0&&automatic==0&&interval==60&&count==0;return true;
    }
};
struct ManagerFakeApi { FakeLorawan lorawan; };
extern ManagerFakeApi api;

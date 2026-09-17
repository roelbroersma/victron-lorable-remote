#pragma once
#include "network_profiles.h"

// Pure, deterministic scheduling policy. All times are monotonic seconds;
// subtraction is wrap-safe. The hardware layer starts exactly one OTAA attempt.
// A backup gets its complete residence/preempt window, also while unreachable.
class NetworkPolicy {
public:
    enum State { DISABLED, WAITING, JOINING, ONLINE, BUDGET_WAIT, FAULT };
    uint8_t slot=255, rank=0, state=DISABLED, failures=0;
    uint32_t residenceAt=0, nextAt=0, healthyAt=0;
    bool probing=false;
    uint8_t returnRank=0;
    bool returnWasHealthy=false;
    uint8_t joinsToday[MAX_NETWORKS]={};
    uint32_t budgetAt[MAX_NETWORKS]={};
    bool budgetStarted[MAX_NETWORKS]={};
    uint32_t lastAttemptAt[MAX_NETWORKS]={};
    bool attempted[MAX_NETWORKS]={};

    void begin(const NetworkProfile *p,const uint8_t *order,uint32_t now) {
        *this=NetworkPolicy();select(p,order,0,now);
    }
    uint8_t first(const NetworkProfile *p,const uint8_t *order,unsigned from=0) const {
        for(unsigned r=from;r<MAX_NETWORKS;++r)if(p[order[r]].enabled&&networkHasKey(p[order[r]]))return r;
        return 255;
    }
    void select(const NetworkProfile *p,const uint8_t *order,uint8_t r,uint32_t now) {
        r=first(p,order,r);if(r==255){slot=255;state=DISABLED;return;}
        rank=r;slot=order[r];state=WAITING;nextAt=now;failures=0;residenceAt=now;
    }
    void tick(const NetworkProfile *p,const uint8_t *order,uint32_t now,bool radioBusy) {
        if(slot==255||state==FAULT||radioBusy)return;
        const uint8_t preferred=first(p,order);
        if(!probing && rank!=preferred && now-residenceAt>=p[slot].preemptMinutes*60UL) {
            returnRank=rank;returnWasHealthy=state==ONLINE;probing=true;
            select(p,order,preferred,now);
        }
    }
    bool ready(const NetworkProfile *p,uint32_t now) {
        if(slot==255||state==ONLINE||state==JOINING||state==FAULT)return false;
        if((int32_t)(now-nextAt)<0)return false;
        if(!budgetStarted[slot]||now-budgetAt[slot]>=86400UL){budgetStarted[slot]=true;budgetAt[slot]=now;joinsToday[slot]=0;}
        if(p[slot].kind==1&&joinsToday[slot]>=6){state=BUDGET_WAIT;nextAt=budgetAt[slot]+86400UL;return false;}
        // A priority/preempt switch must not bypass this profile's cooldown.
        if(p[slot].kind==1&&attempted[slot]&&now-lastAttemptAt[slot]<3600UL){state=BUDGET_WAIT;nextAt=lastAttemptAt[slot]+3600UL;return false;}
        state=WAITING;return true;
    }
    void started(uint32_t now) {state=JOINING;nextAt=now+120;attempted[slot]=true;lastAttemptAt[slot]=now; if(joinsToday[slot]<255)++joinsToday[slot];}
    void deferred(uint32_t now){state=WAITING;nextAt=now+60;}
    void joined(uint32_t now) {state=ONLINE;healthyAt=now;failures=0;probing=false;}
    void failed(const NetworkProfile *p,const uint8_t *order,uint32_t now) {
        state=WAITING;
        if(probing) {
            const uint8_t next=first(p,order,rank+1);
            if(next<returnRank){select(p,order,next,now);nextAt=now+60;return;}
            // Higher priorities failed. A formerly working backup is retried;
            // an unreachable backup yields to the next lower one this round.
            uint8_t resume=returnWasHealthy?returnRank:first(p,order,returnRank+1);
            if(resume==255)resume=first(p,order,first(p,order)+1);
            if(resume==255)resume=first(p,order);
            probing=false;select(p,order,resume,now);nextAt=now+60;return;
        }
        if(rank==first(p,order)) {
            const uint8_t next=first(p,order,rank+1);
            if(next!=255){select(p,order,next,now);nextAt=now+60;return;}
        }
        // No flash writes for retries, countdowns, budgets or health counters.
        nextAt=now+(p[slot].kind==1?3600UL:300UL);
    }
    bool healthDue(const NetworkProfile *p,uint32_t now,uint16_t minutes) const {
        if(slot==255||state!=ONLINE||minutes==0)return false;
        if(p[slot].kind==1&&minutes<240)minutes=240;
        return now-healthyAt>=minutes*60UL;
    }
    void health(bool ok,uint32_t now) {
        healthyAt=now;if(ok)failures=0;else if(failures<255)++failures;
    }
    uint32_t remaining(const NetworkProfile *p,const uint8_t *order,uint32_t now) const {
        if(slot==255||probing||rank==first(p,order))return 0;
        const uint32_t period=p[slot].preemptMinutes*60UL,elapsed=now-residenceAt;
        return elapsed>=period?0:period-elapsed;
    }
};

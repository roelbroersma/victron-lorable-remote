#pragma once
/* Bounded decoder for Victron's concatenated CBOR response records.
 * No byte-pattern matching: malformed/unsupported frames fail closed. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { const uint8_t *p; size_t n, at; } vic_cursor_t;
static bool vic_head(vic_cursor_t *c, unsigned *major, uint32_t *v) {
    if(c->at>=c->n) return false;
    uint8_t b=c->p[c->at++]; *major=b>>5; *v=b&31;
    if(*v<24) return true;
    unsigned count=*v==24?1:*v==25?2:*v==26?4:0;
    if(!count || count>c->n-c->at) return false;
    *v=0; while(count--) *v=(*v<<8)|c->p[c->at++];
    return true;
}
static bool vic_uint(vic_cursor_t *c, uint32_t *v) {
    unsigned m; return vic_head(c,&m,v) && m==0;
}
static bool vic_skip(vic_cursor_t *c, unsigned depth) {
    if(depth>4 || c->at>=c->n) return false;
    if(c->p[c->at]==0x9f) {
        ++c->at;
        while(c->at<c->n && c->p[c->at]!=0xff) if(!vic_skip(c,depth+1)) return false;
        if(c->at>=c->n) return false;
        ++c->at; return true;
    }
    unsigned m; uint32_t v;
    if(!vic_head(c,&m,&v)) return false;
    if(m==0 || m==1) return true;
    if(m==2 || m==3) { if(v>c->n-c->at) return false; c->at+=v; return true; }
    if(m==4 && v<=c->n-c->at) { while(v--) if(!vic_skip(c,depth+1)) return false; return true; }
    return false;
}
static bool victron_find_value(const uint8_t *frame,size_t n,uint8_t instance,uint16_t reg,
                              uint8_t *out,size_t capacity,size_t *length) {
    vic_cursor_t c={frame,n,0}; size_t found_at=0,found_len=0; bool found=false;
    if(!frame || !n || n>512 || !out || !length) return false;
    while(c.at<n) {
        uint32_t op,ins,key,value; unsigned m;
        if(!vic_uint(&c,&op)) return false;
        if(op==2) { if(!vic_skip(&c,0)) return false; continue; }
        if(op!=7 && op!=8 && op!=9) return false;
        if(!vic_uint(&c,&ins) || !vic_uint(&c,&key)) return false;
        if(op==8) {
            if(!vic_head(&c,&m,&value) || m!=2 || value>c.n-c.at) return false;
            if(ins==instance && key==reg) { found=true;found_at=c.at;found_len=value; }
            c.at+=value;
        } else if(!vic_skip(&c,0)) return false;
    }
    if(!found || found_len>capacity) return false;
    memcpy(out,frame+found_at,found_len); *length=found_len; return true;
}

#include "coop_presentation.h"
#include "coop_runtime.h"
#include "common_rtl.h"
#include "snes/ppu.h"
#include <stdlib.h>
#include <string.h>

static unsigned read16(const uint8_t *p,unsigned a) {return p[a]|(p[a+1]<<8);}
static uint16_t picture[0x8000],palette[256];

void SmwCoopCaptureVisual(CoopVisual *visual) {
    /* Reproduce MarioGFXDMA's source selection into private picture memory.
     * The guest NMI still uploads the primary once; it is never replayed. */
    memcpy(picture,g_ppu->vram,sizeof(picture));
    memcpy(palette,g_ppu->cgram,sizeof(palette));
    unsigned count=g_ram[0xd84];
    if(count>10 || (count&1))Die("Invalid native co-op player graphics count");
    unsigned pal=read16(g_ram,0xd82);
    if(count) {
        if(pal<0x8000 || pal>0xffec)Die("Invalid native co-op player palette");
        for(unsigned i=0;i<10;++i)palette[0x86+i]=(uint16_t)read16(g_rom,(pal&0x7fff)+i*2);
    }
    for(unsigned row=0;row<2;++row)for(unsigned tile=0;tile<count;tile+=2) {
        unsigned source=read16(g_ram,0xd85+row*10+tile);
        for(unsigned j=0;j<32;++j)
            picture[0x6000+row*0x100+tile*16+j]=(uint16_t)read16(g_ram,(source+j*2)&0xffff);
    }
    unsigned source=read16(g_ram,0xd99);
    for(unsigned j=0;j<16;++j)picture[0x67f0+j]=(uint16_t)read16(g_ram,(source+j*2)&0xffff);
    int origin_x=(int16_t)(read16(g_ram,0x94)-read16(g_ram,0x1a));
    int origin_y=(int16_t)(read16(g_ram,0x96)-read16(g_ram,0x1c));
    memset(visual,0,sizeof(*visual));
    for(unsigned slot=0;slot<128;++slot) {
        unsigned pos=read16(g_ram,0x200+slot*4),attr=read16(g_ram,0x202+slot*4);
        if((pos>>8)==0xf0)continue;
        if(visual->count==COOP_BODY_PIECES)Die("Native co-op player draw exceeded audited piece count");
        CoopVisualPiece *v=&visual->pieces[visual->count++];
        v->x=origin_x+(int8_t)((pos&255)-(origin_x&255));
        v->y=origin_y+(int8_t)((pos>>8)-(origin_y&255));
        v->width=v->height=(g_ram[0x420+slot]&2)?16:8;
        v->priority=(attr>>12)&3;v->math=(attr&0x800)!=0;v->slot=slot;
        unsigned base=(g_ppu->obsel&7)*8192;
        if(attr&256)base+=(((g_ppu->obsel>>3)&3)+1)*4096;
        for(unsigned y=0;y<v->height;++y)for(unsigned x=0;x<v->width;++x) {
            unsigned row=(attr&0x8000)?v->height-1-y:y;
            unsigned col=(attr&0x4000)?v->width-1-x:x;
            unsigned tile=(((((attr&255)>>4)+row/8)&15)<<4)|(((attr&15)+col/8)&15);
            unsigned address=(base+tile*16+(row&7))&0x7fff,shift=7-(col&7);
            uint32_t bits=((uint32_t)picture[address]|((uint32_t)picture[(address+8)&0x7fff]<<16))>>shift;
            unsigned pixel=(bits&1)|((bits>>7)&2)|((bits>>14)&4)|((bits>>21)&8);
            if(pixel)v->pixels[y*16+x]=0x8000|palette[128+((attr>>9)&7)*16+pixel];
        }
    }
}
void SmwCoopPresentationLatch(void) {
    CoopMachine *m=SmwCoopMachine();if(!m)return;
    unsigned mode=g_ram[0x100];
    bool level=mode==0x13 || mode==0x14 || mode==0x0b || mode==0x0f || mode==0x15 || mode==0x18;
    for(size_t i=0;i<m->actor_count;++i) {
        if(!level)m->actors[i].pending.count=0;
        m->actors[i].visible=m->actors[i].pending;
    }
}
void SmwCoopPresentationPrepare(void) {
    static PpuExtraObject *objects;
    static size_t capacity;
    CoopMachine *m=SmwCoopMachine();size_t count=0;
    if(m)for(size_t i=0;i<m->actor_count;++i)
        if(m->actors[i].player!=m->session.primary)count+=m->actors[i].visible.count;
    if(count>capacity) {
        if(count>SIZE_MAX/sizeof(*objects))Die("Native co-op draw allocation overflow");
        PpuExtraObject *next=realloc(objects,count*sizeof(*objects));
        if(!next)Die("Unable to allocate native co-op draw list");
        objects=next;capacity=count;
    }
    size_t at=0;
    if(m)for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];if(a->player==m->session.primary)continue;
        for(unsigned j=0;j<a->visible.count;++j) {
            CoopVisualPiece *v=&a->visible.pieces[j];
            objects[at++]=(PpuExtraObject){v->x,v->y,(uint16_t)v->width,(uint16_t)v->height,16,
                (uint8_t)v->priority,(uint8_t)v->math,(uint8_t)v->slot,a->player+1,v->pixels};
        }
    }
    PpuSetExtraObjects(g_ppu,objects,count);
}

#include "coop_presentation.h"
#include "coop_runtime.h"
#include "common_rtl.h"
#include "snes/ppu.h"
#include "smw_renderer.h"
#include <stdlib.h>
#include <string.h>

static unsigned read16(const uint8_t *p,unsigned a) {return p[a]|(p[a+1]<<8);}
static uint16_t picture[0x8000],palette[256];

static void capture_visual(CoopVisual *visual,bool primary) {
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
        if(primary) {
            unsigned fence=g_ram[0x13f9];
            if(fence>2)Die("Invalid native co-op fence side");
            unsigned first=(0x100+g_rom[0xe2b6-0x8000+fence])/4;
            if(slot<first || slot>=first+7)continue;
        }
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
void SmwCoopCaptureVisual(CoopVisual *visual) {capture_visual(visual,false);}
void SmwCoopPresentationLatch(void) {
    CoopMachine *m=SmwCoopMachine();if(!m)return;
    unsigned mode=g_ram[0x100];
    bool level=mode==0x13 || mode==0x14 || mode==0x0b || mode==0x0f || mode==0x15 || mode==0x18;
    if(level && m->room_initialized) {
        CoopActor *primary=coop_machine_actor(m,m->session.primary);
        CoopPlayer *p=coop_player(&m->session,m->session.primary);
        primary->pending.count=0;
        if(p->life==COOP_PLAYING || p->life==COOP_DYING)capture_visual(&primary->pending,true);
    }
    for(size_t i=0;i<m->actor_count;++i) {
        if(!level)m->actors[i].pending.count=0;
        m->actors[i].visible=m->actors[i].pending;
    }
}

enum { DECOR_WIDTH=32, DECOR_HEIGHT=40, DRAW_PIXELS=DECOR_WIDTH*DECOR_HEIGHT };
static void number(uint16_t *pixels,unsigned stride,int x,int y,unsigned n,uint16_t color) {
    static const uint16_t digits[]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    if(n>9) {number(pixels,stride,x-4,y,n/10,color);n%=10;}
    unsigned bits=digits[n];
    for(unsigned row=0;row<5;++row)for(unsigned col=0;col<3;++col)
        if(x+(int)col>=0 && x+col<stride && ((bits>>(14-row*3-col))&1))
            pixels[(y+row)*stride+x+col]=color;
}
static void identity(uint16_t *pixels,unsigned stride,int y,const CoopPlayer *p) {
    uint16_t color=p->character==0?0x801f:p->character==1?0x83e0:0xffff;
    /* A compact P plus the stable player number for recovery bubbles and
     * future characters. Player IDs are independent of array order. */
    static const uint8_t letter[]={6,5,6,4,4};
    for(unsigned row=0;row<5;++row)for(unsigned col=0;col<3;++col)
        if(letter[row]&(4>>col))pixels[(y+row)*stride+7+col]=color;
    number(pixels,stride,13,y,p->id+1,color);
}
static void hud_picture(uint16_t *pixels,unsigned source_x) {
    /* Decode the original BG3 tiles, including their transparency and palette.
     * Both reserve boxes and a relocated TIME use the unmodified native art. */
    for(unsigned y=0;y<40;++y)for(unsigned x=0;x<32;++x) {
        unsigned px=(source_x+x+g_ppu->hScroll[2])&1023,py=(y+1+g_ppu->vScroll[2])&1023;
        unsigned sc=g_ppu->bgXsc[2],tx=px>>3,ty=py>>3;
        unsigned address=(sc&0xfc)*256+(tx&31)+(ty&31)*32;
        if((sc&1) && (tx&32))address+=1024;
        if((sc&2) && (ty&32))address+=(sc&1)?2048:1024;
        unsigned tile=g_ppu->vram[address&0x7fff];
        unsigned col=px&7,row=py&7;
        if(tile&0x4000)col=7-col;if(tile&0x8000)row=7-row;
        unsigned base=((g_ppu->bgTileAdr>>8)&15)*4096;
        unsigned bits=g_ppu->vram[(base+(tile&1023)*8+row)&0x7fff]>>(7-col);
        unsigned pixel=(bits&1)|((bits>>7)&2);
        if(pixel)pixels[y*DECOR_WIDTH+x]=0x8000|g_ppu->cgram[((tile>>10)&7)*4+pixel];
    }
}
static void reserve_picture(uint16_t *pixels,const CoopPlayer *p) {
    hud_picture(pixels,112);
    unsigned item=p->reserve;
    if(item>=1 && item<=4) {
        unsigned tile=g_rom[0x8dfa-0x8000+item-1];
        unsigned attr=g_rom[0x8e02-0x8000+item-1];
        if(item==3)attr=g_rom[0x8dfe - 0x8000+((g_ram[0x13]>>1)&3)];
        unsigned base=(g_ppu->obsel&7)*8192;
        if(attr&1)base+=(((g_ppu->obsel>>3)&3)+1)*4096;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
            unsigned t=(((((tile>>4)+y/8)&15)<<4)|(((tile&15)+x/8)&15));
            unsigned address=(base+t*16+(y&7))&0x7fff,shift=7-(x&7);
            uint32_t bits=((uint32_t)g_ppu->vram[address]|((uint32_t)g_ppu->vram[(address+8)&0x7fff]<<16))>>shift;
            unsigned pixel=(bits&1)|((bits>>7)&2)|((bits>>14)&4)|((bits>>21)&8);
            if(pixel)pixels[(y+15)*DECOR_WIDTH+x+8]=0x8000|g_ppu->cgram[128+((attr>>1)&7)*16+pixel];
        }
    }
    static const uint8_t letters[2][5]={{17,27,21,17,17},{16,16,16,16,31}};
    if(p->character<2) {
        for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)
            if(letters[p->character][y]&(16>>x))pixels[(y+1)*DECOR_WIDTH+x+2]=p->character?0x83e0:0x801f;
    } else identity(pixels,DECOR_WIDTH,1,p);
}
static void bubble_picture(uint16_t *pixels,const CoopPlayer *p) {
    for(int y=0;y<24;++y)for(int x=0;x<24;++x) {
        int dx=x*2-23,dy=y*2-23,d=dx*dx+dy*dy;
        if(d<23*23 && d>19*19)pixels[y*24+x]=d>21*21?0x8000:0xffa8;
    }
    for(int y=5;y<8;++y)for(int x=6;x<9;++x)pixels[y*24+x]=0xffff;
    identity(pixels,24,11,p);
}
static bool covered(const CoopMachine *m,const CoopActor *actor,const CoopVisualPiece *v,int x,int y) {
    for(size_t i=0;i<m->actor_count;++i) {
        const CoopActor *other=&m->actors[i];
        if(other==actor || !coop_player_precedes(&m->session,other->player,actor->player))continue;
        for(unsigned j=0;j<other->visible.count;++j) {
            const CoopVisualPiece *q=&other->visible.pieces[j];
            int dx=x-q->x,dy=y-q->y;
            if(q->priority==v->priority && dx>=0 && dy>=0 && dx<(int)q->width && dy<(int)q->height &&
               (q->pixels[dy*16+dx]&0x8000))return true;
        }
    }
    return false;
}
void SmwCoopPresentationPrepare(void) {
    static PpuExtraObject *objects;
    static uint16_t *pixels;
    static size_t capacity;
    static uint32_t removed_bg[kPpuBufWidth*240],removed_obj[kPpuBufWidth*240];
    CoopMachine *m=SmwCoopMachine();size_t count=0;
    bool level=m && m->room_initialized && (g_ram[0x100]==0x14 || g_ram[0x100]==0x13 ||
        g_ram[0x100]==0x0b || g_ram[0x100]==0x0f || g_ram[0x100]==0x15);
    if(m)for(size_t i=0;i<m->actor_count;++i) {
        if(m->actors[i].player!=m->session.primary)count+=m->actors[i].visible.count;
        if(level)count+=2; /* reserve plus optional bubble/warning */
    }
    if(level)++count; /* original TIME label and digits */
    if(count>capacity) {
        if(count>SIZE_MAX/sizeof(*objects) || count>SIZE_MAX/(DRAW_PIXELS*sizeof(*pixels)))
            Die("Native co-op draw allocation overflow");
        PpuExtraObject *next=realloc(objects,count*sizeof(*objects));
        if(!next)Die("Unable to allocate native co-op draw list");
        objects=next;
        uint16_t *next_pixels=realloc(pixels,count*DRAW_PIXELS*sizeof(*pixels));
        if(!next_pixels)Die("Unable to allocate native co-op picture list");
        pixels=next_pixels;capacity=count;
    }
    size_t at=0;
    if(m)for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];if(a->player==m->session.primary)continue;
        for(unsigned j=0;j<a->visible.count;++j) {
            CoopVisualPiece *v=&a->visible.pieces[j];
            uint16_t *picture=pixels+at*DRAW_PIXELS;
            memcpy(picture,v->pixels,sizeof(v->pixels));
            for(unsigned y=0;y<v->height;++y)for(unsigned x=0;x<v->width;++x)
                if(covered(m,a,v,v->x+(int)x,v->y+(int)y))picture[y*16+x]=0;
            objects[at++]=(PpuExtraObject){v->x,v->y,(uint16_t)v->width,(uint16_t)v->height,16,
                (uint8_t)v->priority,(uint8_t)v->math,(uint8_t)v->slot,a->player+1,picture};
        }
    }
    if(level) {
        if(!PpuBindOverlaySurface(g_ppu,kPpuOverlaySource_Bg3,(uint8_t *)removed_bg,kPpuBufWidth*4) ||
           !PpuBindOverlaySurface(g_ppu,kPpuOverlaySource_Obj,(uint8_t *)removed_obj,kPpuBufWidth*4) ||
           !PpuSetOverlayCapture(g_ppu,kPpuOverlaySource_Bg3,112,0,64,40,kPpuOverlayFlag_RemoveFromGame) ||
           !PpuSetOverlayCapture(g_ppu,kPpuOverlaySource_Obj,112,0,32,40,kPpuOverlayFlag_RemoveFromGame) ||
           !PpuSetOverlayOamRange(g_ppu,(g_ram[0xd9b]&0x40)?0:56,1))
            Die("Native co-op reserve capture failed");
        int width=g_smw_video.enabled?g_smw_viewport.width:256;
        int native=g_smw_video.enabled?SmwViewOffset(g_smw_viewport,(int)read16(g_ram,0x1a),
                        (g_ram[0x5e]+1)*256):0;
        /* Keep lives/bonus at the left and coins/score at the right. At native
         * width, TIME shifts 24 pixels to make room for the second stock box.
         * A larger future roster wraps instead of covering those counters. */
        size_t columns=(size_t)((width-192)/32);
        if(!columns)columns=1;
        if(columns>m->actor_count)columns=m->actor_count;
        int start=width/2-(int)(columns*32)/2;
        if(start<112)start=112;
        int timer=width-112,end=start+(int)columns*32;
        if(timer<end-8)timer=end-8;
        uint16_t *timer_picture=pixels+at*DRAW_PIXELS;memset(timer_picture,0,DRAW_PIXELS*sizeof(*timer_picture));
        hud_picture(timer_picture,144);
        objects[at++]=(PpuExtraObject){timer-native,0,32,40,32,3,0,0,1,timer_picture};
        for(size_t i=0;i<m->actor_count;++i) {
            CoopPlayer *p=coop_player(&m->session,m->actors[i].player);
            size_t rank=0;for(size_t j=0;j<m->actor_count;++j)
                rank+=coop_player_precedes(&m->session,m->actors[j].player,p->id);
            uint16_t *picture=pixels+at*DRAW_PIXELS;memset(picture,0,DRAW_PIXELS*sizeof(*picture));
            reserve_picture(picture,p);
            objects[at++]=(PpuExtraObject){start+(int)(rank%columns)*32-native,(int)(rank/columns)*40,
                32,40,32,3,0,0,p->id+1,picture};
            bool bubble=p->life==COOP_DEATH_BUBBLE || p->life==COOP_CATCHUP_BUBBLE;
            if(!bubble && !p->separation_ticks)continue;
            if(m->session.outcome!=COOP_CONTINUE)continue;
            CoopPlayerId anchor=coop_nearest_player(&m->session,p->x,p->y,false,false);
            CoopPlayer *survivor=coop_player(&m->session,anchor);
            if(!survivor)continue;
            picture=pixels+at*DRAW_PIXELS;memset(picture,0,DRAW_PIXELS*sizeof(*picture));
            bubble_picture(picture,p);
            int x=survivor->x-(int)read16(g_ram,0x1a)+20+(int)rank*26;
            int y=survivor->y-(int)read16(g_ram,0x1c)-56;
            if(!bubble)x=p->x<survivor->x?4-native:width-28-native;
            if(x<4-native)x=4-native;if(x>width-28-native)x=width-28-native;
            if(y<44)y=44;if(y>188)y=188;
            y+=(int)((m->session.frame/8)%4)-2;
            objects[at++]=(PpuExtraObject){x,y,24,24,24,3,0,0,p->id+1,picture};
        }
    }
    PpuSetExtraObjects(g_ppu,objects,at);
}

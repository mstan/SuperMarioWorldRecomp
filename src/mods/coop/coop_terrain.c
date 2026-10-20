#include "coop_terrain.h"
#include "coop_machine.h"

static unsigned r16(const uint8_t *r,unsigned at) {return r[at]|(r[at+1]<<8);}
static bool rom8(const CoopTerrain *t,unsigned address,uint8_t *out) {
    if((address&0xffff)<0x8000)return false;
    size_t at=((address>>16)&0x7f)*0x8000+(address&0x7fff);
    if(at>=t->rom_size)return false;
    *out=t->rom[at];return true;
}
bool coop_terrain_block(const CoopTerrain *t,unsigned layer,int32_t x,int32_t y,
                        uint16_t *block) {
    if(!t || !t->ram || !t->rom || !block || layer>1 || x<0 || y<0)return false;
    const uint8_t *r=t->ram;
    /* CODE_00F465..F54D, including the separate layer-2 table offsets. */
    bool vertical=(r[0x5b]&(1u<<layer))!=0;
    unsigned screen=(unsigned)(vertical?y:x)>>8;
    unsigned limit=layer?(vertical?14u:16u):r[0x5d];
    if(screen>=limit || screen>=32 || (vertical?x>=512:y>=432))return false;
    unsigned lo=vertical?(layer?0xba8e:0xba80):(layer?0xba70:0xba60);
    unsigned hi=vertical?(layer?0xbaca:0xbabc):(layer?0xbaac:0xba9c);
    uint8_t l,h;
    if(!rom8(t,lo+screen,&l) || !rom8(t,hi+screen,&h))return false;
    unsigned index=((unsigned)y&0xf0)|(((unsigned)x&255)>>4);
    index+=(unsigned)(vertical?x:y)&0xff00;
    unsigned at=l+(h<<8)+index;
    if(at<0xc800 || at>0xffff)return false;
    unsigned b=r[at]|(r[at+0x10000]<<8);
    if(b>=512)return false;
    /* RemapBlocks ($00:F545). Switch effects must be interpreted at query
     * time, without activating switches, coins or any other block effects. */
    if(b<256) {
        if(b==0x29 && r[0x14ad])b=0x24;
        else if((b==0x2b && r[0x14ad]) || (b>=0xec && b<0xfc))b=0x132;
    } else if((b==0x132 && r[0x14ad]) || (b==0x12f && r[0x14ae]))b=0x2b;
    *block=(uint16_t)b;return true;
}
static bool dangerous(const CoopTerrain *t,unsigned b) {
    unsigned tileset=t->ram[0x1931];
    if(b==5 || b==0x12f || b>=0x1fb)return true;
    if((tileset==5 || tileset==13) && b>=0x159 && b<=0x15b)return true;
    if(tileset==1 && (b==0x15c || (b>=0x166 && b<0x16a)))return true;
    return (tileset==3 || tileset==14) && b>=0x1d2;
}
static bool body_clear(const CoopTerrain *t,unsigned layer,int left,int right,
                       int top,int feet,bool water) {
    for(int y=top;y<feet;) {
        for(int x=left;x<=right;) {
            uint16_t b;
            if(!coop_terrain_block(t,layer,x,y,&b) || dangerous(t,b))return false;
            if(b<256) {if(b<5 && !water)return false;}
            else if(b>=0x111)return false; /* slope clearance audited separately */
            int next=(x&~15)+16;x=next>right && x!=right?right:next;
        }
        int next=(y&~15)+16;y=next>=feet && y!=feet-1?feet-1:next;
    }
    return true;
}
static bool footing(const CoopTerrain *t,unsigned layer,int x,int feet) {
    uint16_t b;
    if(!coop_terrain_block(t,layer,x,feet,&b) || dangerous(t,b))return false;
    /* Flat and one-way floor ranges from Normal_IP4/5. Lava tiles in the
     * underground tilesets are not floors even when their ID is in range. */
    if((t->ram[0x1931]==3 || t->ram[0x1931]==14) && b>=0x159 && b<=0x15b)return false;
    return b>=0x100 && b<0x16e && !(feet&15);
}
static bool rider_owns_sprite(const CoopTerrain *t,unsigned slot) {
    if(!t->session || !t->entities || t->ram[0x9e + slot]!=0x35 ||
       t->ram[0x14c8+slot]!=8)return false;
    for(size_t i=0;i<t->entity_count;++i) {
        const CoopEntity *e=&t->entities[i];
        if(e->kind!=COOP_ENTITY_NORMAL || e->slot!=slot || e->type!=0x35 ||
           e->flags!=COOP_ENTITY_RIDDEN)continue;
        const CoopPlayer *p=coop_player_const(t->session,e->owner);
        if(p && p->mount==e->id && (p->life==COOP_PLAYING ||
                p->life==COOP_DEATH_BUBBLE || p->life==COOP_CATCHUP_BUBBLE))return true;
    }
    return false;
}
static bool platform_footing(const CoopTerrain *t,unsigned slot,int left,int right,int feet) {
    const uint8_t *r=t->ram;
    if(r[0x14c8+slot]!=8 || r[0x9e + slot]<0x55 || r[0x9e + slot]>0x59)return false;
    /* CODE_01B457 places an unmounted pose at platform Y-31. Its native
     * clipping feet are therefore Y+1. Use GetSpriteClippingA's ROM-derived
     * horizontal bounds; the complete returning body must fit on the top. */
    if(feet!=(int)(r[0xd8+slot]|(r[0x14d4+slot]<<8))+1)return false;
    unsigned clip=r[0x1662+slot]&0x3f;uint8_t dx,width;
    if(!rom8(t,0x03b56c+clip,&dx) || !rom8(t,0x03b5a8+clip,&width))return false;
    int x=(r[0xe4+slot]|(r[0x14e0+slot]<<8))+(int8_t)dx;
    return width && left>=x && right<x+width;
}
static bool clear_of_sprites(const CoopTerrain *t,int left,int right,int top,int feet) {
    for(unsigned i=0;i<12;++i) {
        if(t->ram[0x14c8+i]<8)continue;
        /* An active rider/mount uses the combined body below. A mount held
         * by an out-of-play bubble is not an obstacle at its stale position.
         * Resolve ownership by stable identity, never by Yoshi type alone. */
        if(rider_owns_sprite(t,i) || platform_footing(t,i,left,right,feet))continue;
        int x=t->ram[0xe4+i]|(t->ram[0x14e0+i]<<8);
        int y=t->ram[0xd8+i]|(t->ram[0x14d4+i]<<8);
        /* Conservative clearance while per-type contact shapes are added.
         * Waiting is preferable to appearing inside a live world object. */
        if(right>=x-16 && left<x+48 && feet>y-16 && top<y+48)return false;
    }
    for(unsigned i=0;i<10;++i) {
        if(!t->ram[0x170b+i])continue;
        int x=t->ram[0x171f+i]|(t->ram[0x1733+i]<<8);
        int y=t->ram[0x1715+i]|(t->ram[0x1729+i]<<8);
        if(right>=x-8 && left<x+24 && feet>y-8 && top<y+24)return false;
    }
    return true;
}
static bool safe_at(const CoopTerrain *t,const CoopPlayer *returning,
                    const CoopPlayer *anchor,int center,int32_t *x,int32_t *y) {
    /* Start with the anchor's footing, not its center: a small returning
     * actor must not appear in the floor below a tall survivor. */
    bool mounted=returning->mount!=COOP_NO_ENTITY;
    int height=returning->checkpoint_upgrade || returning->power!=COOP_SMALL?26:12;
    if(mounted)height=height==26?32:24;
    int clearance=mounted?48:height,half=mounted?16:6;
    int feet=anchor->y+anchor->height/2,left=center-half,right=center+half-1;
    if(left<0 || feet<clearance)return false;
    bool support=false,water=anchor->swimming;
    for(unsigned layer=0;layer<2;++layer) {
        if(layer==0 && (t->ram[0x5b]&0x40))continue;
        if(layer==1 && !(t->ram[0x5b]&0x80))continue;
        int dx=layer?(int16_t)r16(t->ram,0x26):0;
        int dy=layer?(int16_t)r16(t->ram,0x28):0;
        if(!body_clear(t,layer,left+dx,right+dx,feet-clearance+dy,feet+dy,water))return false;
        support|=footing(t,layer,left+dx,feet+dy) && footing(t,layer,right+dx,feet+dy);
    }
    for(unsigned i=0;i<12;++i)support|=platform_footing(t,i,left,right,feet);
    if(!support && !water)return false;
    if(!clear_of_sprites(t,left,right,feet-clearance,feet))return false;
    if(t->session)for(size_t i=0;i<t->session->player_count;++i) {
        const CoopPlayer *p=&t->session->players[i];
        if(p->id==returning->id || p->life!=COOP_PLAYING)continue;
        int other_half=p->mount!=COOP_NO_ENTITY?16:p->half_width;
        int other_height=p->mount!=COOP_NO_ENTITY?48:p->height;
        int other_feet=p->y+p->height/2;
        if(right>=p->x-other_half-4 && left<=p->x+other_half+4 &&
           feet>other_feet-other_height && feet-clearance<other_feet)return false;
    }
    *x=center;*y=feet-height/2;return true;
}
bool coop_terrain_safe(const CoopTerrain *t,const CoopPlayer *returning,
                       const CoopPlayer *anchor,int32_t *x,int32_t *y) {
    if(!t || !returning || !anchor || !x || !y || anchor->life!=COOP_PLAYING)return false;
    /* Keep both bodies distinct. If nearby footing is obstructed, keep the
     * visible recovery bubble and try again; never fall back to overlapping. */
    static const int offsets[]={24,-24,40,-40,56,-56};
    for(size_t i=0;i<sizeof(offsets)/sizeof(*offsets);++i)
        if(safe_at(t,returning,anchor,anchor->x+offsets[i],x,y))return true;
    return false;
}

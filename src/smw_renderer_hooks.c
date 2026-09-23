#include "smw_renderer.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "common_rtl.h"
#include "snes/saveload.h"

static unsigned spawn_frame;
typedef struct SpawnState {
  uint32_t list;
  uint8_t visited[128], pending;
} SpawnState;
static SpawnState spawn_state={.pending=255}, restored_spawn;
static bool restored_extra;
static void reset_spawns(void) {
  memset(&spawn_state,0,sizeof(spawn_state));
  spawn_state.pending=255;
}
void SmwRendererSpawnFrame(void) {
  ++spawn_frame;
  if(!g_smw_video.enabled || !g_smw_video.adaptive_spawns || g_smw_viewport.width<=256 ||
     g_ram[0x100]!=0x14 || (g_ram[0x5b]&1)) reset_spawns();
}

/* Optional RTLS game chunk: old saves remain readable. The activation guard
 * is gameplay state and must rewind with WRAM, including consumed records
 * whose native load flag is already clear. Stream fields without padding. */
void SmwRendererSaveExtra(SaveLoadInfo *sli) {
  uint32_t header[]={0x53574d53,1}; /* SMWS, version 1 */
  sli->func(sli,header,sizeof(header));
  sli->func(sli,&spawn_state.list,sizeof(spawn_state.list));
  sli->func(sli,spawn_state.visited,sizeof(spawn_state.visited));
  sli->func(sli,&spawn_state.pending,sizeof(spawn_state.pending));
}
void SmwRendererLoadExtra(SaveLoadInfo *sli,uint32_t version) {
  (void)version;
  uint32_t header[2]={0};
  restored_extra=false;
  sli->func(sli,header,sizeof(header));
  if(header[0]!=0x53574d53 || header[1]!=1) return;
  memset(&restored_spawn,0,sizeof(restored_spawn));
  sli->func(sli,&restored_spawn.list,sizeof(restored_spawn.list));
  sli->func(sli,restored_spawn.visited,sizeof(restored_spawn.visited));
  sli->func(sli,&restored_spawn.pending,sizeof(restored_spawn.pending));
  restored_extra=true;
}
void SmwRendererStateLoaded(uint32_t version) {
  (void)version;
  SmwRendererResetScene();
  if(restored_extra) spawn_state=restored_spawn;
  else reset_spawns();
  restored_extra=false;
}

static void spawn_event(CpuState *c,const char *event,unsigned record,unsigned id,int x) {
  const char *directory=getenv("SMW_RENDER_DIAGNOSTICS");
  if(!directory || !*directory) return;
  static FILE *trace;
  if(!trace) {
    char path[1024];snprintf(path,sizeof(path),"%s/spawns.csv",directory);
    trace=fopen(path,"w");
    if(trace) fprintf(trace,"frame,event,record,id,x,camera,load_flag\n");
  }
  if(trace) {
    unsigned camera=cpu_read8(c,0x7e,0x1a)|(cpu_read8(c,0x7e,0x1b)<<8);
    fprintf(trace,"%u,%s,%u,%u,%d,%u,%u\n",spawn_frame,event,record,id,x,camera,
            cpu_read8(c,0x7e,(uint16_t)(0x1938+record)));
    fflush(trace);
  }
}

static unsigned r8(CpuState *c,unsigned a) { return cpu_read8(c,0x7e,(uint16_t)a); }
static unsigned r16(CpuState *c,unsigned a) { return r8(c,a)|(r8(c,a+1)<<8); }
static bool active(CpuState *c) {
  return g_smw_video.enabled && g_smw_viewport.width>256 && r8(c,0x100)==0x14;
}
static int left_margin(CpuState *c) {
  return r8(c,0x5b)&1 ? g_smw_viewport.extra :
      SmwViewOffset(g_smw_viewport,r16(c,0x1a),(r8(c,0x5e)+1)*256);
}

/* Preset $11's ROM table ($07:F000) reserves $30-$7F for slots 6/7 and
 * gives slots 0-5 five tiles each at $80-$F7. $00-$27 belongs to Mario/cape,
 * not spare sprites. Only $F8/$FC remain free: one tile per extra Eerie.
 * Both Eerie variants ($38/$39) use SubSprGfx2Entry1's single-tile draw. */
static int ghost_house_spare(CpuState *c,unsigned id) {
  if(id!=0x38 && id!=0x39) return -1;
  if(r8(c,0x1692)!=0x11 || (r8(c,0x5b)&1)) return -1;
  for(unsigned slot=8;slot<=9;++slot)
    if(!r8(c,0x14c8+slot)) return (int)slot;
  return -1;
}
static bool placement_has_slot(CpuState *c,unsigned id,int x) {
  /* The ordinary loader returns from the WHOLE list on allocation failure.
   * With a wide sweep, that would also block later records using a different
   * reserved pool. Defer only the blocked record, using the ROM's own bounds.
   * Goal tape ($7B) may replace an occupied slot; leave that policy native. */
  if((id>=0xc9 && id<0xda) || id==0xe0 || id>=0xe1 || id==0x7b) return true;
  /* DA-DD/DF placed shells use the ordinary allocator too. DE's Eerie
   * factory uses its common pool: do not consume that trigger with zero
   * available slots. E0's platform factory and scene commands stay native. */
  unsigned memory=r8(c,0x1692);
  if(memory>=19) return true;
  int top=cpu_read8(c,2,0xa773+memory);
  unsigned bottom=cpu_read8(c,2,0xa7ac+memory);
  if(id==cpu_read8(c,2,0xa7d2+memory)) {
    top=cpu_read8(c,2,0xa786+memory);
    bottom=cpu_read8(c,2,0xa7bf+memory);
  }
  if(id==cpu_read8(c,2,0xa7e4+memory) && (id!=0x64 || (x&0x10))) {
    top=cpu_read8(c,2,0xa799+memory);bottom=255;
  }
  int end=bottom==255?-1:(int)bottom;
  if(top>=12 || end>=top) return true;
  for(int slot=top;slot>end;--slot)
    if(!r8(c,0x14c8+slot)) return true;
  return top==5 && bottom==255 && ghost_house_spare(c,id==0xde?0x39:id)>=0;
}

void SmwRendererDrawInfo(CpuState *c) {
  if(!active(c)) return;
  unsigned slot=c->X&0xffff;
  if(slot>=12) return;
  int x=(int16_t)((r8(c,0xe4+slot)|(r8(c,0x14e0+slot)<<8))-r16(c,0x1a));
  int left=left_margin(c);
  bool draw=x>=-left-64 && x<g_smw_viewport.width-left;
  if(draw) {
    unsigned first=r8(c,0x15ea+slot), end=256;
    for(unsigned i=0;i<12;++i) {
      unsigned next=r8(c,0x15ea+i);
      if(next>first && next<end) end=next;
    }
    int y=(int16_t)((r8(c,0xd8+slot)|(r8(c,0x14d4+slot)<<8))-r16(c,0x1c));
    SmwRendererRecordSprite(slot,x,y,first,end);
  }
  c->_flag_Z=draw?1:0;
  c->_flag_C=draw?0:1;
  cpu_write8(c,0x7e,0x15c4+slot,draw?0:1);
}

void SmwRendererGuestHook(CpuState *c,uint32_t pc) {
  static int fireball_x, wing_x;
  static bool wing_draw, baseball_draw;
  pc &= 0x7fffff;
  if(pc==0x0180B2 || pc==0x029B12) {
    SmwRendererEndActor();
    return;
  }
  if(!active(c)) return;
  if(pc==0x01A7F3) {
    /* MarioSprInteractRt has ORed its alternating-frame predicate with
     * $15A0. That byte also supplies native OAM X bit 8, so it must retain
     * its 256px meaning. Produce this contact decision from the actual
     * viewport instead; clipping and every interaction outcome stay native.
     * The tweaker that requests every-frame contact bypasses this site. */
    unsigned slot=c->X&0xffff;
    if(slot>=12 || !c->m_flag) return;
    int x=(int16_t)((r8(c,0xe4+slot)|(r8(c,0x14e0+slot)<<8))-r16(c,0x1a));
    int left=left_margin(c);
    unsigned result=((slot^r8(c,c->D+0x13))&1) |
        (x < -left || x >= g_smw_viewport.width-left);
    c->A=(c->A&0xff00)|result;
    c->_flag_Z=result==0;c->_flag_N=0;
    c->P=(c->P&~0x82)|(c->_flag_Z?0x02:0);
    return;
  }
  if(pc==0x00E498) {
    /* The body/cape helper's [-128,384) horizontal test precedes its OAM
     * write. Let it publish the piece at any signed X; host scanout clips
     * against the selected viewport. Hidden tiles, vertical culling and
     * invulnerability flashing still take their original paths. */
    c->_flag_C=0;c->P&=(uint8_t)~1u;
    return;
  }
  if(pc==0x00E49A) {
    /* A still holds the full 16-bit tile X just before STA $0300,Y.
     * Capture at the draw, before later player movement/camera updates.
     * OAM's ninth bit cannot distinguish right-side +256 from left -256. */
    unsigned index=c->Y&255;
    int x=(int16_t)c->A;
    unsigned pos=(r8(c,0x301+index)<<8)|(c->A&255);
    SmwRendererRecordOam(64+index/4,x,(uint16_t)pos,r16(c,0x302+index));
    return;
  }
  if(pc==0x0180AF || pc==0x029B0C) {
    SmwRendererBeginActor(pc==0x029B0C,c->X&0xffff);
    return;
  }
  if(pc==0x02A27E) {
    unsigned slot=c->X&255;
    if(slot>=10) return;
    int x=(int16_t)((r8(c,0x171f+slot)|(r8(c,0x1733+slot)<<8))-r16(c,0x1a));
    int left=g_smw_video.adaptive_spawns?left_margin(c):0;
    int right=g_smw_video.adaptive_spawns?g_smw_viewport.width-left:256;
    baseball_draw=x>=-left && x<right;
    c->_flag_Z=baseball_draw;
    return;
  }
  if(pc==0x02A2BE) {
    /* Only the successful draw converts Y into an OAM tile index. The
     * native return for an offscreen, approaching ball keeps it unconverted. */
    unsigned slot=c->X&255, piece=c->Y&255;
    if(baseball_draw && slot<10 && piece<64) {
      int x=(int16_t)((r8(c,0x171f+slot)|(r8(c,0x1733+slot)<<8))-r16(c,0x1a));
      SmwRendererRecordOam(piece,x,r16(c,0x200+piece*4),r16(c,0x202+piece*4));
    }
    return;
  }
  if(pc==0x01A393 || pc==0x02D3A6 || pc==0x03B78E) {
    /* The same post-STA draw decision used by the generated hooks. The
     * current engine can keep a caller interpreted across these helpers. */
    SmwRendererDrawInfo(c);
    return;
  }
  if(pc==0x019E6D) {
    /* DrawWingTiles has already subtracted the camera from both bytes.
     * Its BNE normally rejects every wing outside the native 256px view.
     * Keep the full signed coordinate and change only that draw decision. */
    unsigned index=c->Y&255;
    wing_x=(int16_t)(((c->A&255)<<8)|r8(c,0x300+index));
    int left=left_margin(c);
    wing_draw=wing_x+16>-left && wing_x<g_smw_viewport.width-left;
    c->_flag_Z=wing_draw;
    return;
  }
  if(pc==0x019E93) {
    /* Successful draws have converted Y from a byte offset to a tile index;
     * the culled branch reaches the same return with Y still unconverted. */
    unsigned piece=c->Y&255;
    if(wing_draw && piece<64)
      SmwRendererRecordOam(64+piece,wing_x,r16(c,0x300+piece*4),r16(c,0x302+piece*4));
    return;
  }
  if(pc==0x019F5A) {
    /* Record the actual single-tile draw before a composite sprite advances
     * its allocation (e.g. a jumping Piranha draws its head, then its stem).
     * A later sprite's inferred allocation span must not steal this tile. */
    unsigned slot=c->X&255, piece=c->Y&255;
    if(slot>=12 || piece>=64 || (r8(c,0x186c+slot)&1)) return;
    int x=(int16_t)((r8(c,0xe4+slot)|(r8(c,0x14e0+slot)<<8))-r16(c,0x1a));
    int y=(int16_t)((r8(c,0xd8+slot)|(r8(c,0x14d4+slot)<<8))-r16(c,0x1c));
    SmwRendererRecordSpriteTile(piece,x,y);
    return;
  }
  if(pc==0x02A1BE) {
    unsigned slot=c->X&255;
    if(slot>=10) return;
    fireball_x=(int16_t)((r8(c,0x171f+slot)|(r8(c,0x1733+slot)<<8))-r16(c,0x1a));
    int left=g_smw_video.adaptive_spawns?left_margin(c):0;
    int right=g_smw_video.adaptive_spawns?g_smw_viewport.width-left:256;
    c->_flag_Z=fireball_x>=-left && fireball_x<right;
    return;
  }
  if(pc==0x02A204) {
    unsigned index=c->Y&255;
    SmwRendererRecordOam(index/4,fireball_x,r16(c,0x200+index),r16(c,0x202+index));
    return;
  }
  if(pc==0x01B844) {
    unsigned index=c->Y&255;
    int x=(int16_t)(r16(c,c->D+4)-r16(c,0x1a));
    SmwRendererRecordOam(64+index/4,x,r16(c,0x300+index),r16(c,0x302+index));
    return;
  }
  if(!g_smw_video.adaptive_spawns || !g_smw_viewport.extra) return;
  if(pc==0x0180E5) {
    unsigned slot=c->X&0xffff;
    if(r8(c,0x1692)==0x11 && !(r8(c,0x5b)&1) && (slot==8 || slot==9))
      cpu_write8(c,0x7e,0x15ea+slot,(uint8_t)(0xf8+(slot-8)*4));
    return;
  }
  if(pc==0x02A916) {
    if((c->X&0xffff)==5 && r8(c,c->D+6)==255) {
      bool full=true;
      for(unsigned slot=0;slot<6;++slot) if(!r8(c,0x14c8+slot)) full=false;
      int spare=ghost_house_spare(c,r8(c,c->D+5));
      if(full && spare>=0) c->X=(uint16_t)spare;
    }
    return;
  }
  if(pc==0x02AFB3) {
    /* The five-Eerie factory uses the same six-slot pool through the common
     * finder. Only extend that factory; other dynamic spawners stay native. */
    if((c->Y&255)==255) {
      int spare=ghost_house_spare(c,0x39);
      if(spare>=0) {
        c->Y=(uint16_t)spare;c->A=(c->A&0xff00)|(uint16_t)spare;
        c->_flag_N=0;c->_flag_Z=0;
      }
    }
    return;
  }
  if(pc==0x02A826 && !(r8(c,0x5b)&1)) {
    /* At the left level edge, the native leftward frontier is negative and
     * BMI exits before visiting any records. The adaptive record selection
     * below remains valid even then, including while Mario stands still. */
    c->_flag_N=0;
  } else if(pc==0x02A82E && !(r8(c,0x5b)&1)) {
    /* Select every visible record on the native loader's ordinary sweep.
     * Keep native allocation, initialization, load flags and respawn policy.
     * Excluded records compare below $FF and continue instead of ending the
     * list early. No camera shifting, replayed game frames or forged OAM. */
    unsigned pointer=r16(c,c->D+0xce)|(r8(c,c->D+0xd0)<<16);
    if(spawn_state.list!=pointer) {
      reset_spawns();spawn_state.list=pointer;
    }
    /* The next record (including the terminator) is reached after allocation
     * finishes. A full sprite pool clears the attempted load flag, so it must
     * remain eligible for retry. Only successful loads consume the trigger. */
    unsigned previous=spawn_state.pending;
    spawn_state.pending=255;
    if(previous<128 && r8(c,0x1938+previous)) {
      spawn_state.visited[previous]=1;
      unsigned source=pointer+1+previous*3;
      unsigned a=cpu_read8(c,source>>16,source&65535);
      unsigned b=cpu_read8(c,(source+1)>>16,(source+1)&65535);
      unsigned id=cpu_read8(c,(source+2)>>16,(source+2)&65535);
      spawn_event(c,"loaded",previous,id,(((a&2)<<3)|(b&15))*256+(b&0xf0));
    }
    unsigned address=pointer+c->Y;
    unsigned a=cpu_read8(c,address>>16,address&65535);
    unsigned b=cpu_read8(c,(address+1)>>16,(address+1)&65535);
    unsigned id=cpu_read8(c,(address+2)>>16,(address+2)&65535);
    if(a==255) return;
    int x=(((a&2)<<3)|(b&15))*256+(b&0xf0);
    int camera=r16(c,0x1a), left=left_margin(c);
    bool visible=x>=camera-left-32 && x<camera+g_smw_viewport.width-left+32;
    /* DA-E0 are placed shells and groups; their native factories use $00/$01
     * as world X. CB-D9 generators and E1+ scene effects/scroll commands use
     * the current camera instead, so retain their native activation frontier. */
    if((id>=0xcb && id<0xda) || id>=0xe1) {
      unsigned direction=r8(c,0x55);
      int edge=(camera+(direction==0?-48:direction==2?288:0))&~15;
      visible=x==edge;
    } else if((c->X&0xffff)<128) {
      unsigned record=c->X&0xffff;
      if(!visible) {
        if(spawn_state.visited[record]) spawn_event(c,"rearmed",record,id,x);
        spawn_state.visited[record]=0;
      } else {
        if(r8(c,0x1938+record)) spawn_state.visited[record]=1;
        if(spawn_state.visited[record]) {
          /* Native transformations (e.g. a Koopa entering its shell) can clear
           * the flag without the trigger leaving the screen. Do not treat
           * that as another entry into the expanded activation region. */
          if(!r8(c,0x1938+record) && spawn_state.visited[record]!=2) {
            spawn_event(c,"suppressed",record,id,x);
            spawn_state.visited[record]=2;
          }
          visible=false;
        } else if(!placement_has_slot(c,id,x)) {
          spawn_event(c,"deferred",record,id,x);
          visible=false;
        } else spawn_state.pending=(uint8_t)record;
      }
    }
    if(visible && (c->X&0xffff)<128 && !r8(c,0x1938+(c->X&0xffff)))
      spawn_event(c,"candidate",c->X&0xffff,id,x);
    cpu_write8(c,0x7e,c->D,(uint8_t)(x&0xf0));
    cpu_write8(c,0x7e,c->D+1,visible?(uint8_t)(x>>8):255);
  } else if(pc==0x01AC7C || pc==0x02D076 || pc==0x03B8A8) {
    if(r8(c,0x5b)&1) return;
    unsigned slot=c->X&0xffff, side=c->Y&7;
    if(slot>=12) return;
    unsigned table=pc==0x01AC7C?0xac11:pc==0x02D076?0xd007:0xb83f;
    int bound=(int16_t)(cpu_read8(c,c->DB,table+side)|
                          (cpu_read8(c,c->DB,table+side+8)<<8));
    int delta=(int16_t)(bound+r16(c,0x1a)-(r8(c,0xe4+slot)|(r8(c,0x14e0+slot)<<8)));
    int left=left_margin(c);
    int extra=(side&1?left:g_smw_viewport.width-256-left)+32;
    int threshold=bound<=-64?extra:bound+64+extra;
    bool erase=side&1?delta>=threshold:delta<-extra;
    cpu_write8(c,0x7e,c->D,erase?128:0);
  }
}
void SmwRendererInstallHooks(void) {
  const uint32_t pcs[]={0x02A826,0x02A82E,0x01B844,0x01AC7C,0x02D076,0x03B8A8,0x02A1BE,0x02A204,
                        0x019E6D,0x019E93,0x019F5A,0x0180E5,0x02A916,0x02AFB3,
                        0x01A393,0x02D3A6,0x03B78E,
                        0x0180AF,0x0180B2,0x029B0C,0x029B12,0x02A27E,0x02A2BE,
                        0x00E498,0x00E49A,0x01A7F3};
  for(unsigned i=0;i<sizeof(pcs)/sizeof(*pcs);++i)
    interp_bridge_set_pre_opcode_hook(pcs[i],SmwRendererGuestHook);
}

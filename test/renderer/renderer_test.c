#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/saveload.h"
#include "smw_renderer.h"
#include "common_rtl.h"
uint8_t g_ram[0x20000];
static uint8_t rom[0x80000];
const uint8_t *g_rom=rom;
uint8 cpu_read8(CpuState *cpu,uint8 bank,uint16 addr) {
  (void)cpu;
  return bank==0x7e || (bank==0 && addr<0x2000) ? g_ram[addr] : rom[((bank&15)<<15)|(addr&0x7fff)];
}
void cpu_write8(CpuState *cpu,uint8 bank,uint16 addr,uint8 value) { (void)cpu; assert(bank==0x7e); g_ram[addr]=value; }
void interp_bridge_set_pre_opcode_hook(uint32_t pc,InterpPreOpcodeHook hook) { (void)pc;(void)hook; }
extern void SmwRendererGuestHook(CpuState *,uint32_t);
extern void SmwRendererDrawInfo(CpuState *);
static void word(unsigned a,unsigned value) { g_ram[a]=value;g_ram[a+1]=value>>8; }
static void geometry(void) {
  SmwVideoSettings s={true,true,0};
  const int cases[][3]={{800,600,256},{1920,1080,342},{3440,1440,458},{3840,1080,682},{10000,900,2134},{2759,777,682},{600,1000,256}};
  for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
    SmwViewport v=SmwCalculateViewport(&s,cases[i][0],cases[i][1],kSnesDisplayAspect_Crt4x3);
    assert(v.width==cases[i][2]);
    int x,y,w,h;SmwDestination(v,cases[i][0],cases[i][1],&x,&y,&w,&h);
    assert(w>0 && h>0 && x>=0 && y>=0 && x+w<=cases[i][0] && y+h<=cases[i][1]);
  }
  assert(SmwCalculateViewport(&s,0,0,kSnesDisplayAspect_Crt4x3).width==256);
  assert(SmwCalculateViewport(&s,2147483647,1,kSnesDisplayAspect_Crt4x3).width==SMW_RENDER_MAX_WIDTH);
  s.aspect=100.0/9;assert(SmwCalculateViewport(&s,640,480,kSnesDisplayAspect_Crt4x3).width==2134);
  s.enabled=false;assert(SmwCalculateViewport(&s,10000,900,kSnesDisplayAspect_Crt4x3).width==256);
  const int widths[] = {342,398,456};
  const double pixel_aspects[] = {7.0/6,1,7.0/8};
  for (int setting=0;setting<kSnesDisplayAspect_Count;++setting) {
    SnesDisplayAspect aspect=(SnesDisplayAspect)setting;
    s.enabled=true;s.aspect=0;
    SmwViewport fit=SmwCalculateViewport(&s,1920,1080,aspect);
    assert(fit.width==widths[setting] && fabs(fit.aspect-16.0/9)<1e-9);
    int x,y,w,h;SmwDestination(fit,1920,1080,&x,&y,&w,&h);
    assert(x==0 && y==0 && w==1920 && h==1080);
    assert(fabs((double)w*224/(h*fit.width)-pixel_aspects[setting])<.006);
    s.aspect=16.0/9;
    assert(SmwCalculateViewport(&s,800,600,aspect).width==widths[setting]);
    s.enabled=false;
    SmwViewport stock=SmwCalculateViewport(&s,1920,1080,aspect);
    assert(stock.width==256 && fabs(stock.aspect-256.0/224*pixel_aspects[setting])<1e-9);
  }
  SmwViewport v={2134,939,100.0/9};
  assert(SmwViewOffset(v,0,8192)==0);
  assert(SmwViewOffset(v,4096,8192)==939);
  assert(SmwViewOffset(v,7936,8192)==1878);
}
static void spawn(void) {
  CpuState cpu={0};cpu.Y=1;
  g_smw_video=(SmwVideoSettings){true,true,0};g_smw_viewport=(SmwViewport){2134,939,100.0/9};
  memset(g_ram,0,sizeof(g_ram));g_ram[0x100]=20;g_ram[0x5e]=31;
  /* Standing at x=0 must not return before scanning the extended view. */
  cpu._flag_N=1;SmwRendererGuestHook(&cpu,0x02A826);assert(cpu._flag_N==0);
  g_smw_video.adaptive_spawns=false;
  cpu._flag_N=1;SmwRendererGuestHook(&cpu,0x02A826);assert(cpu._flag_N==1);
  g_smw_video.adaptive_spawns=true;
  word(0xce,0x8000);g_ram[0xd0]=2;
  unsigned data=(2<<15)+1;
  /* Enemy at x=2048, beyond every hardware OAM representation. */
  rom[data]=0;rom[data+1]=8;rom[data+2]=4;
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==8 && g_ram[0]==0);
  /* 32px lookahead hides entry pop-in; the next column is excluded. */
  rom[data+1]=0x78;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==8);
  rom[data+1]=0x88;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  /* Original policy leaves the native loader's frontier completely alone. */
  g_smw_video.adaptive_spawns=false;g_ram[0]=0x20;g_ram[1]=1;
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[0]==0x20 && g_ram[1]==1);
  g_smw_video.adaptive_spawns=true;
  /* Generators/level commands still activate only at their native frontier. */
  rom[data+1]=8;rom[data+2]=0xe7;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  /* DA-E0 are placed shells/groups, not global generator commands. */
  for(unsigned id=0xda;id<=0xe0;++id) {
    rom[data+2]=id;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==8);
  }
  /* Fireballs use their actual host coordinate and honor original lifecycle. */
  cpu.X=0;g_ram[0x171f]=0;g_ram[0x1733]=8;
  SmwRendererGuestHook(&cpu,0x02A1BE);assert(cpu._flag_Z==1);
  g_smw_video.adaptive_spawns=false;
  SmwRendererGuestHook(&cpu,0x02A1BE);assert(cpu._flag_Z==0);
  g_smw_video.adaptive_spawns=true;
  /* Native GetDrawInfo culling cannot hide an object 2048px to the right. */
  g_ram[0xe4]=0;g_ram[0x14e0]=8;cpu.X=0;
  SmwRendererDrawInfo(&cpu);assert(g_ram[0x15c4]==0 && cpu._flag_Z==1);
  g_ram[0x14e0]=9;SmwRendererDrawInfo(&cpu);assert(g_ram[0x15c4]==1);
  /* All three ROM copies must also work when their caller is interpreted. */
  const uint32_t draw_pcs[]={0x01A393,0x02D3A6,0x03B78E};
  for(unsigned i=0;i<sizeof(draw_pcs)/sizeof(*draw_pcs);++i) {
    g_ram[0x14e0]=8;SmwRendererGuestHook(&cpu,draw_pcs[i]);
    assert(g_ram[0x15c4]==0 && cpu._flag_Z==1);
    g_ram[0x14e0]=9;SmwRendererGuestHook(&cpu,draw_pcs[i]);
    assert(g_ram[0x15c4]==1 && cpu._flag_Z==0);
  }
}
typedef struct StateBuffer { SaveLoadInfo base; bool writing; size_t position; uint8_t data[256]; } StateBuffer;
static void state_transfer(SaveLoadInfo *sli,void *data,size_t size) {
  StateBuffer *buffer=(StateBuffer *)sli;
  assert(buffer->position+size<=sizeof(buffer->data));
  if(buffer->writing) memcpy(buffer->data+buffer->position,data,size);
  else memcpy(data,buffer->data+buffer->position,size);
  buffer->position+=size;
}
static void spawn_lifecycle(void) {
  CpuState cpu={0};cpu.Y=1;
  memset(g_ram,0,sizeof(g_ram));memset(rom,0,sizeof(rom));
  g_smw_video=(SmwVideoSettings){true,true,0};g_smw_viewport=(SmwViewport){1118,431,1118.0/192};
  g_ram[0x100]=20;g_ram[0x5e]=31;word(0xce,0x8000);g_ram[0xd0]=2;
  unsigned data=(2<<15)+1;
  rom[data]=0x71;rom[data+1]=0x43;rom[data+2]=1;rom[data+3]=255; /* Koopa at x=832 */
  SmwRendererStateLoaded(6);
  /* Full pool: the native loader rolls the flag back to zero. Keep retrying. */
  for(int attempt=0;attempt<3;++attempt) {
    cpu.X=0;cpu.Y=1;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==3);
    cpu.X=1;cpu.Y=4;SmwRendererGuestHook(&cpu,0x02A82E);
  }
  cpu.X=0;cpu.Y=1;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==3);
  g_ram[0x1938]=1; /* successful native allocation */
  cpu.X=1;cpu.Y=4;SmwRendererGuestHook(&cpu,0x02A82E);
  g_ram[0x1938]=0; /* consumed by shell entry; source is still in the view */
  cpu.X=0;cpu.Y=1;
  for(int i=0;i<10;++i) { SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255); }
  StateBuffer saved={{state_transfer,NULL},true,0,{0}};
  SmwRendererSaveExtra(&saved.base);assert(saved.position==141);
  /* Leaving the region rearms the source, so a genuine return may respawn. */
  word(0x1a,3000);SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  word(0x1a,0);SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==3);
  /* Rewind to the consumed record with a clear native flag: remain blocked. */
  saved.writing=false;saved.position=0;
  SmwRendererLoadExtra(&saved.base,6);SmwRendererStateLoaded(6);
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  /* Legacy saves rebuild from native loaded flags, without inheriting history. */
  SmwRendererStateLoaded(6);g_ram[0x1938]=1;
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  g_ram[0x1938]=0;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  /* Same level/list after a death or transition starts a fresh visit. */
  g_ram[0x100]=13;SmwRendererSpawnFrame();g_ram[0x100]=20;
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==3);
  /* A different list cannot inherit the old list's record guard either. */
  g_ram[0x1938]=1;SmwRendererGuestHook(&cpu,0x02A82E);
  g_ram[0x1938]=0;word(0xce,0x8100);memcpy(rom+data+256,rom+data,4);
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==3);
}
static void ghost_house_capacity(void) {
  CpuState cpu={0};cpu.X=5;
  memset(g_ram,0,sizeof(g_ram));memset(rom,0,sizeof(rom));
  g_smw_video=(SmwVideoSettings){true,true,0};g_smw_viewport=(SmwViewport){558,151,558.0/192};
  g_ram[0x100]=20;g_ram[0x5e]=14;g_ram[0x1692]=0x11;g_ram[6]=255;g_ram[5]=0x39;
  for(int i=0;i<6;++i)g_ram[0x14c8+i]=8;
  SmwRendererGuestHook(&cpu,0x02A916);assert(cpu.X==8);
  g_ram[0x14c8+8]=8;cpu.X=5;
  SmwRendererGuestHook(&cpu,0x02A916);assert(cpu.X==9);
  g_ram[0x14c8+9]=8;cpu.X=5;
  SmwRendererGuestHook(&cpu,0x02A916);assert(cpu.X==5); /* retain reserved 6/7 */
  g_ram[0x14c8+8]=0;cpu.X=7;g_ram[6]=5;
  SmwRendererGuestHook(&cpu,0x02A916);assert(cpu.X==7); /* Fishin' Boo pool */
  cpu.Y=255;cpu.A=0x12ff;cpu._flag_N=1;
  SmwRendererGuestHook(&cpu,0x02AFB3);
  assert(cpu.Y==8 && cpu.A==0x1208 && !cpu._flag_N && !cpu._flag_Z);
  for(int i=6;i<=11;++i) {
    cpu.X=i;g_ram[0x15ea+i]=0xcc;SmwRendererGuestHook(&cpu,0x0180E5);
    assert(g_ram[0x15ea+i]==(i==8?0xf8:i==9?0xfc:0xcc));
  }
  g_smw_video.adaptive_spawns=false;cpu.X=8;g_ram[0x15ea+8]=0xcc;
  SmwRendererGuestHook(&cpu,0x0180E5);assert(g_ram[0x15ea+8]==0xcc);
  cpu.X=5;g_ram[6]=255;SmwRendererGuestHook(&cpu,0x02A916);assert(cpu.X==5);
  g_smw_video.adaptive_spawns=true;
  /* A free RAM slot is insufficient for a multi-tile hole or Koopa. The
   * two tail allocations each fit exactly one Eerie; never overlap Mario. */
  g_ram[0x14c8+8]=0;g_ram[0x14c8+9]=0;g_ram[6]=255;
  const unsigned ids[]={0x38,0x39,0x52,0x05,0xda};
  for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);++i) {
    cpu.X=5;g_ram[5]=ids[i];SmwRendererGuestHook(&cpu,0x02A916);
    assert(cpu.X==(i<2?8:5));
  }
  /* A blocked ordinary placement must not stop the reserved-pool record
   * after it. These are the ROM's preset-$11 bounds, with distinct pools. */
  word(0xce,0x8000);g_ram[0xd0]=2;SmwRendererStateLoaded(7);
  rom[0x10000+0x2773+17]=5;rom[0x10000+0x27ac+17]=255;
  rom[0x10000+0x27d2+17]=0xae;
  rom[0x10000+0x2786+17]=7;rom[0x10000+0x27bf+17]=5;
  rom[0x10001]=0x71;rom[0x10002]=0x91;rom[0x10003]=0x52;
  rom[0x10004]=0x30;rom[0x10005]=0xa1;rom[0x10006]=0xae;
  cpu.X=0;cpu.Y=1;
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  rom[0x10003]=0xda;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  rom[0x10003]=0x39;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==1);
  g_ram[0x14c8+8]=g_ram[0x14c8+9]=8;
  rom[0x10003]=0xde;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  cpu.X=1;cpu.Y=4;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==1);
  assert(!g_ram[0x1938] && !g_ram[0x1939]); /* native loader still owns allocation */
}
static Ppu test_ppu;
static uint8_t surface[2134*224*4], native[256*224*4];
static void objects(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  g_ram[0x100]=20;g_ram[0x5e]=31;
  g_smw_video=(SmwVideoSettings){true,true,0};g_smw_viewport=(SmwViewport){2134,939,100.0/9};
  test_ppu.inidisp=15;test_ppu.bgmode=1;test_ppu.screenEnabled[0]=16;
  test_ppu.cgram[129]=31;
  for(int y=0;y<8;++y)test_ppu.vram[y]=0x00ff;
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  test_ppu.oam[128]=100*256;test_ppu.oam[129]=0x3000;
  word(0x300,100*256);word(0x302,0x3000);
  SmwRendererRecordSprite(0,2048,100,0,4);
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,2134*4,native);
  const uint32_t *pixel=(const uint32_t *)surface;
  assert(pixel[100*2134+2048]==0xff0000);
  assert(pixel[100*2134]==0); /* Never alias x=2048 into native x=0. */
  /* Reused OAM with different attributes must not retain the far owner. */
  test_ppu.oam[129]=0x3400;test_ppu.cgram[161]=0x03e0;
  SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,2134*4,native);
  assert(pixel[100*2134+2048]==0);
  assert(pixel[100*2134]==0x00ff00);
  /* Yoshi's head and body call GetDrawInfo using the same sprite slot but
   * different draw origins and adjacent OAM pieces. Keep both in a margin. */
  g_smw_viewport=(SmwViewport){568,156,568.0/192};word(0x1a,512);
  test_ppu.oam[128]=100*256+176;test_ppu.oam[129]=0x3000;
  test_ppu.oam[130]=116*256+186;test_ppu.oam[131]=0x3400;
  test_ppu.highOam[16]=5;
  word(0x300,test_ppu.oam[128]);word(0x302,test_ppu.oam[129]);
  word(0x304,test_ppu.oam[130]);word(0x306,test_ppu.oam[131]);
  SmwRendererRecordSprite(0,-80,100,0,8);
  SmwRendererRecordSprite(0,-70,116,4,8);
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,568*4,native);
  assert(pixel[100*568+76]==0xff0000);
  assert(pixel[116*568+86]==0x00ff00);
}
static void sprite_parts(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  g_ram[0x100]=20;g_ram[0x5e]=31;
  g_smw_video=(SmwVideoSettings){true,true,0};
  g_smw_viewport=(SmwViewport){2134,939,100.0/9};word(0x1a,4096);
  CpuState cpu={0};cpu._flag_C=1;cpu._flag_N=1;
  const int positions[]={-1000,-955,-954,-939,-52,0,256,1024,1194,1195};
  for(unsigned policy=0;policy<2;++policy) {
    g_smw_video.adaptive_spawns=policy;
    for(unsigned i=0;i<sizeof(positions)/sizeof(*positions);++i) {
      int x=positions[i];cpu.A=0xa500|(((uint16_t)x>>8)&255);
      cpu.Y=0;word(0x300,100*256+(x&255));
      unsigned a=cpu.A;
      SmwRendererGuestHook(&cpu,0x019E6D);
      assert(cpu._flag_Z==(x+16>-939 && x<1195));
      assert(cpu.A==a && cpu._flag_C==1 && cpu._flag_N==1 && cpu.Y==0);
    }
  }
  test_ppu.inidisp=15;test_ppu.bgmode=1;test_ppu.screenEnabled[0]=16;
  test_ppu.cgram[129]=31;test_ppu.cgram[161]=0x03e0;
  for(int y=0;y<8;++y)test_ppu.vram[y]=0xff;
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  cpu.A=4;word(0x300,0x6400);word(0x302,0x3000);
  SmwRendererGuestHook(&cpu,0x019E6D);SmwRendererGuestHook(&cpu,0x019E93);
  test_ppu.oam[128]=0x6400;test_ppu.oam[129]=0x3000;
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,2134*4,native);
  const uint32_t *pixel=(const uint32_t *)surface;
  assert(pixel[100*2134+1963]==0xff0000 && pixel[100*2134+939]==0);
  /* Exact head draw survives a later neighbour claiming its allocation span,
   * and uses the caller's FINAL tile/attributes rather than the generic ones. */
  g_smw_viewport=(SmwViewport){558,151,558.0/192};word(0x1a,2284);
  g_ram[0xe4]=2232&255;g_ram[0x14e0]=2232>>8;g_ram[0xd8]=100;
  word(0x300,0x64cc);word(0x302,0x3000);
  SmwRendererRecordSprite(0,-52,100,0,4);
  SmwRendererGuestHook(&cpu,0x019F5A);
  SmwRendererRecordSprite(1,308,100,0,4);
  word(0x302,0x3400);test_ppu.oam[128]=0x64cc;test_ppu.oam[129]=0x3400;
  test_ppu.highOam[16]=1;
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,558*4,native);
  assert(pixel[100*558+99]==0x00ff00 && pixel[100*558+459]==0);
  /* The native vertical culler hides tiles using x=$180, not y=$F0. */
  g_ram[0x186c]=1;word(0x300,0x6480);test_ppu.oam[128]=0x6480;
  SmwRendererGuestHook(&cpu,0x019F5A);
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,558*4,native);
  for(int x=0;x<558;++x)assert(pixel[100*558+x]==0);
  /* Unowned pieces outside the native view must remain hidden, including
   * short negative coordinates that fit within a larger sprite's footprint. */
  SmwRendererResetScene();word(0x300,0x64f0);test_ppu.oam[128]=0x64f0;
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,558*4,native);
  for(int x=0;x<558;++x)assert(pixel[100*558+x]==0);
}
static void completed_draws(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  SmwRendererResetScene();g_ram[0x100]=20;g_ram[0x5e]=31;
  g_smw_video=(SmwVideoSettings){true,true,0};g_smw_viewport=(SmwViewport){2134,939,100.0/9};
  test_ppu.inidisp=15;test_ppu.bgmode=1;test_ppu.screenEnabled[0]=16;
  test_ppu.cgram[129]=31;test_ppu.cgram[161]=0x03e0;
  for(int y=0;y<8;++y)test_ppu.vram[y]=0xff;
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  /* More than one hardware OAM's worth of draws reuse the SAME guest tile.
   * Capture the caller's final palette, not the earlier helper's attributes. */
  for(unsigned i=0;i<150;++i) {
    unsigned x=16+i*8,pos=100*256+(x&255);
    SmwRendererBeginActor(0,i%12);
    word(0x300,pos);word(0x302,0x3000);
    SmwRendererRecordOam(64,(int)x,(uint16_t)pos,0x3000);
    word(0x302,0x3400);
    SmwRendererEndActor();
  }
  test_ppu.oam[128]=(uint16_t)(100*256+((16+149*8)&255));test_ppu.oam[129]=0x3400;
  const uint32_t *pixel=(const uint32_t *)surface;
  for(unsigned pass=0;pass<2;++pass) {
    if(pass)g_ram[0x100]=0x0f; /* fade reuses the complete display list */
    SmwRendererLatchFrame();SmwRendererBeginFrame();
    for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
    SmwRendererDraw(surface,2134*4,native);
    for(unsigned i=0;i<150;++i)assert(pixel[100*2134+16+i*8]==0x00ff00);
  }
  SmwRendererResetScene();g_ram[0x100]=20;
  test_ppu.oam[128]=0xf000;word(0x300,0xf000);
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,2134*4,native);
  assert(pixel[100*2134+16]==0 && pixel[100*2134+1208]==0);
}
static void transitions(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  g_smw_video=(SmwVideoSettings){true,true,0};
  g_smw_viewport=(SmwViewport){558,151,558.0/192};g_ram[0x5e]=31;
  test_ppu.bgmode=1;test_ppu.screenEnabled[0]=16;
  test_ppu.cgram[0]=31;test_ppu.cgram[129]=0x03e0;
  for(int y=0;y<8;++y)test_ppu.vram[y]=0xff;
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  for(int i=0;i<256*224;++i)((uint32_t *)native)[i]=0x0000ff;
  word(0x300,0x6490);word(0x302,0x3000);
  test_ppu.oam[128]=0x6490;test_ppu.oam[129]=0x3000;
  const int outgoing[]={0x0b,0x0f,0x15,0x18};
  for(unsigned mode=0;mode<sizeof(outgoing)/sizeof(*outgoing);++mode) {
    SmwRendererResetScene();g_ram[0x100]=0x14;
    SmwRendererRecordOam(64,400,0x6490,0x3000);SmwRendererLatchFrame();
    g_ram[0x100]=outgoing[mode];
    for(int brightness=15;brightness>=0;--brightness) {
      test_ppu.inidisp=brightness;
      SmwRendererLatchFrame();SmwRendererBeginFrame();
      for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
      SmwRendererDraw(surface,558*4,native);
      const uint32_t *pixel=(const uint32_t *)surface;
      assert(pixel[100*558+550]==(unsigned)(255*brightness/15)<<16);
      assert(pixel[100*558+400]==(unsigned)(255*brightness/15)<<8);
    }
  }
  /* A newly loaded level fades in wide. An overworld/title fade sharing a
   * mode number must stay native, and save loading cannot inherit a scene. */
  const int modes[]={0x12,0x13,0x14,0x0c,0x0d,0x0e,0x0b,0x0f};
  for(unsigned i=0;i<sizeof(modes)/sizeof(*modes);++i) {
    g_ram[0x100]=modes[i];test_ppu.inidisp=15;
    SmwRendererLatchFrame();SmwRendererBeginFrame();
    for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
    SmwRendererDraw(surface,558*4,native);
    const uint32_t *pixel=(const uint32_t *)surface;
    assert(pixel[80*558+550]==(modes[i]==0x13 || modes[i]==0x14 ? 0xff0000u : 0));
  }
  memset(native,0,sizeof(native));SmwRendererResetScene();
}
static void map16(void) {
  memset(g_ram,0,sizeof(g_ram));memset(rom,0,sizeof(rom));g_ram[0x5e]=31;
  rom[0x3da8]=0x00;rom[0x3da9]=0xc0; /* mode-0 screen table -> $00:C000 */
  rom[0x4000]=0x00;rom[0x4001]=0xc8;
  rom[0x4003]=0xb0;rom[0x4004]=0xc9;
  g_ram[0xc800]=1;g_ram[0xc9b0]=1;word(0xfc0,0x9000);
  unsigned base=(13<<15)+0x1000;
  for(unsigned i=0;i<8;++i)rom[base+i]=i+1;
  uint16_t tile=0;
  assert(SmwRendererMapTile(g_ram,0,0,0,&tile) && tile==0x0201);
  assert(SmwRendererMapTile(g_ram,0,0,8,&tile) && tile==0x0403);
  assert(SmwRendererMapTile(g_ram,0,8,0,&tile) && tile==0x0605);
  assert(SmwRendererMapTile(g_ram,0,8,8,&tile) && tile==0x0807);
  assert(SmwRendererMapTile(g_ram,0,256,0,&tile) && tile==0x0201);
  assert(!SmwRendererMapTile(g_ram,0,-1,0,&tile));
  assert(!SmwRendererMapTile(g_ram,0,0,432,&tile));
}
static void hud(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  g_ram[0x100]=20;g_ram[0x5e]=31;
  g_smw_video=(SmwVideoSettings){true,true,0};
  test_ppu.inidisp=15;test_ppu.bgmode=1;test_ppu.screenEnabled[0]=4;
  test_ppu.bgXsc[2]=0x20;
  test_ppu.cgram[1]=31;test_ppu.cgram[5]=0x03e0;test_ppu.cgram[9]=0x7c00;
  for(int y=0;y<8;++y)test_ppu.vram[8+y]=0xff;
  /* Distinct glyphs in the actual left, reserve-box and right HUD groups. */
  test_ppu.vram[0x2000+32+2]=1;
  test_ppu.vram[0x2000+32+15]=1|(1<<10);
  test_ppu.vram[0x2000+32+23]=1|(2<<10);
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  /* Reserve-item sprite must follow the centered BG3 box too. */
  g_ram[0xdc2]=1;test_ppu.oam[112]=0x0f78;test_ppu.oam[113]=0x3024;
  test_ppu.cgram[129]=31;
  for(int y=0;y<8;++y)test_ppu.vram[0x24*16+y]=0xff;
  const int widths[]={342,682,2134};
  for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i) for(int camera=0;camera<=4096;camera+=4096) {
    int width=widths[i],extra=(width-256)/2;
    word(0x1a,camera);g_smw_viewport=(SmwViewport){width,extra,width/192.0};
    test_ppu.screenEnabled[0]=4|16;
    SmwRendererLatchFrame();SmwRendererBeginFrame();
    for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
    SmwRendererDraw(surface,width*4,native);
    const uint32_t *pixel=(const uint32_t *)surface;
    assert(pixel[10*width+16]==0xff0000);
    assert(pixel[10*width+120+extra]==0x00ff00);
    assert(pixel[10*width+184+width-256]==0x0000ff);
    assert(pixel[10*width+120]==0);
    assert(pixel[16*width+120+extra]==0xff0000);
    assert(pixel[16*width+120]==0);
  }
}
static void parallax(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  g_ram[0x100]=20;g_ram[0x5e]=31;
  g_smw_video=(SmwVideoSettings){true,true,0};
  test_ppu.inidisp=15;test_ppu.bgmode=1;test_ppu.screenEnabled[0]=2;
  test_ppu.bgXsc[1]=0x20;
  /* Every source pixel identifies its position in a repeating 256px strip.
   * Exercise the composed pixels, including odd camera positions, both level
   * edges and a per-scanline offset that must survive the projection. */
  for(int i=1;i<16;++i)test_ppu.cgram[i]=i;
  for(int ty=0;ty<32;++ty)for(int tx=0;tx<32;++tx)
    test_ppu.vram[0x2000+ty*32+tx]=tx;
  for(int tile=0;tile<32;++tile)for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
    unsigned value=1+(tile*8+x)%15;
    for(int bit=0;bit<4;++bit) if(value&(1u<<bit))
      test_ppu.vram[tile*16+y+(bit/2)*8] |= 1u<<(7-x+(bit%2)*8);
  }
  const int widths[]={342,1118,2134};
  for(unsigned w=0;w<sizeof(widths)/sizeof(*widths);++w) {
    int width=widths[w],extra=(width-256)/2,last=8192-width;
    const int cameras[]={0,1,extra-1,extra,extra+1,1023,4096,last+extra-1,last+extra,last+extra+1,7936};
    for(unsigned c=0;c<sizeof(cameras)/sizeof(*cameras);++c)for(int setting=0;setting<3;++setting) {
      int camera=cameras[c],origin=camera-extra;
      if(origin<0)origin=0;
      if(origin>last)origin=last;
      word(0x1a,camera);g_ram[0x1413]=setting;
      g_smw_viewport=(SmwViewport){width,extra,width/192.0};
      SmwRendererLatchFrame();
      /* The simulation advances after NMI; presentation must keep the camera
       * belonging to the uploaded tiles even across either clamp threshold. */
      word(0x1a,camera+4);
      SmwRendererBeginFrame();
      for(int y=1;y<=224;++y) {
        test_ppu.hScroll[1]=37+(setting==0?0:setting==1?camera:camera/2)+(y>100?11:0);
        SmwRendererCaptureLine(&test_ppu,y);
      }
      SmwRendererDraw(surface,width*4,native);
      const uint32_t *pixel=(const uint32_t *)surface;
      int phase=37+(setting==0?0:setting==1?origin:origin/2);
      for(int y=80;y<=120;y+=40)for(int x=0;x<width;++x) {
        unsigned red=1+((x+phase+(y>100?11:0))&255)%15;
        assert(pixel[y*width+x]==((red<<3)|(red>>2))<<16);
      }
    }
  }
  /* Layer-2 terrain keeps its full world projection even if a scroll command
   * changes the setting; never apply decorative-background compensation. */
  g_ram[0x1925]=1;g_ram[0x1413]=2;word(0x1a,700);
  g_smw_viewport=(SmwViewport){1118,431,1118.0/192};test_ppu.hScroll[1]=350;
  SmwRendererLatchFrame();SmwRendererBeginFrame();
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,1118*4,native);
  for(int x=0;x<256;++x) {
    unsigned red=1+((x+350)&255)%15;
    assert(((uint32_t *)surface)[80*1118+431+x]==((red<<3)|(red>>2))<<16);
  }
}
static void camera_timing(void) {
  /* Reuse parallax's uniquely colored tile strip as foreground terrain. */
  g_ram[0x1925]=0;test_ppu.screenEnabled[0]=1|16;
  test_ppu.bgXsc[0]=test_ppu.bgXsc[1];test_ppu.cgram[129]=0x7c00;
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  test_ppu.oam[128]=0x6428;test_ppu.oam[129]=0x3040;
  for(int y=0;y<8;++y)test_ppu.vram[64*16+y]=0xff;
  const int widths[]={558,2134}, cameras[]={0,24,28,150,151,152,938,939,940,1023,1024};
  for(unsigned w=0;w<sizeof(widths)/sizeof(*widths);++w)
    for(unsigned c=0;c<sizeof(cameras)/sizeof(*cameras);++c) {
      int camera=cameras[c],width=widths[w],extra=(width-256)/2;
      int origin=camera>extra?camera-extra:0,offset=camera-origin;
      g_smw_viewport=(SmwViewport){width,extra,width/192.0};word(0x1a,camera);
      SmwRendererRecordOam(64,40,0x6428,0x3040);
      SmwRendererLatchFrame();
      /* Upload the latched scene, then vary the NEXT simulation camera. */
      test_ppu.hScroll[0]=camera&1023;
      const int deltas[]={4,1,2,0,-1};
      for(unsigned d=0;d<sizeof(deltas)/sizeof(*deltas);++d) {
        word(0x1a,camera+deltas[d]);
        SmwRendererBeginFrame();
        for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
        SmwRendererDraw(surface,width*4,native);
        const uint32_t *pixel=(const uint32_t *)surface;
        for(int x=0;x<256;++x) {
          unsigned red=1+((x+camera)&255)%15;
          assert(pixel[80*width+offset+x]==((red<<3)|(red>>2))<<16);
        }
        assert(pixel[100*width+offset+40]==0x0000ff);
      }
    }
}
static void pipe_variants(void) {
  memset(g_ram,0,sizeof(g_ram));memset(rom,0,sizeof(rom));g_ram[0x5e]=31;
  rom[0x3da9]=rom[0x3de9]=0xc0; /* both layers -> fixture screen table */
  for(unsigned screen=0;screen<8;++screen) {
    unsigned low=0xc800+screen*0x1b0;
    rom[0x4000+screen*3]=low;rom[0x4001+screen*3]=low>>8;
  }
  const unsigned bases[]={0x8ab0,0x84e0,0x8af0,0x8b30};
  for(unsigned variant=0;variant<4;++variant) {
    unsigned table=(5<<15)+0x776+variant*2;
    rom[table]=bases[variant];rom[table+1]=bases[variant]>>8;
    for(unsigned block=0;block<8;++block) for(unsigned part=0;part<4;++part) {
      unsigned address=(13<<15)+(bases[variant]&0x7fff)+block*8+part*2;
      rom[address]=block*4+part;rom[address+1]=variant*4+0x20;
    }
  }
  uint16_t tile;
  for(unsigned layer=0;layer<2;++layer) for(unsigned screen=0;screen<8;++screen)
    for(unsigned block=0;block<8;++block) for(unsigned current=0;current<4;++current) {
      unsigned low=0xc800+screen*0x1b0;
      g_ram[low]=0x33+block;g_ram[0x10000+low]=1;
      word(0xfbe + (0x133+block)*2,bases[current]+block*8);
      for(unsigned part=0;part<4;++part) {
        assert(SmwRendererMapTile(g_ram,layer,screen*256+(part/2)*8,(part%2)*8,&tile));
        assert(tile==((0x20+(screen%4)*4)<<8)+block*4+part);
      }
    }
  /* Vertical levels and background definitions keep the native lookup. */
  g_ram[0xc800]=0x33;g_ram[0x1c800]=1;word(0xfbe + 0x133*2,0x84e0);
  g_ram[0x5b]=3;
  for(unsigned layer=0;layer<2;++layer) {
    assert(SmwRendererMapTile(g_ram,layer,0,0,&tile) && tile==0x2400);
  }
  g_ram[0x5b]=0;g_ram[0x1931]=0x10;
  rom[(5<<15)+0x4e0]=0x34;rom[(5<<15)+0x4e1]=0x12;
  assert(SmwRendererMapTile(g_ram,0,0,0,&tile) && tile==0x1234);
}
int main(void) { geometry();spawn();spawn_lifecycle();ghost_house_capacity();objects();sprite_parts();completed_draws();transitions();map16();pipe_variants();hud();parallax();camera_timing();puts("geometry, spawn lifecycle/save state and ghost-house capacity, signed OAM/sprite parts, independent sprite draws, transitions, Map16/pipe variants, anchored HUD, parallax and presentation camera timing: passed");return 0; }

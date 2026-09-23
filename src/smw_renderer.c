/* Host renderer. Mode-1 tile decoding/composition follows the sibling
 * Super Metroid custom renderer; level addressing and object ownership are SMW.
 * Per-line immutable snapshots retain IRQ/HDMA changes without replaying logic. */
#include "smw_renderer.h"
#include "common_rtl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct RasterLine {
  uint8_t regs[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256], vram[0x8000];
  uint8_t high[32];
} RasterLine;
typedef struct OamOwner { int x; uint16_t position, attr; bool valid; } OamOwner;
static RasterLine lines[224];
static PpuOverlayCapture obj_capture[224];
static uint8_t frame_ram[0x20000];
static OamOwner pending[128], latched[128];
typedef struct SpriteOwner { int x,y; bool valid, exact; } SpriteOwner;
static SpriteOwner sprite_owners[64];
typedef struct SpriteDraw {
  OamOwner image;
  unsigned slot, high, kind, actor, order;
} SpriteDraw;
typedef struct DrawList { SpriteDraw *items; size_t count, capacity; } DrawList;
static DrawList pending_draws, latched_draws;
static size_t draw_start[129];
static struct {
  bool active;
  unsigned kind, index;
  uint8_t before[512];
  OamOwner exact[128];
  SpriteOwner inferred[64];
} actor_draw;
static bool actors_ran;
static unsigned captured;
static int native_x;
static bool level_scene;
static Ppu raster;
static uint16_t objects[SMW_RENDER_MAX_WIDTH];
static uint16_t object_colors[SMW_RENDER_MAX_WIDTH];
static uint64_t object_order[SMW_RENDER_MAX_WIDTH];
static uint32_t native_control[256*224];
int SmwRendererNativeOffset(void) { return g_smw_video.enabled ? native_x : 0; }
static bool world_layer(unsigned layer) {
  return layer == 0 || (layer == 1 && frame_ram[0x1925] < 32 &&
                       (0x800081feu & (1u << frame_ram[0x1925])));
}
static unsigned read16(const uint8_t *r, unsigned a) { return r[a] | (r[a+1] << 8); }
static int background_shift(void) {
  if (world_layer(1) || (frame_ram[0x5b] & 1)) return 0;
  /* $00:F79D: BG2 follows BG1 at zero, full or half speed. Project that
   * camera component from the visible origin, not the embedded 256px view.
   * Otherwise the left clamp drags scenery with Mario until native_x stops
   * growing. The difference retains captured animation/IRQ/HDMA offsets and
   * native integer rounding, with no history to reset on load or resize. */
  unsigned setting = frame_ram[0x1413];
  if (!setting) return native_x;
  if (setting == 1) return 0;
  int camera = read16(frame_ram,0x1a), origin = camera-native_x;
  return native_x + origin/2 - camera/2;
}
static unsigned rom16(unsigned bank, unsigned a) {
  if (!g_rom || a < 0x8000 || a > 0xfffe || bank > 15) return 0;
  return read16(g_rom, (bank << 15) | (a & 0x7fff));
}
void SmwRendererRecordOam(unsigned slot, int x, uint16_t pos, uint16_t attr) {
  if (slot < 128) {
    pending[slot] = (OamOwner){x, pos, attr, true};
    if (actor_draw.active) actor_draw.exact[slot]=pending[slot];
  }
}
void SmwRendererRecordSprite(unsigned slot, int x, int y, unsigned first, unsigned end) {
  if (slot < 12 && first < end && end <= 256)
    for (unsigned index=first;index<end;index+=4) {
      if (!sprite_owners[index/4].exact)
        sprite_owners[index/4] = (SpriteOwner){x,y,true,false};
      if (actor_draw.active && !actor_draw.inferred[index/4].exact)
        actor_draw.inferred[index/4] = (SpriteOwner){x,y,true,false};
    }
}
void SmwRendererRecordSpriteTile(unsigned piece, int x, int y) {
  /* The draw identifies this exact tile. Adjacent sprites' inferred reserved
   * spans cannot replace it, but callers may still finish its tile/attributes.
   * Bind the final OAM image at NMI just like the other generic graphics. */
  if (piece < 64) {
    sprite_owners[piece] = (SpriteOwner){x,y,true,true};
    if (actor_draw.active) actor_draw.inferred[piece]=sprite_owners[piece];
  }
}
static void append_draw(SpriteDraw draw) {
  if (pending_draws.count==pending_draws.capacity) {
    size_t capacity=pending_draws.capacity?pending_draws.capacity*2:128;
    SpriteDraw *items=realloc(pending_draws.items,capacity*sizeof(*items));
    if (!items) abort();
    pending_draws.items=items;pending_draws.capacity=capacity;
  }
  draw.order=(unsigned)pending_draws.count;
  pending_draws.items[pending_draws.count++]=draw;
}
void SmwRendererBeginActor(unsigned kind,unsigned index) {
  SmwRendererEndActor();
  if (kind>1 || index>=(kind?10u:12u)) return;
  memset(&actor_draw,0,sizeof(actor_draw));
  actor_draw.active=true;actor_draw.kind=kind;actor_draw.index=index;
  memcpy(actor_draw.before,g_ram+0x200,sizeof(actor_draw.before));
  actors_ran=true;
}
void SmwRendererEndActor(void) {
  if (!actor_draw.active) return;
  /* Draw helpers often return before callers replace tiles, palette or size.
   * Capture at the object boundary, while its completed image still exists.
   * A later object may reuse these same guest bytes: both host draws survive. */
  for (unsigned slot=0;slot<128;++slot) {
    unsigned pos=read16(g_ram,0x200+slot*4),attr=read16(g_ram,0x202+slot*4);
    if ((pos>>8)==240) continue;
    const OamOwner *exact=&actor_draw.exact[slot];
    int x;
    if (exact->valid) {
      x=exact->x+(int8_t)((pos&255)-(exact->position&255));
    } else {
      if (slot<64) continue;
      const SpriteOwner *owner=&actor_draw.inferred[slot-64];
      if (!owner->valid || (!owner->exact &&
          !memcmp(actor_draw.before+slot*4,g_ram+0x200+slot*4,4))) continue;
      int dx=(int8_t)((pos&255)-(owner->x&255));
      int dy=(int8_t)((pos>>8)-(owner->y&255));
      if (abs(dx)>64 || abs(dy)>64) continue;
      x=owner->x+dx;
    }
    pending[slot]=(OamOwner){x,(uint16_t)pos,(uint16_t)attr,true};
    append_draw((SpriteDraw){pending[slot],slot,g_ram[0x420+slot]&3,
                            actor_draw.kind,actor_draw.index,0});
  }
  actor_draw.active=false;
}
static int compare_draw(const void *a,const void *b) {
  const SpriteDraw *x=a,*y=b;
  if (x->slot!=y->slot) return x->slot<y->slot?-1:1;
  return x->order<y->order?-1:x->order!=y->order;
}
void SmwRendererResetScene(void) {
  level_scene=false;
  memset(pending,0,sizeof(pending));
  memset(latched,0,sizeof(latched));
  memset(sprite_owners,0,sizeof(sprite_owners));
  actor_draw.active=false;actors_ran=false;
  pending_draws.count=latched_draws.count=0;
  memset(draw_start,0,sizeof(draw_start));
}
void SmwRendererLatchFrame(void) {
  SmwRendererEndActor();
  bool reused_oam=!memcmp(frame_ram+0x200,g_ram+0x200,512);
  /* NMI uploads this scene's scroll and OAM before the next simulation tick.
   * Keep the camera, Map16 and sprite metadata from the same scene. Reading
   * live RAM at scanout instead mixes the next camera with the uploaded tiles,
   * making a clamped viewport move by the changing per-tick camera delta. */
  if (g_smw_video.enabled) memcpy(frame_ram,g_ram,sizeof(frame_ram));
  unsigned mode=frame_ram[0x100];
  if (mode==0x13 || mode==0x14) level_scene=true;
  else if (mode!=0x0b && mode!=0x0f && mode!=0x15 && mode!=0x18)
    level_scene=false;
  /* Shared fade modes also serve the title and overworld. Retain the actual
   * outgoing scene until its loader takes over, rather than switching the
   * level to a centered native-width image on the first fade frame. */
  bool fading_level=level_scene && mode!=0x13 && mode!=0x14;
  bool reuse_draws=level_scene && !actors_ran && !pending_draws.count && reused_oam;
  /* SMW's small generic graphics paths do not call FinishOAMWrite. Their
   * GetDrawInfo call identifies the sprite's reserved OAM allocation. Bind
   * the completed pieces before NMI, with signed deltas to that draw origin.
   * Each piece retains its own draw origin: multi-pass sprites such as Yoshi
   * draw the head first, then shift the same sprite's origin for the body.
   * Store the exact final image so later OAM reuse cannot inherit an owner. */
  for (unsigned piece=0;piece<64;++piece) {
    const SpriteOwner *owner=&sprite_owners[piece];
    if (!owner->valid) continue;
    unsigned slot=64+piece, index=piece*4;
    if (pending[slot].valid) continue;
    unsigned pos=read16(g_ram,0x300+index), attr=read16(g_ram,0x302+index);
    if ((pos>>8)==240) continue;
    int dx=(int8_t)((pos&255)-(owner->x&255));
    int dy=(int8_t)((pos>>8)-(owner->y&255));
    if (abs(dx)>64 || abs(dy)>64) continue;
    pending[slot]=(OamOwner){owner->x+dx,(uint16_t)pos,(uint16_t)attr,true};
  }
  /* Fade routines reuse the last uploaded OAM without drawing it again. Keep
   * its full coordinates only while that exact image still belongs to this
   * outgoing scene; loaders and save loads clear the association. */
  if (fading_level || reuse_draws) for (unsigned slot=0;slot<128;++slot) {
    if (!pending[slot].valid && latched[slot].valid &&
        latched[slot].position==read16(g_ram,0x200+slot*4) &&
        latched[slot].attr==read16(g_ram,0x202+slot*4))
      pending[slot]=latched[slot];
  }
  memcpy(latched, pending, sizeof(latched));
  memset(pending, 0, sizeof(pending));
  memset(sprite_owners,0,sizeof(sprite_owners));
  if (!reuse_draws) {
    DrawList swap=latched_draws;latched_draws=pending_draws;pending_draws=swap;
    if (latched_draws.count>1)
      qsort(latched_draws.items,latched_draws.count,sizeof(SpriteDraw),compare_draw);
    size_t next=0;
    for (unsigned slot=0;slot<=128;++slot) {
      while (next<latched_draws.count && latched_draws.items[next].slot<slot) ++next;
      draw_start[slot]=next;
    }
  }
  pending_draws.count=0;actors_ran=false;
}
void SmwRendererBeginFrame(void) {
  captured = 0;
  if (!g_smw_video.enabled) return;
  native_x = g_smw_viewport.extra;
  if (level_scene && !(frame_ram[0x5b] & 1))
    native_x = SmwViewOffset(g_smw_viewport, read16(frame_ram, 0x1a), (frame_ram[0x5e]+1)*256);
}
void SmwRendererCaptureLine(const Ppu *p, int line) {
  if (!g_smw_video.enabled || line < 1 || line > 224) return;
  RasterLine *l = &lines[line-1];
  obj_capture[line-1] = p->overlayCaptures[kPpuOverlaySource_Obj];
  memcpy(l->regs, p, sizeof(l->regs));
  memcpy(l->palette, p->cgram, sizeof(l->palette));
  memcpy(l->oam, p->oam, sizeof(l->oam));
  memcpy(l->high, p->highOam, sizeof(l->high));
  memcpy(l->vram, p->vram, sizeof(l->vram));
  ++captured;
}

bool SmwRendererMapTile(const uint8_t *ram, unsigned layer, int x, int y, uint16_t *tile) {
  unsigned mode = ram[0x1925];
  if (layer > 1 || mode >= 32 || x < 0 || y < 0) return false;
  bool vertical = (ram[0x5b] & (1u << layer)) != 0;
  unsigned screen, index;
  if (vertical) {
    if (x >= 512 || y >= 0x1c00) return false;
    screen = (unsigned)y >> 8;
    index = ((x >> 8) * 256) + ((y & 255) >> 4) * 16 + ((x & 255) >> 4);
  } else {
    if (x >= ((int)ram[0x5e] + 1) * 256 || y >= 432) return false;
    screen = (unsigned)x >> 8;
    index = (y >> 4) * 16 + ((x & 255) >> 4);
  }
  unsigned table = rom16(0, (layer ? 0xbde8 : 0xbda8) + mode * 2);
  if (!table) return false;
  unsigned low = rom16(0, table + screen * 3);
  /* Vanilla tables address parallel $7E/$7F Map16 planes. */
  if (low < 0xc800 || low + index > 0xffff) return false;
  unsigned block = ram[low+index] | (ram[0x10000+low+index] << 8);
  if (block >= 512) return false;
  unsigned address = read16(ram, 0xfbe + block * 2);
  /* $05877E rewrites the eight pipe pointers for the strip being streamed.
   * That temporary table is not valid for other screens in the expanded view.
   * Select the same ROM variant by this pipe's world screen, independently of
   * camera direction. Vertical levels retain their native, fixed pointers;
   * tilesets >= $10 use background definitions rather than these pipe tiles. */
  if (!vertical && ram[0x1931] < 0x10 && block >= 0x133 && block <= 0x13a)
    address = rom16(5, 0x8776 + (((unsigned)x >> 8) & 3) * 2) + (block - 0x133) * 8;
  address += (x & 8 ? 4 : 0) + (y & 8 ? 2 : 0);
  if (address < 0x8000 || address > 0xfffe) return false;
  *tile = (uint16_t)rom16(ram[0x1931] >= 0x10 ? 5 : 13, address);
  return true;
}
static unsigned tile_pixel(const uint16_t *vram, unsigned address, int x, int y, int bpp) {
  unsigned a = (address + y) & 0x7fff;
  unsigned shift = 7 - x;
  unsigned bits = vram[a] >> shift;
  unsigned pixel = (bits & 1) | ((bits >> 7) & 2);
  if (bpp == 4) {
    bits = vram[(a + 8) & 0x7fff] >> shift;
    pixel |= ((bits & 1) << 2) | ((bits >> 5) & 8);
  }
  return pixel;
}

static bool window_condition(unsigned mode, bool inside) {
  return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside);
}

static unsigned pixel_colour(const uint16_t *palette,uint16_t pixel,int x) {
  unsigned layer=(pixel>>8)&15;
  if(g_ppu->extraObjectCount && (layer==4 || layer==6) && (object_colors[x]&0x8000))
    return object_colors[x]&0x7fff;
  return palette[pixel&255];
}
static uint32_t colour(const Ppu *p, const uint16_t *palette, const uint8_t *brightness, uint16_t main,
                       uint16_t sub, bool inside, int x) {
  unsigned rgb = pixel_colour(palette,main,x), layer = (main >> 8) & 15;
  bool clipped = window_condition(p->cgwsel >> 6, inside);
  bool math = !window_condition((p->cgwsel >> 4) & 3, inside) &&
              ((p->cgadsub & 63) & (1u << layer));
  unsigned other = p->fixedColor;
  bool half = math && (p->cgadsub & 64) && !clipped;
  if (math && (p->cgwsel & 2)) {
    if ((sub & 255) != 0) other = pixel_colour(palette,sub,x);
    else half = false;
  }
  uint32_t result = 0;
  for (int component = 0; component < 3; ++component) {
    int c = clipped ? 0 : (rgb >> (component * 5)) & 31;
    if (math) {
      int second = (other >> (component * 5)) & 31;
      c += p->cgadsub & 128 ? -second : second;
      if (c < 0) c = 0;
      if (half) c /= 2;
      if (c > 31) c = 31;
    }
    c = brightness[c];
    result |= (uint32_t)c << (16 - component * 8);
  }
  return result;
}

static bool window(const Ppu *p, int layer, int x) {
  unsigned flags = (p->windowsel >> (layer*4)) & 15;
  if (!(flags&10)) return false;
  int left = -native_x, right = g_smw_viewport.width-native_x-1;
  int l1 = p->window1left ? p->window1left : left;
  int r1 = p->window1right == 255 ? right : p->window1right;
  int l2 = p->window2left ? p->window2left : left;
  int r2 = p->window2right == 255 ? right : p->window2right;
  bool a = (x >= l1 && x <= r1) != ((flags&1)!=0);
  bool b = (x >= l2 && x <= r2) != ((flags&4)!=0);
  if (!(flags&2)) return (flags&8) && b;
  if (!(flags&8)) return a;
  switch ((p->wbgobjlog >> (layer*2)) & 3) {
    case 0: return a || b; case 1: return a && b;
    case 2: return a != b; default: return a == b;
  }
}
static uint16_t background(const Ppu *p, const RasterLine *l, unsigned layer, int x, int y) {
  static const unsigned low[] = {8,7,1}, high[] = {12,11,3};
  /* Preserve the native groups: lives/bonus at left, reserve box centered,
   * TIME/coins/score at right. Sample their original tiles without stretching
   * glyphs or depending on the gameplay camera's position at a level edge. */
  if (layer == 2 && y <= 40) {
    int sx = x + native_x, width = g_smw_viewport.width;
    if (sx < 112) x = sx;
    else if (sx >= width - 112) x = sx - (width - 256);
    else if (sx >= g_smw_viewport.extra + 112 && sx < g_smw_viewport.extra + 144)
      x = sx - g_smw_viewport.extra;
    else return 0;
  }
  if (layer == 2 && (y <= 40 || frame_ram[0x1426]) && (x < 0 || x >= 256)) return 0;
  unsigned tile_shift = PPU_bigTiles(p, layer) ? 4 : 3;
  int size = 1 << tile_shift;
  int bpp = layer == 2 ? 2 : 4;
  if (p->mosaic & (1u << layer)) {
    int m = (p->mosaic >> 4) + 1;
    x -= ((x % m) + m) % m;
    y -= y % m;
  }
  int px = (x + p->hScroll[layer]) & 1023;
  int py = (y + p->vScroll[layer]) & 1023;
  unsigned sc = p->bgXsc[layer], tx = (unsigned)px>>tile_shift, ty = (unsigned)py>>tile_shift;
  unsigned addr = (sc & 0xfc)*256 + (tx&31) + (ty&31)*32;
  if ((sc&1) && (tx&32)) addr += 1024;
  if ((sc&2) && (ty&32)) addr += sc&1 ? 2048 : 1024;
  uint16_t tile = l->vram[addr&0x7fff];
  if ((x < 0 || x >= 256) && world_layer(layer) && size == 8) {
    int camx = read16(frame_ram,0x1a+layer*4), camy = read16(frame_ram,0x1c+layer*4);
    int dx = ((p->hScroll[layer]-camx+512)&1023)-512;
    int dy = ((p->vScroll[layer]-camy+512)&1023)-512;
    int wx = camx+x+dx, wy = camy+y+dy;
    if (!SmwRendererMapTile(frame_ram,layer,wx,wy,&tile)) return 0;
    if (layer == 1 && frame_ram[0x1931] == 3) tile |= 0x1000;
    px = wx; py = wy;
  }
  int cx = px&(size-1), cy = py&(size-1);
  if (tile&0x4000) cx = size-1-cx;
  if (tile&0x8000) cy = size-1-cy;
  unsigned number = ((tile&1023) + (cx>>3) + ((cy>>3)<<4))&1023;
  unsigned base = ((p->bgTileAdr>>(layer*4))&15)*4096;
  unsigned pixel = tile_pixel(l->vram,base+number*(bpp*4),cx&7,cy&7,bpp);
  if (!pixel) return 0;
  unsigned priority = tile&0x2000 ? high[layer] : low[layer];
  if (layer == 2 && (tile&0x2000) && (p->bgmode&8)) priority = 15;
  return (priority<<12)|(layer<<8)|(((tile>>10)&7)*(1u<<bpp))|pixel;
}
static const int sprite_sizes[8][2]={{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
static void sprite_piece(const Ppu *p,const RasterLine *l,int y,unsigned slot,
                         unsigned pos,unsigned attr,unsigned hi,int x) {
  const PpuOverlayCapture *capture = &obj_capture[y];
  bool remove = (capture->flags & kPpuOverlayFlag_RemoveFromGame) &&
      y >= capture->y0 && y < capture->y1 &&
      slot >= capture->oamFirst && slot < capture->oamFirst + capture->oamCount;
  int size=sprite_sizes[p->obsel>>5][(hi>>1)&1];
  int row=(y-(pos>>8))&255;
  if (row>=size) return;
  /* $00:9Dxx DrawReserveItem owns OAM 56 (or 0 in the boss variant).
   * Verify its native position and current tile before moving it. */
  static const uint8_t item_tiles[4]={0x24,0x26,0x48,0x0e};
  unsigned item=frame_ram[0xdc2];
  if ((slot==56 || slot==0) && pos==0x0f78 && item>=1 && item<=4 &&
      (attr&255)==item_tiles[item-1]) x += g_smw_viewport.extra-native_x;
  if(attr&0x8000) row=size-1-row;
  unsigned base=(p->obsel&7)*8192;
  if(attr&256) base+=(((p->obsel>>3)&3)+1)*4096;
  unsigned palette=128+((attr>>9)&7)*16;
  unsigned priority=((attr>>12)&3)*4+2;
  unsigned layer=attr&0x800?4:6;
  for(int col=0;col<size;++col) {
    if(remove && x+col >= capture->x0 && x+col < capture->x1) continue;
    int dest=x+col+native_x;
    if(dest<0 || dest>=g_smw_viewport.width) continue;
    int cx=attr&0x4000?size-1-col:col;
    unsigned tile=(((((attr&255)>>4)+row/8)&15)<<4)|(((attr&15)+cx/8)&15);
    unsigned pixel=tile_pixel(l->vram,base+tile*16,cx&7,row&7,4);
    if(pixel) {
      objects[dest]=(priority<<12)|(layer<<8)|palette|pixel;
      if(g_ppu->extraObjectCount)object_order[dest]=(uint64_t)slot<<32;
    }
  }
}
static void sprites(const Ppu *p, const RasterLine *l, int y) {
  memset(objects,0,g_smw_viewport.width*sizeof(*objects));
  if(g_ppu->extraObjectCount) {
    memset(object_colors,0,g_smw_viewport.width*sizeof(*object_colors));
    memset(object_order,0xff,g_smw_viewport.width*sizeof(*object_order));
  }
  for (int slot=127;slot>=0;--slot) {
    unsigned pos=l->oam[slot*2],attr=l->oam[slot*2+1];
    unsigned hi=(l->high[slot/4]>>((slot%4)*2))&3;
    bool captured_native=false;
    for (size_t i=draw_start[slot];i<draw_start[slot+1];++i) {
      const SpriteDraw *draw=&latched_draws.items[i];
      sprite_piece(p,l,y,slot,draw->image.position,draw->image.attr,draw->high,draw->image.x);
      if (draw->image.position==pos && draw->image.attr==attr && draw->high==hi)
        captured_native=true;
    }
    if (captured_native) continue;
    int x=(pos&255)-((hi&1)<<8);
    const OamOwner *owner=&latched[slot];
    if (owner->valid && owner->position==pos && owner->attr==attr) x=owner->x;
    else if (x+sprite_sizes[p->obsel>>5][(hi>>1)&1]<=0 || x>=256) continue;
    sprite_piece(p,l,y,slot,pos,attr,hi,x);
  }
  if(g_ppu->extraObjectCount)PpuComposeExtraObjects(g_ppu->extraObjects,g_ppu->extraObjectCount,
      y,-native_x,(size_t)g_smw_viewport.width,0,objects,object_colors,object_order);
}
static uint32_t compose(const Ppu *p, const RasterLine *l, const uint8_t *brightness,
                        const uint16_t *bg, uint16_t object, int x) {
  uint16_t screens[2]={0x500,0x500};
  for(int sub=0;sub<2;++sub) {
    for(int layer=0;layer<3;++layer) {
      if(!(p->screenEnabled[sub]&(1u<<layer))) continue;
      if((p->screenWindowed[sub]&(1u<<layer)) && window(p,layer,x)) continue;
      if(bg[layer]>screens[sub]) screens[sub]=bg[layer];
    }
    if((p->screenEnabled[sub]&16) &&
       (!(p->screenWindowed[sub]&16) || !window(p,4,x)) &&
       object>screens[sub]) screens[sub]=object;
  }
  return colour(p,l->palette,brightness,screens[0],screens[1],window(p,5,x),x+native_x);
}
void SmwRendererDraw(uint8_t *pixels,size_t pitch,const uint8_t *stock) {
  int width=g_smw_viewport.width;
  bool level=level_scene;
  const char *directory=getenv("SMW_RENDER_DIAGNOSTICS");
  bool control=directory && *directory;
  int shift=background_shift();
  if(control) memcpy(native_control,stock,sizeof(native_control));
  for(int y=0;y<224;++y) {
    uint32_t *dst=(uint32_t *)(pixels+y*pitch);
    const RasterLine *l=&lines[y];
    memcpy(&raster,l->regs,sizeof(l->regs));
    if(width==256 || !level || captured!=224 || (raster.bgmode&7)!=1) {
      memset(dst,0,width*4);
      memcpy(dst+native_x,stock+y*256*4,256*4);
      continue;
    }
    if(raster.inidisp&128) { memset(dst,0,width*4); continue; }
    uint8_t brightness[32];
    for(int i=0;i<32;++i) brightness[i]=((i<<3)|(i>>2))*(raster.inidisp&15)/15;
    sprites(&raster,l,y);
    for(int sx=0;sx<width;++sx) {
      int x=sx-native_x;
      uint16_t bg[3];
      for(int layer=0;layer<3;++layer)
        bg[layer]=background(&raster,l,layer,x+(layer==1?shift:0),y+1);
      dst[sx]=compose(&raster,l,brightness,bg,objects[sx],x);
      if(control && x>=0 && x<256) {
        /* Compare the original projection to the native PPU as well. This
         * checks every native-area pixel without excusing a sky rectangle
         * that could also contain broken foreground, sprites or color math. */
        if(shift) bg[1]=background(&raster,l,1,x,y+1);
        native_control[y*256+x]=shift?compose(&raster,l,brightness,bg,objects[sx],x):dst[sx];
      }
    }
  }
}

/* A far object's wrapped native OAM can alias into the stock image. Only
 * differences inside the exact captured, re-positioned piece are expected. */
static bool alias_footprint(int x,int y) {
  static const int sizes[8][2]={{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
  const RasterLine *l=&lines[y];
  for (size_t i=0;i<latched_draws.count;++i) {
    const SpriteDraw *d=&latched_draws.items[i];
    const OamOwner *o=&d->image;
    unsigned slot=d->slot;
    unsigned hi=(l->high[slot/4]>>((slot%4)*2))&3;
    bool matches=o->position==l->oam[slot*2] && o->attr==l->oam[slot*2+1] && d->high==hi;
    int old=(o->position&255)-((d->high&1)<<8);
    if (matches && old==o->x) continue;
    int size=sizes[l->regs[offsetof(Ppu,obsel)]>>5][(d->high>>1)&1];
    if (((y-(o->position>>8))&255)>=(unsigned)size) continue;
    if ((x>=o->x && x<o->x+size) || (matches && x>=old && x<old+size)) return true;
  }
  for(unsigned slot=0;slot<128;++slot) {
    const OamOwner *o=&latched[slot];
    unsigned pos=l->oam[slot*2],attr=l->oam[slot*2+1];
    if(!o->valid || o->position!=pos || o->attr!=attr) continue;
    unsigned hi=l->high[slot/4]>>((slot%4)*2);
    int old=(pos&255)|((hi&1)<<8);
    if(old>=256)old-=512;
    if(old==o->x)continue;
    int size=sizes[l->regs[offsetof(Ppu,obsel)]>>5][(hi>>1)&1];
    if(((y-(pos>>8))&255)>=(unsigned)size)continue;
    if((x>=old && x<old+size) || (x>=o->x && x<o->x+size))return true;
  }
  return false;
}
/* Opt-in capture and native-center oracle. Does not pause or modify the guest. */
void SmwRendererDiagnostics(const uint8_t *stock, const uint8_t *image, size_t pitch) {
  static unsigned frame;
  static FILE *trace;
  const char *directory=getenv("SMW_RENDER_DIAGNOSTICS");
  ++frame;
  if(!directory || !*directory) return;
  size_t size=(size_t)g_smw_viewport.width*224*4;
  unsigned differing=0, unexplained=0, parallax=0, active=0, far=0;
  /* Wide HUD anchoring is verified separately; its relocation is intentional. */
  for(int y=(g_smw_viewport.width==256?0:40);y<224;++y) for(int x=0;x<256;++x) {
    uint32_t a=((const uint32_t *)(image+y*pitch))[native_x+x];
    uint32_t b=((const uint32_t *)stock)[y*256+x];
    uint32_t c=native_control[y*256+x];
    if((a&0xffffff)!=(b&0xffffff)) ++differing;
    if((a&0xffffff)!=(c&0xffffff)) ++parallax;
    if((c&0xffffff)!=(b&0xffffff) && !alias_footprint(x,y)) ++unexplained;
  }
  int camera=read16(frame_ram,0x1a);
  for(int i=0;i<12;++i) if(frame_ram[0x14c8+i]) {
    ++active;
    int x=(frame_ram[0xe4+i]|(frame_ram[0x14e0+i]<<8))-camera;
    if(x<-64 || x>=320) ++far;
  }
  char path[1024];
  if(!trace) {
    snprintf(path,sizeof(path),"%s/frames.csv",directory);
    trace=fopen(path,"w");
    if(trace) fprintf(trace,"frame,mode,camera,width,active,far,native_differences,unexplained_differences,parallax_differences,simulation_camera,raster_camera,native_offset\n");
  }
  if(trace) {
    fprintf(trace,"%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%d\n",frame,frame_ram[0x100],camera,
            g_smw_viewport.width,active,far,differing,unexplained,parallax,read16(g_ram,0x1a),
            read16(lines[80].regs,offsetof(Ppu,hScroll)),native_x);
    fflush(trace);
  }
  static FILE *sprite_trace;
  if(!sprite_trace) {
    snprintf(path,sizeof(path),"%s/sprites.csv",directory);
    sprite_trace=fopen(path,"w");
    if(sprite_trace) fprintf(sprite_trace,"frame,slot,status,id,record,x,y,load_flag,enter_timer,target,paused\n");
  }
  if(sprite_trace) {
    for(unsigned slot=0;slot<12;++slot) if(frame_ram[0x14c8+slot]) {
      unsigned record=frame_ram[0x161a+slot];
      fprintf(sprite_trace,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",frame,slot,
              frame_ram[0x14c8+slot],frame_ram[0x9e + slot],record,
              frame_ram[0xe4+slot]|(frame_ram[0x14e0+slot]<<8),
              frame_ram[0xd8+slot]|(frame_ram[0x14d4+slot]<<8),
              record<128?frame_ram[0x1938+record]:0,frame_ram[0x1558+slot],
              frame_ram[0x1594+slot],frame_ram[0x13d4]);
    }
    fflush(sprite_trace);
  }
  const char *requested=getenv("SMW_RENDER_CAPTURE_FRAME");
  const char *interval=getenv("SMW_RENDER_CAPTURE_EVERY");
  unsigned every=interval && atoi(interval)>0?(unsigned)atoi(interval):300;
  bool selected=false;
  if(requested) {
    for(const char *cursor=requested;*cursor;) {
      char *end;
      unsigned long value=strtoul(cursor,&end,10);
      if(end==cursor) break;
      if(value==frame) selected=true;
      if(*end!=',') break;
      cursor=end+1;
    }
  } else selected=frame%every==0;
  if(!selected) return;
  snprintf(path,sizeof(path),"%s/frame-%06u.draws.json",directory,frame);
  FILE *draw_file=fopen(path,"w");
  if (draw_file) {
    fputs("[",draw_file);
    for (size_t i=0;i<latched_draws.count;++i) {
      const SpriteDraw *d=&latched_draws.items[i];
      fprintf(draw_file,"%s{\"slot\":%u,\"x\":%d,\"position\":%u,\"attr\":%u,\"high\":%u,\"kind\":%u,\"actor\":%u}",
              i?",":"",d->slot,d->image.x,d->image.position,d->image.attr,d->high,d->kind,d->actor);
    }
    fputs("]\n",draw_file);fclose(draw_file);
  }
  snprintf(path,sizeof(path),"%s/frame-%06u.bmp",directory,frame);
  FILE *f=fopen(path,"wb");
  if(!f) return;
  uint8_t header[54]={0x42,0x4d};
  uint32_t values[]={54+(uint32_t)size,54,40,(uint32_t)g_smw_viewport.width,224,0x200001};
  const unsigned offsets[]={2,10,14,18,22,26};
  for(unsigned i=0;i<6;++i) for(unsigned j=0;j<4;++j) header[offsets[i]+j]=(uint8_t)(values[i]>>(8*j));
  fwrite(header,1,54,f);
  for(int y=223;y>=0;--y) fwrite(image+(size_t)y*pitch,4,g_smw_viewport.width,f);
  fclose(f);
  if(requested) {
    if(strchr(requested,',')) snprintf(path,sizeof(path),"%s/frame-%06u.swr",directory,frame);
    else snprintf(path,sizeof(path),"%s/frame.swr",directory);
    f=fopen(path,"wb");
    if(f) {
      fwrite(frame_ram,1,sizeof(frame_ram),f);
      fwrite(lines,1,sizeof(lines),f);
      fwrite(latched,1,sizeof(latched),f);
      fwrite(stock,1,256*224*4,f);
      fclose(f);
    }
  }
}

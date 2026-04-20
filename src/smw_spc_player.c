#include "smw_spc_player.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include "types.h"
#include "snes/spc.h"
#include "snes/dsp_regs.h"
#include "tracing.h"

#pragma warning (disable: 4267)

typedef struct Channel {
  uint16 pattern_cur_ptr;
  uint8 index;
  uint8 note_ticks_left;
  uint8 volume_fade_ticks;
  uint8 pan_num_ticks;
  uint8 pitch_slide_length;
  uint8 pitch_slide_delay_left;
  uint8 vibrato_hold_count;
  uint8 vib_depth;
  uint8 tremolo_hold_count;
  uint8 tremolo_depth;
  uint8 subroutine_num_loops;
  uint8 instrument_id;
  uint8 note_keyoff_ticks_left;
  uint8 vibrato_change_count;
  uint8 note_length;
  uint8 note_gate_off_fixedpt;
  uint8 instrument_pitch_base;
  uint8 channel_volume_master;
  uint16 channel_volume;
  uint16 volume_fade_addpertick;
  uint8 volume_fade_target;
  uint16 pan_value;
  uint16 pan_add_per_tick;
  uint8 pan_target_value;
  uint8 pan_flag_with_phase_invert;
  uint16 pitch;
  uint16 pitch_add_per_tick;
  uint8 pitch_target;
  uint8 fine_tune;
  uint8 pitch_envelope_num_ticks;
  uint8 pitch_envelope_delay;
  uint8 pitch_envelope_direction;
  uint8 pitch_envelope_slide_value;
  uint8 vibrato_count;
  uint8 vibrato_rate;
  uint8 vibrato_delay_ticks;
  uint8 vibrato_fade_num_ticks;
  uint8 vibrato_fade_add_per_tick;
  uint8 vib_depth_orig;
  uint8 tremolo_count;
  uint8 tremolo_delay_ticks;
  uint8 final_volume;
  uint16 saved_pattern_ptr;
  uint16 pattern_start_ptr;
} Channel;

typedef struct SmwSpcPlayer {
  SpcPlayer base;
  uint8 new_value_from_snes[4];
  
  uint8 last_value_from_snes[4];
  uint8 timer_cycles;
  uint8 counter_sf0c;
  uint8 sfx3_timer;
  uint16 temp_accum;
  uint8 ttt;
  uint8 did_affect_volumepitch_flag;
  uint16 addr0;
  uint16 addr1;
  uint16 sfx0_sound_ptr_cur;
  uint16 sfx3_sound_ptr_cur;
  uint8 chan7_countdown_2;
  uint8 is_chan_on;
  uint8 sfx3_unused;
  uint8 chan_bit_flags;
  uint16 music_ptr_toplevel;
  uint8 block_count;
  uint8 global_transposition;
  uint8 sfx_timer_accum;
  uint8 sfx_tick_counter;
  uint8 chn;
  uint8 key_ON;
  uint8 cur_chan_bit;
  uint8 main_tempo_accum;
  uint16 tempo;
  uint8 tempo_fade_num_ticks;
  uint8 tempo_fade_final;
  uint16 tempo_fade_add;
  uint16 master_volume;
  uint8 master_volume_fade_ticks;
  uint8 master_volume_fade_target;
  uint16 master_volume_fade_add_per_tick;
  uint8 vol_dirty;
  uint8 echo_volume_fade_ticks;
  uint16 echo_volume_left;
  uint16 echo_volume_right;
  uint16 echo_volume_fade_add_left;
  uint16 echo_volume_fade_add_right;
  uint8 echo_volume_fade_target_left;
  uint8 echo_volume_fade_target_right;
  uint8 byte_2FF;
  uint8 sfx0_note_length_left;
  uint8 sfx0_note_length;
  uint8 sfx0_countdown;
  uint8 sfx1_countdown;
  uint8 sfx3_countdown;
  uint8 sfx3_length;
  uint8 smw_player_on_yoshi;
  uint8 smw_tempo_increase;
  uint8 smw_pause_music;
  uint8 echo_channels;
  uint16 pattern_start_ptr;
  Channel channel[8];
  uint8 ram[65536]; // rest of ram
} SmwSpcPlayer;
typedef struct MemMap {
  uint16 off, org_off;
} MemMap;
typedef struct MemMapSized {
  uint16 off, org_off, size;
} MemMapSized;
static const MemMap kChannel_Maps[] = {
{offsetof(Channel, pattern_cur_ptr), 0x8030},
{offsetof(Channel, note_ticks_left), 0x70},
{offsetof(Channel, volume_fade_ticks), 0x80},
{offsetof(Channel, pan_num_ticks), 0x81},
{offsetof(Channel, pitch_slide_length), 0x90},
{offsetof(Channel, pitch_slide_delay_left), 0x91},
{offsetof(Channel, vibrato_hold_count), 0xa0},
{offsetof(Channel, vib_depth), 0xa1},
{offsetof(Channel, tremolo_hold_count), 0xb0},
{offsetof(Channel, tremolo_depth), 0xb1},
{offsetof(Channel, subroutine_num_loops), 0xc0},
{offsetof(Channel, instrument_id), 0xc1},
{offsetof(Channel, note_keyoff_ticks_left), 0x100},
{offsetof(Channel, vibrato_change_count), 0x110},
{offsetof(Channel, note_length), 0x200},
{offsetof(Channel, note_gate_off_fixedpt), 0x201},
{offsetof(Channel, instrument_pitch_base), 0x210},
{offsetof(Channel, channel_volume_master), 0x211},
{offsetof(Channel, channel_volume), 0x8240},
{offsetof(Channel, volume_fade_addpertick), 0x8250},
{offsetof(Channel, volume_fade_target), 0x260},
{offsetof(Channel, pan_value), 0x8280},
{offsetof(Channel, pan_add_per_tick), 0x8290},
{offsetof(Channel, pan_target_value), 0x2a0},
{offsetof(Channel, pan_flag_with_phase_invert), 0x2a1},
{offsetof(Channel, pitch), 0x82b0},
{offsetof(Channel, pitch_add_per_tick), 0x82c0},
{offsetof(Channel, pitch_target), 0x2d0},
{offsetof(Channel, fine_tune), 0x2d1},
{offsetof(Channel, pitch_envelope_num_ticks), 0x300},
{offsetof(Channel, pitch_envelope_delay), 0x301},
{offsetof(Channel, pitch_envelope_direction), 0x320},
{offsetof(Channel, pitch_envelope_slide_value), 0x321},
{offsetof(Channel, vibrato_count), 0x330},
{offsetof(Channel, vibrato_rate), 0x331},
{offsetof(Channel, vibrato_delay_ticks), 0x340},
{offsetof(Channel, vibrato_fade_num_ticks), 0x341},
{offsetof(Channel, vibrato_fade_add_per_tick), 0x350},
{offsetof(Channel, vib_depth_orig), 0x351},
{offsetof(Channel, tremolo_count), 0x360},
{offsetof(Channel, tremolo_delay_ticks), 0x370},
{offsetof(Channel, final_volume), 0x371},
{offsetof(Channel, saved_pattern_ptr), 0x83e0},
{offsetof(Channel, pattern_start_ptr), 0x83f0},
};
static const MemMapSized kSpcPlayer_Maps[] = {
{offsetof(SmwSpcPlayer, new_value_from_snes), 0x0, 4},
{offsetof(SmwSpcPlayer, base.port_to_snes), 0x4, 4},
{offsetof(SmwSpcPlayer, last_value_from_snes), 0x8, 4},
{offsetof(SmwSpcPlayer, counter_sf0c), 0xc, 1},
{offsetof(SmwSpcPlayer, sfx3_timer), 0xd, 1},
{offsetof(SmwSpcPlayer, temp_accum), 0x10, 2},
{offsetof(SmwSpcPlayer, ttt), 0x12, 1},
{offsetof(SmwSpcPlayer, did_affect_volumepitch_flag), 0x13, 1},
{offsetof(SmwSpcPlayer, addr0), 0x14, 2},
{offsetof(SmwSpcPlayer, addr1), 0x16, 2},
{offsetof(SmwSpcPlayer, sfx0_sound_ptr_cur), 0x18, 2},
{offsetof(SmwSpcPlayer, sfx3_sound_ptr_cur), 0x1a, 2},
{offsetof(SmwSpcPlayer, chan7_countdown_2), 0x1c, 1},
{offsetof(SmwSpcPlayer, is_chan_on), 0x1d, 1},
{offsetof(SmwSpcPlayer, sfx3_unused), 0x2e, 1},
{offsetof(SmwSpcPlayer, chan_bit_flags), 0x2f, 1},
{offsetof(SmwSpcPlayer, music_ptr_toplevel), 0x40, 2},
{offsetof(SmwSpcPlayer, block_count), 0x42, 1},
{offsetof(SmwSpcPlayer, global_transposition), 0x43, 1},
{offsetof(SmwSpcPlayer, sfx_timer_accum), 0x44, 1},
{offsetof(SmwSpcPlayer, sfx_tick_counter), 0x45, 1},
{offsetof(SmwSpcPlayer, chn), 0x46, 1},
{offsetof(SmwSpcPlayer, key_ON), 0x47, 1},
{offsetof(SmwSpcPlayer, cur_chan_bit), 0x48, 1},
{offsetof(SmwSpcPlayer, main_tempo_accum), 0x49, 1},
{offsetof(SmwSpcPlayer, timer_cycles), 0x4a, 1},
{offsetof(SmwSpcPlayer, tempo), 0x50, 2},
{offsetof(SmwSpcPlayer, tempo_fade_num_ticks), 0x52, 1},
{offsetof(SmwSpcPlayer, tempo_fade_final), 0x53, 1},
{offsetof(SmwSpcPlayer, tempo_fade_add), 0x54, 2},
{offsetof(SmwSpcPlayer, master_volume), 0x56, 2},
{offsetof(SmwSpcPlayer, master_volume_fade_ticks), 0x58, 1},
{offsetof(SmwSpcPlayer, master_volume_fade_target), 0x59, 1},
{offsetof(SmwSpcPlayer, master_volume_fade_add_per_tick), 0x5a, 2},
{offsetof(SmwSpcPlayer, vol_dirty), 0x5c, 1},
{offsetof(SmwSpcPlayer, echo_volume_fade_ticks), 0x60, 1},
{offsetof(SmwSpcPlayer, echo_volume_left), 0x61, 2},
{offsetof(SmwSpcPlayer, echo_volume_right), 0x63, 2},
{offsetof(SmwSpcPlayer, echo_volume_fade_add_left), 0x65, 2},
{offsetof(SmwSpcPlayer, echo_volume_fade_add_right), 0x67, 2},
{offsetof(SmwSpcPlayer, echo_volume_fade_target_left), 0x69, 1},
{offsetof(SmwSpcPlayer, echo_volume_fade_target_right), 0x6a, 1},
{offsetof(SmwSpcPlayer, byte_2FF), 0x2ff, 1},
{offsetof(SmwSpcPlayer, sfx0_note_length_left), 0x380, 1},
{offsetof(SmwSpcPlayer, sfx0_note_length), 0x381, 1},
{offsetof(SmwSpcPlayer, sfx0_countdown), 0x382, 1},
{offsetof(SmwSpcPlayer, sfx1_countdown), 0x383, 1},
{offsetof(SmwSpcPlayer, sfx3_countdown), 0x384, 1},
{offsetof(SmwSpcPlayer, sfx3_length), 0x385, 1},
{offsetof(SmwSpcPlayer, smw_player_on_yoshi), 0x386, 1},
{offsetof(SmwSpcPlayer, smw_tempo_increase), 0x387, 1},
{offsetof(SmwSpcPlayer, smw_pause_music), 0x388, 1},
{offsetof(SmwSpcPlayer, echo_channels), 0x389, 1},
{offsetof(SmwSpcPlayer, pattern_start_ptr), 0x3f0, 2},
};

static void Dsp_Write(SmwSpcPlayer *p, uint8_t reg, uint8 value) {
  if (p->base.dsp)
    dsp_write(p->base.dsp, reg, value);
}

static void SmwSpcPlayer_CopyVariablesFromRam(SmwSpcPlayer *p) {
  Channel *c = p->channel;
  for (int i = 0; i < 8; i++, c++) {
    for (const MemMap *m = &kChannel_Maps[0]; m != &kChannel_Maps[countof(kChannel_Maps)]; m++)
      memcpy((uint8 *)c + m->off, &p->ram[(m->org_off & 0x7fff) + i * 2], m->org_off & 0x8000 ? 2 : 1);
  }
  for (const MemMapSized *m = &kSpcPlayer_Maps[0]; m != &kSpcPlayer_Maps[countof(kSpcPlayer_Maps)]; m++)
    memcpy((uint8 *)p + m->off, &p->ram[m->org_off], m->size);

  for (int i = 0; i < 8; i++)
    p->channel[i].index = i;
}

static const uint8 kDefDspRegs[12] = { MVOLL,MVOLR,EVOLL,EVOLR,FLG,EFB,PMON,NON,EON,DIR,ESA,EDL };
static const uint8 kDefDspValues[12] = { 0x7F, 0x7F,  0,  0, 0x2F, 0x60,  0,  0,  0, 0x80, 0x60, 2 };

static void Spc_Reset(SmwSpcPlayer *p) {
  memset(p->ram, 0, 0x500);

  SmwSpcPlayer_CopyVariablesFromRam(p);

  memset(p->base.input_ports, 0, sizeof(p->base.input_ports));

  for (int i = 11; i >= 0; i--)
    Dsp_Write(p, kDefDspRegs[i], kDefDspValues[i]);
  HIBYTE(p->tempo) = 0x36;
}


static void SmwSpcPlayer_Initialize(SpcPlayer *p_in) {
  SmwSpcPlayer *p = (SmwSpcPlayer *)p_in;
  dsp_reset(p->base.dsp);
  Spc_Reset(p);
}

static void SmwSpcPlayer_Upload(SpcPlayer *p_in, const uint8_t *data) {
  const uint8 *data_org = data;
  SmwSpcPlayer *p = (SmwSpcPlayer *)p_in;
  Dsp_Write(p, FLG, 0x60);
  Dsp_Write(p, KOF, 0xff);
  for (;;) {
    int numbytes = *(uint16 *)(data);
    if (numbytes == 0) {
      break;
    }
    int target = *(uint16 *)(data + 2);
    data += 4;
    do {
      p->ram[target++ & 0xffff] = *data++;
    } while (--numbytes);
  }
  p->base.port_to_snes[0] = p->base.port_to_snes[1] = p->base.port_to_snes[2] = p->base.port_to_snes[3] = 0;
  p->is_chan_on = 0;
  p->smw_tempo_increase = 0;
  p->smw_pause_music = 0;
  p->smw_player_on_yoshi = 0;
  p->echo_channels = 0;
  memset(p->base.input_ports, 0, sizeof(p->base.input_ports));
  memset(p->last_value_from_snes, 0, sizeof(p->last_value_from_snes));
  memset(p->new_value_from_snes, 0, sizeof(p->new_value_from_snes));

  p->music_ptr_toplevel = 0;
  for (int i = 0; i < 8; i++)
    p->channel[i].pattern_cur_ptr = 0;

  Dsp_Write(p, FLG, 0x20);  
}

SpcPlayer *SmwSpcPlayer_Create(void) {
  SmwSpcPlayer *p = (SmwSpcPlayer *)malloc(sizeof(SmwSpcPlayer));
  memset(p, 0, sizeof(SmwSpcPlayer));
  p->base.dsp = dsp_init(p->ram);
  p->base.ram = p->ram;
  p->base.initialize = &SmwSpcPlayer_Initialize;
  p->base.upload = &SmwSpcPlayer_Upload;
  return &p->base;
}


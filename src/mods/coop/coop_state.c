#include "coop_session.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER_SIZE = 136, PLAYER_SIZE = 96, EVENT_SIZE = 28, ACTION_SIZE = 16 };
#define COOP_STATE_MAGIC 0x504f4f43u /* COOP */
#define COOP_STATE_VERSION 1u

typedef struct Stream {
    uint8_t *p;
    size_t at, size;
    bool error, loading;
} Stream;

static uint32_t word(Stream *s, uint32_t value) {
    if (s->at > s->size || s->size - s->at < 4) { s->error = true; return 0; }
    if (s->loading)
        value = (uint32_t)s->p[s->at] | ((uint32_t)s->p[s->at+1] << 8) |
            ((uint32_t)s->p[s->at+2] << 16) | ((uint32_t)s->p[s->at+3] << 24);
    else for (unsigned i = 0; i < 4; ++i) s->p[s->at+i] = (uint8_t)(value >> (8*i));
    s->at += 4;
    return value;
}

static uint64_t wide(Stream *s, uint64_t value) {
    uint32_t lo = word(s, (uint32_t)value), hi = word(s, (uint32_t)(value >> 32));
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static bool boolean(Stream *s, bool value) {
    uint32_t v = word(s, value ? 1u : 0u);
    if (v > 1) s->error = true;
    return v != 0;
}

static int32_t signed_word(Stream *s, int32_t value) {
    uint32_t v = word(s, (uint32_t)value);
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v);
}

static uint32_t checksum(const uint8_t *data, size_t n) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static size_t encoded_size(size_t players, size_t events, size_t actions) {
    size_t n = HEADER_SIZE + 4;
    if (players > (SIZE_MAX - n) / PLAYER_SIZE) return 0;
    n += players * PLAYER_SIZE;
    if (events > (SIZE_MAX - n) / EVENT_SIZE) return 0;
    n += events * EVENT_SIZE;
    if (actions > (SIZE_MAX - n) / ACTION_SIZE) return 0;
    return n + actions * ACTION_SIZE;
}

size_t coop_session_save_size(const CoopSession *s) {
    if (!s || !s->players || s->player_count >= COOP_NO_PLAYER ||
        s->event_count > UINT32_MAX || s->action_count > UINT32_MAX) return 0;
    return encoded_size(s->player_count, s->event_count, s->action_count);
}

static bool stream_session(Stream *w, CoopSession *s) {
#define W(field) s->field = word(w, s->field)
#define I(field) s->field = signed_word(w, s->field)
#define B(field) s->field = boolean(w, s->field)
    W(primary); W(ticks_per_second);
    s->frame = wide(w, s->frame);
    W(lives); W(coins); W(score); W(dragon_coins); W(bonus_stars);
    W(checkpoint); W(level_ticks); W(outcome);
    B(advancing); B(input_blocked); B(failed); B(resolved);
    I(camera.x); I(camera.y); I(camera.width); I(camera.height);
    I(camera.room_width); I(camera.room_height);
    I(camera.previous_center_x); I(camera.previous_center_y);
    I(camera.travel_x); I(camera.travel_y); W(camera.leader);
    B(camera.initialized); B(camera.autoscroll);
#undef W
#undef I
#undef B
    for (size_t n = 0; n < s->player_count; ++n) {
        CoopPlayer *p = &s->players[n];
#define W(field) p->field = word(w, p->field)
#define I(field) p->field = signed_word(w, p->field)
#define B(field) p->field = boolean(w, p->field)
        W(id); W(character); W(life); W(power); W(reserve);
        I(x); I(y); I(half_width); I(height);
        W(star_ticks); W(protection_ticks); W(recovery_ticks); W(separation_ticks);
        W(mount); W(held_object); W(held_input); W(pressed_input); W(action_latch);
        B(connected); B(grounded); B(swimming); B(supported_flight);
        B(behind_fence); B(checkpoint_upgrade);
#undef W
#undef I
#undef B
    }
    for (size_t n = 0; n < s->event_count; ++n) {
        CoopEvent *e = &s->events[n];
        e->kind = word(w, e->kind); e->player = word(w, e->player);
        e->entity = word(w, e->entity); e->value = word(w, e->value);
        e->flags = word(w, e->flags); e->distance_squared = wide(w, e->distance_squared);
    }
    for (size_t n = 0; n < s->action_count; ++n) {
        CoopAction *a = &s->actions[n];
        a->kind = word(w, a->kind); a->player = word(w, a->player);
        a->entity = word(w, a->entity); a->value = word(w, a->value);
    }
    return !w->error;
}

static bool valid(const CoopSession *s) {
    if (!s->ticks_per_second || s->ticks_per_second > 1000 ||
        s->outcome > COOP_CLEAR || !coop_player_const(s, s->primary) ||
        (s->camera.leader != COOP_NO_PLAYER && !coop_player_const(s, s->camera.leader))) return false;
    if (s->camera.initialized && (s->camera.width < 32 || s->camera.height < 32 ||
        s->camera.room_width < 1 || s->camera.room_height < 1)) return false;
    if (s->camera.travel_x < -1 || s->camera.travel_x > 1 ||
        s->camera.travel_y < -1 || s->camera.travel_y > 1) return false;
    for (size_t i = 0; i < s->player_count; ++i) {
        const CoopPlayer *p = &s->players[i];
        if (p->id == COOP_NO_PLAYER || p->life > COOP_CATCHUP_BUBBLE ||
            p->power > COOP_FIRE || p->reserve > 4 || p->half_width < 1 ||
            p->half_width > 256 || p->height < 1 || p->height > 512) return false;
        for (size_t j = i + 1; j < s->player_count; ++j) {
            const CoopPlayer *q = &s->players[j];
            if (p->id == q->id || (p->mount != COOP_NO_ENTITY && p->mount == q->mount) ||
                (p->held_object != COOP_NO_ENTITY && p->held_object == q->held_object)) return false;
        }
    }
    for (size_t i = 0; i < s->event_count; ++i) {
        const CoopEvent *e = &s->events[i];
        if (e->kind > COOP_EVENT_EXIT || (e->player != COOP_NO_PLAYER && !coop_player_const(s,e->player))) return false;
    }
    for (size_t i = 0; i < s->action_count; ++i) {
        const CoopAction *a = &s->actions[i];
        if (a->kind > COOP_ACTION_GAME_OVER || (a->player != COOP_NO_PLAYER && !coop_player_const(s,a->player))) return false;
    }
    return true;
}

bool coop_session_save(const CoopSession *s, void *data, size_t capacity) {
    size_t size = coop_session_save_size(s);
    if (!size || !data || capacity < size || !valid(s)) return false;
    Stream w = {(uint8_t *)data, 0, size - 4, false, false};
    word(&w, COOP_STATE_MAGIC); word(&w, COOP_STATE_VERSION);
    word(&w, (uint32_t)s->player_count); word(&w, (uint32_t)s->event_count);
    word(&w, (uint32_t)s->action_count);
    /* The streaming code writes back identical values when saving; use an
     * owned copy so callers may keep the authoritative session const. */
    CoopSession copy = *s;
    copy.players = malloc(s->player_count * sizeof(*s->players));
    copy.events = s->event_count ? malloc(s->event_count * sizeof(*s->events)) : NULL;
    copy.actions = s->action_count ? malloc(s->action_count * sizeof(*s->actions)) : NULL;
    if (!copy.players || (s->event_count && !copy.events) || (s->action_count && !copy.actions)) {
        free(copy.players); free(copy.events); free(copy.actions); return false;
    }
    memcpy(copy.players, s->players, s->player_count * sizeof(*s->players));
    if (s->event_count) memcpy(copy.events,s->events,s->event_count*sizeof(*s->events));
    if (s->action_count) memcpy(copy.actions,s->actions,s->action_count*sizeof(*s->actions));
    bool ok = stream_session(&w, &copy) && w.at == size - 4;
    free(copy.players); free(copy.events); free(copy.actions);
    if (!ok) return false;
    w.size = size;
    word(&w, checksum(data, size - 4));
    return !w.error;
}

bool coop_session_load(CoopSession *s, const void *data, size_t size) {
    if (!s || !data || size < HEADER_SIZE + PLAYER_SIZE * 2 + 4) return false;
    Stream r = {(uint8_t *)data, 0, size, false, true};
    if (word(&r,0) != COOP_STATE_MAGIC || word(&r,0) != COOP_STATE_VERSION) return false;
    uint32_t count = word(&r,0), events = word(&r,0), actions = word(&r,0);
    if (encoded_size(count,events,actions) != size) return false;
    Stream tail = r; tail.at = size - 4;
    if (word(&tail,0) != checksum(data,size-4)) return false;
    CoopSession candidate = {0};
    if (!coop_session_init(&candidate,count,60)) return false;
    if ((uint64_t)events * sizeof(CoopEvent) > SIZE_MAX ||
        (uint64_t)actions * sizeof(CoopAction) > SIZE_MAX) {
        coop_session_destroy(&candidate); return false;
    }
    candidate.events = events ? calloc(events,sizeof(CoopEvent)) : NULL;
    candidate.actions = actions ? calloc(actions,sizeof(CoopAction)) : NULL;
    if ((events && !candidate.events) || (actions && !candidate.actions)) {
        coop_session_destroy(&candidate); return false;
    }
    candidate.event_count = candidate.event_capacity = events;
    candidate.action_count = candidate.action_capacity = actions;
    r.size -= 4;
    if (!stream_session(&r,&candidate) || r.at != r.size || !valid(&candidate)) {
        coop_session_destroy(&candidate); return false;
    }
    coop_session_destroy(s);
    *s = candidate;
    return true;
}

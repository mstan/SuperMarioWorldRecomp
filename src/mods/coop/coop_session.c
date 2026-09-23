#include "coop_session.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool grow(void **data, size_t *capacity, size_t count, size_t stride) {
    if (count <= *capacity) return true;
    if (count > SIZE_MAX / stride) return false;
    size_t next = *capacity ? *capacity : 16;
    while (next < count) {
        if (next > SIZE_MAX / 2) { next = count; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / stride) next = count;
    void *p = realloc(*data, next * stride);
    if (!p) return false;
    *data = p; *capacity = next;
    return true;
}

bool coop_session_init(CoopSession *s, size_t count, uint32_t hz) {
    if (!s || count < 2 || count >= COOP_NO_PLAYER ||
        count > SIZE_MAX / sizeof(CoopPlayer) || hz == 0 || hz > 1000)
        return false;
    CoopSession candidate = {0};
    candidate.players = calloc(count, sizeof(*candidate.players));
    if (!candidate.players) return false;
    candidate.player_count = count;
    candidate.ticks_per_second = hz;
    candidate.lives = 5;
    candidate.camera.leader = COOP_NO_PLAYER;
    for (size_t i = 0; i < count; ++i) {
        CoopPlayer *p = &candidate.players[i];
        p->id = (CoopPlayerId)i;
        p->character = (uint32_t)i;
        p->mount = p->held_object = COOP_NO_ENTITY;
        p->half_width = 8; p->height = 16;
        p->connected = true;
    }
    *s = candidate;
    return true;
}

void coop_session_destroy(CoopSession *s) {
    if (!s) return;
    free(s->players); free(s->events); free(s->actions);
    memset(s, 0, sizeof(*s));
}

CoopPlayer *coop_player(CoopSession *s, CoopPlayerId id) {
    if (!s) return NULL;
    for (size_t i = 0; i < s->player_count; ++i)
        if (s->players[i].id == id) return &s->players[i];
    return NULL;
}

const CoopPlayer *coop_player_const(const CoopSession *s, CoopPlayerId id) {
    if (!s) return NULL;
    for (size_t i = 0; i < s->player_count; ++i)
        if (s->players[i].id == id) return &s->players[i];
    return NULL;
}

size_t coop_active_count(const CoopSession *s) {
    size_t n = 0;
    for (size_t i = 0; i < s->player_count; ++i)
        n += s->players[i].life == COOP_PLAYING;
    return n;
}

static bool emit(CoopSession *s, CoopActionKind kind, CoopPlayerId p,
                 CoopEntityId entity, uint32_t value) {
    if (s->action_count == SIZE_MAX || !grow((void **)&s->actions,
        &s->action_capacity, s->action_count + 1, sizeof(*s->actions))) {
        s->failed = true;
        return false;
    }
    s->actions[s->action_count++] = (CoopAction){kind, p, entity, value};
    return true;
}

bool coop_session_begin_frame(CoopSession *s, bool advances) {
    if (!s || !s->players || s->failed) return false;
    s->event_count = s->action_count = 0;
    s->resolved = false;
    s->input_blocked = false;
    for (size_t i = 0; i < s->player_count; ++i)
        s->input_blocked |= !s->players[i].connected;
    s->advancing = advances && !s->input_blocked && s->outcome == COOP_CONTINUE;
    if (!s->advancing) return true;
    ++s->frame;
    for (size_t i = 0; i < s->player_count; ++i) {
        CoopPlayer *p = &s->players[i];
        if (p->star_ticks) --p->star_ticks;
        if (p->protection_ticks) --p->protection_ticks;
        if (p->recovery_ticks) --p->recovery_ticks;
    }
    return true;
}

bool coop_session_event(CoopSession *s, CoopEvent event) {
    if (!s || s->input_blocked || s->failed || s->resolved ||
        s->outcome != COOP_CONTINUE ||
        (!s->advancing && event.kind != COOP_EVENT_EXIT) ||
        (event.player != COOP_NO_PLAYER && !coop_player(s, event.player)) ||
        event.kind > COOP_EVENT_EXIT || event.kind < COOP_EVENT_PICKUP)
        return false;
    if (s->event_count == SIZE_MAX || !grow((void **)&s->events,
        &s->event_capacity, s->event_count + 1, sizeof(*s->events))) {
        s->failed = true;
        return false;
    }
    s->events[s->event_count++] = event;
    return true;
}

static uint64_t distance2(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    int64_t dx = (int64_t)ax - bx, dy = (int64_t)ay - by;
    uint64_t ux = dx < 0 ? (uint64_t)-dx : (uint64_t)dx;
    uint64_t uy = dy < 0 ? (uint64_t)-dy : (uint64_t)dy;
    uint64_t x = ux * ux, y = uy * uy;
    return UINT64_MAX - x < y ? UINT64_MAX : x + y;
}

bool coop_player_precedes(const CoopSession *s, CoopPlayerId a, CoopPlayerId b) {
    if (a == b) return false;
    if (a == s->primary) return true;
    if (b == s->primary) return false;
    return a < b;
}

CoopPlayerId coop_nearest_player(const CoopSession *s, int32_t x, int32_t y,
                                bool match_fence, bool behind_fence) {
    CoopPlayerId winner = COOP_NO_PLAYER;
    uint64_t best = UINT64_MAX;
    for (size_t i = 0; i < s->player_count; ++i) {
        const CoopPlayer *p = &s->players[i];
        if (p->life != COOP_PLAYING || (match_fence && p->behind_fence != behind_fence)) continue;
        uint64_t d = distance2(x, y, p->x, p->y);
        if (winner == COOP_NO_PLAYER || d < best ||
            (d == best && coop_player_precedes(s, p->id, winner))) {
            best = d; winner = p->id;
        }
    }
    return winner;
}

static int compare_event(const void *va, const void *vb) {
    const CoopEvent *a = va, *b = vb;
#define CMP(field) do { if (a->field != b->field) return a->field < b->field ? -1 : 1; } while (0)
    CMP(kind); CMP(entity); CMP(player); CMP(value); CMP(flags); CMP(distance_squared);
#undef CMP
    return 0;
}

static void die(CoopSession *s, CoopPlayer *p) {
    if (p->life != COOP_PLAYING) return;
    if (p->held_object != COOP_NO_ENTITY)
        emit(s, COOP_ACTION_DROP_OBJECT, p->id, p->held_object, 0);
    if (p->mount != COOP_NO_ENTITY)
        emit(s, COOP_ACTION_DETACH_MOUNT, p->id, p->mount, 0);
    p->held_object = p->mount = COOP_NO_ENTITY;
    p->life = COOP_DYING;
    p->power = COOP_SMALL;
    p->star_ticks = p->protection_ticks = p->separation_ticks = 0;
    p->checkpoint_upgrade = false;
    emit(s, COOP_ACTION_DEATH, p->id, COOP_NO_ENTITY, 0);
}

bool coop_session_resolve(CoopSession *s) {
    if (!s || s->failed) return false;
    if (s->outcome != COOP_CONTINUE || s->resolved) return true;
    if (!s->advancing && !s->event_count) return true;
    s->resolved = true;
    if (s->event_count > 1)
        qsort(s->events, s->event_count, sizeof(*s->events), compare_event);
    const CoopEvent *exit = NULL;
    bool timeout = false, switch_done = false, checkpoint_done = false;
    uint32_t goal_stars = 0;
    /* First collect terminal events. A valid clear wins over all same-frame
     * deaths and timeout, but individual death consequences still apply. */
    for (size_t i = 0; i < s->event_count; ++i) {
        const CoopEvent *e = &s->events[i];
        if (e->kind == COOP_EVENT_TIMEOUT) timeout = true;
        if (e->kind != COOP_EVENT_EXIT) continue;
        if (e->value > goal_stars) goal_stars = e->value;
        if (!exit || ((e->flags & COOP_EVENT_SECRET) > (exit->flags & COOP_EVENT_SECRET)) ||
            ((e->flags & COOP_EVENT_SECRET) == (exit->flags & COOP_EVENT_SECRET) &&
             coop_player_precedes(s, e->player, exit->player))) exit = e;
    }
    /* Lethal hits precede nonlethal reserve drops and catch-up. */
    for (size_t i = 0; i < s->event_count; ++i) {
        const CoopEvent *e = &s->events[i];
        CoopPlayer *p = coop_player(s, e->player);
        if (p && (e->kind == COOP_EVENT_DEATH ||
            (e->kind == COOP_EVENT_DAMAGE && (e->flags & COOP_EVENT_LETHAL)))) die(s, p);
    }
    for (size_t i = 0; i < s->event_count; ++i) {
        const CoopEvent *e = &s->events[i];
        CoopPlayer *p = coop_player(s, e->player);
        switch (e->kind) {
        case COOP_EVENT_PICKUP:
        case COOP_EVENT_ENEMY_CONTACT: {
            size_t end = i + 1;
            while (end < s->event_count && s->events[end].kind == e->kind &&
                   s->events[end].entity == e->entity) ++end;
            const CoopEvent *winner = NULL;
            CoopPlayerId bounced = COOP_NO_PLAYER;
            for (size_t j = i; j < end; ++j) {
                const CoopEvent *v = &s->events[j];
                const CoopPlayer *actor = coop_player_const(s, v->player);
                if (!actor || actor->life != COOP_PLAYING) continue;
                if (!winner || v->distance_squared < winner->distance_squared ||
                    (v->distance_squared == winner->distance_squared &&
                     coop_player_precedes(s, v->player, winner->player))) winner = v;
                if (e->kind == COOP_EVENT_ENEMY_CONTACT && (v->flags & COOP_EVENT_STOMP) &&
                    bounced != v->player) {
                    emit(s, COOP_ACTION_STOMP_BOUNCE, v->player, v->entity, 0);
                    bounced = v->player;
                }
            }
            if (winner) emit(s, e->kind == COOP_EVENT_PICKUP ? COOP_ACTION_PICKUP :
                COOP_ACTION_ENEMY_DAMAGE, winner->player, winner->entity, winner->value);
            i = end - 1;
            break;
        }
        case COOP_EVENT_ON_OFF:
            if (!switch_done) emit(s, COOP_ACTION_ON_OFF, e->player, e->entity, 0);
            switch_done = true;
            break;
        case COOP_EVENT_CHECKPOINT:
            if (checkpoint_done) break;
            checkpoint_done = true; s->checkpoint = e->value;
            for (size_t j = 0; j < s->player_count; ++j) {
                CoopPlayer *a = &s->players[j];
                if (a->power == COOP_SMALL) a->power = COOP_BIG;
                if (a->life != COOP_PLAYING) a->checkpoint_upgrade = true;
            }
            emit(s, COOP_ACTION_CHECKPOINT, e->player, e->entity, e->value);
            break;
        case COOP_EVENT_DAMAGE:
            if (!p || p->life != COOP_PLAYING || p->star_ticks || p->protection_ticks) break;
            if (e->flags & COOP_EVENT_LETHAL) break;
            if (p->reserve) {
                emit(s, COOP_ACTION_DROP_RESERVE, p->id, COOP_NO_ENTITY, p->reserve);
                p->reserve = 0;
            }
            p->power = COOP_SMALL;
            p->protection_ticks = s->ticks_per_second * 2;
            emit(s, COOP_ACTION_DAMAGE, p->id, COOP_NO_ENTITY, 0);
            break;
        case COOP_EVENT_DEATH_FINISHED:
            if (p && p->life == COOP_DYING) {
                p->life = COOP_DEATH_BUBBLE;
                p->recovery_ticks = s->ticks_per_second * 3;
            }
            break;
        default: break;
        }
    }
    if (exit) {
        s->outcome = COOP_CLEAR;
        emit(s, COOP_ACTION_EXIT, exit->player, exit->entity,
             (exit->flags & COOP_EVENT_SECRET) | (goal_stars << 8));
    } else if (timeout || !coop_active_count(s)) {
        if (s->lives) --s->lives;
        s->outcome = s->lives ? COOP_RETRY : COOP_GAME_OVER;
        emit(s, s->lives ? COOP_ACTION_RETRY : COOP_ACTION_GAME_OVER,
             COOP_NO_PLAYER, COOP_NO_ENTITY, s->checkpoint);
    }
    return !s->failed;
}

void coop_player_input(CoopPlayer *p, uint32_t held, uint32_t action_mask) {
    p->action_latch &= held;
    uint32_t effective = held & ~(p->action_latch & action_mask);
    p->pressed_input = effective & ~p->held_input;
    p->held_input = effective;
    if (p->life != COOP_PLAYING) {
        p->action_latch |= held & action_mask;
        /* Session controls outside action_mask (for example Start) remain
         * available while an actor is dying or waiting to return. */
        p->pressed_input &= ~action_mask;
    }
}

static int32_t clamp64(int64_t n, int32_t lo, int32_t hi) {
    return n < lo ? lo : n > hi ? hi : (int32_t)n;
}

bool coop_camera_frame(CoopSession *s, int32_t w, int32_t h,
                        int32_t rw, int32_t rh, bool autoscroll) {
    if (!s || s->failed || w < 32 || h < 32 || rw < 1 || rh < 1) return false;
    CoopCamera *c = &s->camera;
    bool resized = c->width != w || c->height != h;
    c->width = w; c->height = h; c->room_width = rw; c->room_height = rh;
    c->autoscroll = autoscroll;
    if (resized) for (size_t i = 0; i < s->player_count; ++i) s->players[i].separation_ticks = 0;
    int64_t left = INT32_MAX, right = INT32_MIN, top = INT32_MAX, bottom = INT32_MIN;
    size_t active = 0;
    for (size_t i = 0; i < s->player_count; ++i) {
        const CoopPlayer *p = &s->players[i];
        if (p->life != COOP_PLAYING) continue;
        ++active;
        int64_t l = (int64_t)p->x - p->half_width, r = (int64_t)p->x + p->half_width;
        int64_t t = (int64_t)p->y - p->height / 2, b = (int64_t)p->y + p->height / 2;
        if (l < left) left = l;
        if (r > right) right = r;
        if (t < top) top = t;
        if (b > bottom) bottom = b;
    }
    if (!active) return true;
    int32_t cx = (int32_t)((left + right) / 2), cy = (int32_t)((top + bottom) / 2);
    int64_t dx = (int64_t)cx - c->previous_center_x, dy = (int64_t)cy - c->previous_center_y;
    bool fits = right - left <= w - 32 && bottom - top <= h - 32;
    if (c->initialized && fits) {
        if (dx) c->travel_x = dx > 0 ? 1 : -1;
        if (dy) c->travel_y = dy > 0 ? 1 : -1;
    }
    c->previous_center_x = cx; c->previous_center_y = cy;
    c->initialized = true;
    if (!fits) {
        const CoopPlayer *leader = NULL;
        int64_t best = INT64_MIN;
        bool horizontal = right - left - (w - 32) >= bottom - top - (h - 32);
        int32_t travel = horizontal ? c->travel_x : c->travel_y;
        for (size_t i = 0; i < s->player_count; ++i) {
            const CoopPlayer *p = &s->players[i];
            if (p->life != COOP_PLAYING) continue;
            int64_t progress = (int64_t)(horizontal ? p->x : p->y) * travel;
            if (!leader || progress > best || (progress == best && coop_player_precedes(s,p->id,leader->id))) {
                best = progress; leader = p;
            }
        }
        c->leader = leader->id; cx = leader->x; cy = leader->y;
    } else c->leader = COOP_NO_PLAYER;
    if (!autoscroll) {
        c->x = clamp64((int64_t)cx - w / 2, 0, rw > w ? rw - w : 0);
        c->y = clamp64((int64_t)cy - h / 2, 0, rh > h ? rh - h : 0);
    }
    return true;
}

bool coop_camera_check_separation(CoopSession *s,bool resized) {
    if(!s || s->failed)return false;
    CoopCamera *c=&s->camera;
    size_t active=coop_active_count(s);
    if (!s->advancing || s->outcome != COOP_CONTINUE || c->autoscroll) return true;
    for (size_t i = 0; i < s->player_count; ++i) {
        CoopPlayer *p = &s->players[i];
        if (p->life != COOP_PLAYING) continue;
        bool outside = (int64_t)p->x - p->half_width < c->x ||
            (int64_t)p->x + p->half_width > (int64_t)c->x + c->width ||
            (int64_t)p->y - p->height / 2 < c->y ||
            (int64_t)p->y + p->height / 2 > (int64_t)c->y + c->height;
        if (!outside || p->id == c->leader || active <= 1) { p->separation_ticks = 0; continue; }
        if (resized) continue;
        if (++p->separation_ticks < s->ticks_per_second) continue;
        if (p->held_object != COOP_NO_ENTITY)
            emit(s, COOP_ACTION_DROP_OBJECT, p->id, p->held_object, 0);
        p->held_object = COOP_NO_ENTITY;
        p->life = COOP_CATCHUP_BUBBLE;
        p->recovery_ticks = s->ticks_per_second / 2;
        p->separation_ticks = 0;
        --active;
    }
    return !s->failed;
}

bool coop_camera_update(CoopSession *s,int32_t w,int32_t h,int32_t rw,int32_t rh,bool autoscroll) {
    if(!s)return false;
    bool resized=s->camera.width!=w || s->camera.height!=h;
    return coop_camera_frame(s,w,h,rw,rh,autoscroll) && coop_camera_check_separation(s,resized);
}

bool coop_session_recover(CoopSession *s, CoopSafePlacement safe, void *context) {
    if (!s || !safe || s->failed) return false;
    if (!s->advancing || s->outcome != COOP_CONTINUE) return true;
    /* Snapshot anchor IDs so newly recovered actors cannot create a chain of
     * same-frame recovery anchors dependent on roster iteration order. */
    if (s->player_count > SIZE_MAX / sizeof(CoopPlayerId)) return false;
    CoopPlayerId *anchors = malloc(s->player_count * sizeof(*anchors));
    if (!anchors) { s->failed = true; return false; }
    size_t n = 0;
    for (size_t i = 0; i < s->player_count; ++i)
        if (s->players[i].life == COOP_PLAYING) anchors[n++] = s->players[i].id;
    for (size_t i = 0; i < s->player_count; ++i) {
        CoopPlayer *p = &s->players[i];
        if ((p->life != COOP_DEATH_BUBBLE && p->life != COOP_CATCHUP_BUBBLE) || p->recovery_ticks) continue;
        CoopPlayerId winner = COOP_NO_PLAYER;
        uint64_t best = UINT64_MAX;
        int32_t wx = 0, wy = 0;
        for (size_t j = 0; j < n; ++j) {
            const CoopPlayer *a = coop_player_const(s, anchors[j]);
            int32_t x = 0, y = 0;
            if (!safe(p, a, &x, &y, context)) continue;
            uint64_t d = distance2(p->x, p->y, x, y);
            if (winner == COOP_NO_PLAYER || d < best || (d == best && coop_player_precedes(s,a->id,winner))) {
                winner = a->id; best = d; wx = x; wy = y;
            }
        }
        if (winner == COOP_NO_PLAYER) continue;
        if (p->life == COOP_DEATH_BUBBLE) {
            p->power = p->checkpoint_upgrade ? COOP_BIG : COOP_SMALL;
            p->protection_ticks = 2 * s->ticks_per_second;
        }
        p->life = COOP_PLAYING; p->x = wx; p->y = wy;
        p->checkpoint_upgrade = false;
        p->pressed_input = 0;
        emit(s, COOP_ACTION_RECOVER, p->id, COOP_NO_ENTITY, winner);
    }
    free(anchors);
    return !s->failed;
}

void coop_session_restart(CoopSession *s) {
    if (!s) return;
    for (size_t i = 0; i < s->player_count; ++i) {
        CoopPlayer *p = &s->players[i];
        p->life = COOP_PLAYING; p->power = COOP_SMALL;
        p->mount = p->held_object = COOP_NO_ENTITY;
        p->star_ticks = p->protection_ticks = p->recovery_ticks = p->separation_ticks = 0;
        p->checkpoint_upgrade = false;
        p->pressed_input = 0;
    }
    s->outcome = COOP_CONTINUE;
    s->event_count = s->action_count = 0;
    s->camera.initialized = false;
    s->camera.leader = COOP_NO_PLAYER;
}

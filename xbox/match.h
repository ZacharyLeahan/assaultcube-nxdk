#ifndef AC_XBOX_MATCH_H
#define AC_XBOX_MATCH_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { MAP = 256, FRAG_LIMIT = 3 };
typedef struct {
    uint8_t kind;
    int8_t floor, ceil;
    uint8_t wall, ground, roof, upper, tag;
} Cube;
typedef struct { float move, strafe, turn, look; int fire, refill, swap, start, assist_choice, jump, crouch, showmap; } MatchInput;
typedef enum { READY, PLAYING, INTERMISSION } MatchPhase;
typedef enum { SILENT, RIFLE_SHOT, RIFLE_RELOAD, PISTOL_SHOT, PISTOL_RELOAD,
    PLAYER_JUMP, PLAYER_LAND, PLAYER_STEP, PLAYER_PAIN, PLAYER_DEATH,
    PICKUP_AMMO, PICKUP_HEALTH, PICKUP_HELMET, PICKUP_ARMOUR, WEAPON_CHANGE } MatchSound;
typedef struct { int x, y, icon; float base; } ItemView;
typedef struct { float x,y,z; int axis; unsigned expires; } BulletMark;
typedef struct {
    float x,y,z;
    short dx,dy,dz;
    unsigned born, lifetime;
} ImpactSpark;
typedef struct {
    float px, py, eye, angle, pitch, tx, ty, bot_yaw;
    int health, armour, magazine, reserve, pistol, player_frags, bot_frags, bot_alive;
    float stride_blend, weapon_kick;
    float gun_side, gun_up, gun_forward, gun_rotation, gun_back;
    unsigned gun_pose, next_gun_pose;
    int bot_pose, bot_flash, respawning, reload, flash, wide_crosshair;
    float damage_alpha;
    int bot_shot_pending;
    const unsigned *damage_directions;
    unsigned seconds;
    MatchPhase phase;
    int classic_assist;
    MatchSound bot_sound, movement_sound, pickup_sound, pain_sound;
    const ItemView *items;
    int item_count;
    const BulletMark *marks;
    unsigned mark_count, elapsed_ms;
    const ImpactSpark *sparks;
    unsigned spark_count;
} MatchView;

const Cube *map_cube(int x, int y);
float map_floor(const Cube *cube);
int map_solid(const Cube *cube);
void match_prepare(void);
void match_update(const MatchInput *input, unsigned elapsed, float motion_seconds);
MatchSound match_attack(const MatchInput *input);
int match_targeted(const MatchView *view);
void match_view(MatchView *view);
#ifdef __cplusplus
}
#endif
#endif

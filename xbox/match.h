#ifndef AC_XBOX_MATCH_H
#define AC_XBOX_MATCH_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { MAP = 256, FRAG_LIMIT = 3 };
#define BOT_EYE_HEIGHT 4.5f
typedef struct {
    uint8_t kind;
    int8_t floor, ceil;
    uint8_t wall, ground, roof, upper, tag;
} Cube;
typedef struct { float move, strafe, turn, look; int fire, refill, swap, start, assist_choice, jump, crouch, showmap; } MatchInput;
typedef enum { READY, PLAYING, INTERMISSION } MatchPhase;
typedef enum { SILENT, RIFLE_SHOT, RIFLE_RELOAD, PISTOL_SHOT, PISTOL_RELOAD,
    PLAYER_JUMP, PLAYER_LAND, PLAYER_PAIN, PLAYER_DEATH,
    PICKUP_AMMO, PICKUP_HEALTH, PICKUP_HELMET, PICKUP_ARMOUR, WEAPON_CHANGE,
    BOT_PAIN1,BOT_PAIN2,BOT_PAIN3,BOT_PAIN4,BOT_PAIN5,ALT_DEATH,WATER_ENTER,WATER_EXIT,
    FOOTSTEPS,CROUCH_STEPS,WATER_STEPS,WATER_CROUCH_STEPS,CROUCH_IN,CROUCH_OUT,
    BULLET_AIR1,BULLET_AIR2,BULLET_IMPACT,BULLET_WATER,SOFT_LAND } MatchSound;
typedef struct { float x,y,z; } MatchPoint;
typedef struct { MatchPoint pos; MatchSound sound; } PositionalSound;
typedef struct { MatchPoint start,end; unsigned expires; } ShotLine;
typedef struct { int x, y, icon; float base; } ItemView;
typedef struct { float x,y,z; int axis; unsigned expires; } BulletMark;
typedef struct {
    float x,y,z;
    short dx,dy,dz;
    unsigned born, lifetime;
} SplashParticle;
typedef struct { SplashParticle motion; unsigned on_floor; } BloodParticle;
typedef struct {
    float px, py, eye, angle, pitch, tx, ty, bot_yaw,bot_tilt;
    MatchPoint bot_head;
    int health, armour, magazine, reserve, pistol, player_frags, bot_frags, bot_alive;
    float bot_pose_blend, weapon_kick;
    float gun_side, gun_up, gun_forward, gun_rotation, gun_back;
    unsigned gun_pose, next_gun_pose;
    unsigned bot_flash_end_ms;
    int bot_pose, next_bot_pose, respawning, reload, flash, wide_crosshair;
    float damage_alpha;
    int bot_shot_pending,empty_click;
    const unsigned *damage_directions;
    unsigned seconds;
    MatchPhase phase;
    int classic_assist;
    MatchSound bot_sound, movement_sound, pickup_sound, pain_sound, bot_voice;
    MatchSound footsteps,bot_footsteps,stance_sound;
    PositionalSound air,impact;
    ShotLine bot_line;
    const ItemView *items;
    int item_count;
    const BulletMark *marks;
    unsigned mark_count, elapsed_ms;
    const SplashParticle *sparks;
    unsigned spark_count;
    const BloodParticle *blood;
    unsigned blood_count;
} MatchView;

const Cube *map_cube(int x, int y);
float map_floor(const Cube *cube);
int map_solid(const Cube *cube);
void match_prepare(void);
void match_update(const MatchInput *input, unsigned elapsed, float motion_seconds);
MatchSound match_attack(const MatchInput *input);
int match_targeted(const MatchView *view);
MatchPoint bot_model_point(const MatchView *view,MatchPoint local);
void match_view(MatchView *view);
#ifdef __cplusplus
}
#endif
#endif

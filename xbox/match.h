#ifndef AC_XBOX_MATCH_H
#define AC_XBOX_MATCH_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { MAP = 256, FRAG_LIMIT = 3, MARK_CAPACITY = 224 };
#define PLAYER_EYE_HEIGHT 4.5f
#define PLAYER_ABOVE_EYE .7f
// tan(50 degrees / 2) relative to the normal 90-degree horizontal FOV.
#define SCOPE_TAN_HALF_FOV .46630766f
#define PLAYER_RADIUS 1.1f
typedef struct {
    uint8_t kind;
    int8_t floor, ceil;
    uint8_t wall, ground, roof, upper, tag;
} Cube;
typedef struct { float move, strafe, turn, look; int fire, refill, swap, start, assist_choice, jump, crouch, showmap, knife_select, scope; } MatchInput;
typedef enum { READY, PLAYING, INTERMISSION } MatchPhase;
typedef enum { VIEW_RIFLE,VIEW_SMG,VIEW_CARBINE,VIEW_PISTOL,VIEW_KNIFE,VIEW_SNIPER } MatchWeapon;
typedef enum { SILENT, RIFLE_SHOT, RIFLE_RELOAD, PISTOL_SHOT, PISTOL_RELOAD,
    PLAYER_JUMP, PLAYER_LAND, PLAYER_PAIN, PLAYER_DEATH,
    PICKUP_AMMO, PICKUP_HEALTH, PICKUP_HELMET, PICKUP_ARMOUR, WEAPON_CHANGE,
    BOT_PAIN1,BOT_PAIN2,BOT_PAIN3,BOT_PAIN4,BOT_PAIN5,ALT_DEATH,WATER_ENTER,WATER_EXIT,
    FOOTSTEPS,CROUCH_STEPS,WATER_STEPS,WATER_CROUCH_STEPS,CROUCH_IN,CROUCH_OUT,
    BULLET_AIR1,BULLET_AIR2,BULLET_IMPACT,BULLET_WATER,SOFT_LAND,KNIFE_SWING,SMG_SHOT,SMG_RELOAD,CARBINE_SHOT,CARBINE_RELOAD,SNIPER_SHOT,SNIPER_RELOAD } MatchSound;
typedef struct { float x,y,z; } MatchPoint;
typedef struct { MatchPoint pos; float yaw,pitch,sink; unsigned model; } GibView;
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
typedef struct { int first,second; float blend; } BotPose;
static inline MatchPoint mix_point(MatchPoint a,MatchPoint b,float t) {
    MatchPoint result={a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z)};
    return result;
}
typedef struct {
    float px, py, eye, angle, pitch, tx, ty, bot_floor, bot_yaw,bot_tilt,bot_sink;
    MatchPoint bot_head,camera;
    float camera_yaw,camera_pitch,player_floor,death_frame;
    struct { const char *text; unsigned age; } frag_messages[3];
    int health, armour, magazine, reserve, player_frags, bot_frags, bot_alive, bot_pistol, radar_active;
    const GibView *gibs; unsigned gib_count; int hide_bot, gib_sound;
    BotPose current_pose,previous_pose;
    float pose_transition,gun_pose_blend,flash_angle;
    float gun_side, gun_up, gun_forward, gun_rotation, gun_back;
    unsigned gun_pose, next_gun_pose;
    unsigned bot_flash_end_ms,player_light,bot_light;
    int scoped, respawning, reload, hide_crosshair, flash, wide_crosshair;
    float damage_alpha;
    int bot_shot_pending,respawn_work_pending,empty_click;
    const unsigned *damage_directions;
    unsigned seconds;
    MatchPhase phase;
    int classic_assist;
    MatchWeapon weapon,selected_primary;
    int bot_supply,bot_rifle_mag,bot_rifle_ammo,bot_pistol_mag,bot_pistol_ammo;
    MatchSound bot_sound, movement_sound, pickup_sound, pain_sound, bot_voice;
    MatchSound footsteps,bot_footsteps,stance_sound;
    PositionalSound air,impact;
    ShotLine bot_line;
    MatchPoint shot_end,shot_direction;
    int shot_contact; // 0 miss, 1 body, 2 head
    const ItemView *items;
    int item_count;
    const BulletMark *marks;
    unsigned first_mark, mark_count, elapsed_ms;
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

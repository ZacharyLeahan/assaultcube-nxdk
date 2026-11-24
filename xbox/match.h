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
typedef struct { float move, strafe, turn, look; int fire, refill, swap, start; } MatchInput;
typedef enum { READY, PLAYING, INTERMISSION } MatchPhase;
typedef enum { SILENT, RIFLE_SHOT, RIFLE_RELOAD, PISTOL_SHOT, PISTOL_RELOAD } MatchSound;
typedef struct { int x, y, icon; } ItemView;
typedef struct {
    float px, py, eye, angle, pitch, tx, ty, bot_yaw;
    int health, armour, magazine, reserve, pistol, player_frags, bot_frags, bot_alive;
    int bot_pose, bot_flash, respawning, reload, flash, hitflash, hurt;
    unsigned seconds;
    MatchPhase phase;
    MatchSound bot_sound;
    const ItemView *items;
    int item_count;
} MatchView;

const Cube *map_cube(int x, int y);
int map_solid(const Cube *cube);
void match_prepare(void);
void match_update(const MatchInput *input, unsigned elapsed);
MatchSound match_attack(const MatchInput *input, int aimed);
int match_targeted(const MatchView *view);
void match_view(MatchView *view);
#ifdef __cplusplus
}
#endif
#endif

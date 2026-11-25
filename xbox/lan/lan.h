#ifndef AC_XBOX_LAN_H
#define AC_XBOX_LAN_H
#include "../match.h"
#ifdef __cplusplus
extern "C" {
#endif
void lan_init(void);
void lan_poll(unsigned now);
void lan_send_sounds(const MatchView *v);
int match_lan_sound_id(MatchSound sound);
void match_lan_sound(int id);
void lan_send(const MatchView *v,const MatchInput *input,MatchSound effect,unsigned now);
const char *lan_status(void);
int lan_connected(void);
const char *lan_opponent_name(void);
int lan_opponent_number(void);
int lan_skin(int local);
typedef struct { const char *name; int frags,deaths,ping; } LanScore;
int lan_scores(LanScore rows[2],const MatchView *view);
void lan_toggle_primary(unsigned now);
void match_lan_map(unsigned seed);
void match_lan_spawn(int health,int armour,int primary,int gun,const int *ammo,const int *mag);
int match_lan_selected_weapon(void);
int match_lan_take_respawn(void);
unsigned match_lan_shot_time(unsigned now);
void match_lan_remote_spawn(int gun);
void match_lan_remote_weapon(int gun);
void match_lan_remote_reload(int gun);
typedef struct {
    MatchPoint feet,velocity;
    float yaw,pitch;
    int move,strafe,on_floor,on_ladder,crouching;
} LanPose;
void match_lan_remote(const LanPose *pose);
void match_lan_hide(void);
void match_lan_remote_death(int gib);
typedef enum { NADE_PRIME,NADE_RELEASE,NADE_BLAST } GrenadeAction;
typedef struct { int local; float distance; MatchPoint direction; } GrenadeHit;
typedef struct {
    GrenadeAction action;
    unsigned id,cooked;
    MatchPoint position,velocity;
    int hit_count;
    GrenadeHit hits[2];
} GrenadeEvent;
int match_lan_grenade_events(const GrenadeEvent **events);
void match_lan_throw(MatchPoint from,MatchPoint velocity,int cooked);
void match_lan_shot(int gun,float x,float y,float z);
void match_lan_damage(int local_victim,int from_remote,int damage,int health,int armour);
void match_lan_frag(int local_victim,int suicide,int gun,int gib,const char *opponent);
void match_lan_health(int health,int armour);
int match_lan_take_jump(void);
void match_lan_hitpush(int gun,int damage,float x,float y,float z);
void match_lan_time(unsigned remaining_ms);
void match_lan_score(int local,int frags);
void match_lan_reset_items(void);
int match_lan_item(int entity,int visible,int picked_by_us);
int match_lan_touch_item(void);
void match_lan_stop(void);
#ifdef __cplusplus
}
#endif
#endif

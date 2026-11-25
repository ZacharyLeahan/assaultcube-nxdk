#ifndef AC_XBOX_LAN_H
#define AC_XBOX_LAN_H
#include "../match.h"
#ifdef __cplusplus
extern "C" {
#endif
void lan_init(void);
void lan_poll(unsigned now);
void lan_send(const MatchView *v,const MatchInput *input,MatchSound effect,unsigned now);
const char *lan_status(void);
int lan_connected(void);
void lan_toggle_primary(unsigned now);
void match_lan_map(unsigned seed);
void match_lan_spawn(int health,int armour,int primary,int gun,const int *ammo,const int *mag);
int match_lan_selected_weapon(void);
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
void match_lan_item(int entity,int visible,int picked_by_us);
int match_lan_touch_item(void);
void match_lan_stop(void);
#ifdef __cplusplus
}
#endif
#endif

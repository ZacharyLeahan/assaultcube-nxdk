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
void match_lan_spawn(int health,int armour);
void match_lan_remote(float x,float y,float floor,float yaw,float pitch,int moving);
void match_lan_hide(void);
void match_lan_health(int health,int armour);
void match_lan_time(unsigned remaining_ms);
void match_lan_score(int local,int frags);
void match_lan_stop(void);
#ifdef __cplusplus
}
#endif
#endif

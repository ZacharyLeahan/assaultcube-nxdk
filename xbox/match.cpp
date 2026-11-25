#include "match.h"
#include "lan/lan.h"
#include "geometry.h"
#include "grenade.h"
#include "navigation.h"
#include "bot_route.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "../source/src/playerstate.h"
#define STANDALONE
typedef unsigned char uchar;
#include "../source/src/sound.h"
#include "../source/src/weaponstats.inc"

static void shotgun_pattern(MatchPoint from,MatchPoint direction,float length,MatchView *target,bool incoming=false);
static int target_contact(const MatchView *v,float range,bool check_geometry);
namespace {
struct WeaponFeedback { MatchSound shot,reload; const char *verb; };
const WeaponFeedback feedback[]={
    {RIFLE_SHOT,RIFLE_RELOAD,"shredded"},
    {SMG_SHOT,SMG_RELOAD,"sprayed"},
    {CARBINE_SHOT,CARBINE_RELOAD,"picked off"},
    {PISTOL_SHOT,PISTOL_RELOAD,"busted"},
    {KNIFE_SWING,SILENT,"slashed"},
    {SNIPER_SHOT,SNIPER_RELOAD,"punctured"},
    {SHOTGUN_SHOT,SHOTGUN_RELOAD,"peppered"},
    {GRENADE_PULL,SILENT,"gibbed"},
    {PISTOL_SHOT,AKIMBO_RELOAD,"busted"}
};
MatchWeapon view_weapon(int gun) {
    switch (gun) {
    case GUN_PISTOL: return VIEW_PISTOL;
    case GUN_AKIMBO: return VIEW_AKIMBO;
    case GUN_KNIFE: return VIEW_KNIFE;
    case GUN_GRENADE: return VIEW_GRENADE;
    case GUN_SHOTGUN: return VIEW_SHOTGUN;
    case GUN_SUBGUN: return VIEW_SMG;
    case GUN_SNIPER: return VIEW_SNIPER;
    case GUN_CARBINE: return VIEW_CARBINE;
    default: return VIEW_RIFLE;
    }
}
struct ItemLocation { int x, y, height_tenths, kind, entity; };
#include "generated/starts.h"
#include "generated/items.h"
#include "generated/water.h"
#include "generated/bot_head.h"
constexpr int start_count = sizeof(starts)/sizeof(starts[0]);
constexpr int item_count = sizeof(locations)/sizeof(locations[0]);
constexpr unsigned duration = 90000;
constexpr unsigned bot_match_duration=15*60*1000;
bool stock_round,respawn_trigger,request_respawn;
int last_bot_gun;
unsigned bot_scope_since;
unsigned bot_switch_after,bot_akimbo_until;
struct BotSkill {
    int pitch_min,pitch_max,yaw_min,yaw_max,pitch_error_min,pitch_error_max,yaw_error_min,yaw_error_max;
    int attack_min,attack_max,search_min,search_max,detect_radius,circle_strafe;
    float view_cosine;
};
#include "generated/bot_skill.h"
#include "generated/bot_names.h"
const char *offline_bot_name="Bot";
unsigned menu_time;
constexpr unsigned switch_ms = 400;
constexpr unsigned player_respawn_ms=2000,bot_respawn_ms=5000;
constexpr unsigned frag_message_ms=3000;
struct FragMessage { char text[72]; unsigned time; };
FragMessage frag_messages[3];
constexpr float radians = .017453293f;
constexpr float max_pitch = 90*radians;
struct Fighter {
    playerstate state;
    float x, y, yaw, pitch, eye_height;
    float velocity_x, velocity_y, turn_boost;
    float sway_x, sway_y;
    float pitch_velocity;
    unsigned shots, shot_until;
    int turn_direction;
    int frags,deaths;
    unsigned respawn_at, ready_at, reload_at, reload_hud_until, flash_until, light_until, pain_until;
};
Fighter player, bot;
float feet_z, stance, jump_speed, sway_vertical;
unsigned air_ms,jump_start_ticks;
unsigned sway_ms;
float flash_angle;
MatchPoint death_camera;
float death_yaw,death_pitch;
bool grounded, held_jump, in_water, on_ladder, lan_jump;
bool jump_pending,crouched_in_air;
unsigned last_jump;
float last_jump_floor;
float net_vertical_velocity;
unsigned last_splash;
unsigned ear_damage_until;
unsigned clock_ms, frame_start_ms, next_route, next_enemy_search, bot_attack_at, hurt_until;
unsigned damage_directions[8];
unsigned radar_action_until;
BotRoute bot_route;
float bot_vertical;
unsigned bot_air_ms;
bool bot_airborne,bot_jump_request;
struct MovementHistory {
    MatchPoint samples[7];
    int size,next;
    unsigned update_after;
    void record(MatchPoint p,unsigned now) {
        if(now<update_after) return;
        update_after=now+100;
        // poshist::update compares to slot zero in the original client.
        float dx=p.x-samples[0].x,dy=p.y-samples[0].y,dz=p.z-samples[0].z;
        if(size && dx*dx+dy*dy+dz*dz<16) return;
        samples[next]=p;next=(next+1)%7;
        if(size<7) ++size;
    }
};
MovementHistory player_trail,bot_trail;
MatchPoint hunt_spot;
bool hunting;
unsigned hunt_after,patrol_retry,check_progress,recover_until;
float progress_x,progress_y,recover_x,recover_y;
int progress_item;
unsigned skip_supply_until[item_count];
int supply_index=-1;
unsigned next_supply_check;
bool lan_item_visible[item_count];
unsigned lan_retry_at[item_count];
MatchSound lan_pickup_sound,lan_pain_sound,lan_actor_voice;
unsigned unavailable_until[item_count], random_state, aim_state;
int spawn_cycle=-1, initial_spawn_cycles=2;
unsigned spawn_seed;
LanPose remote_pose;
MatchPoint correction;
float yaw_correction,pitch_correction;
unsigned pose_received,remote_air_ms;
bool remote_air_crouch;
float remote_stance=PLAYER_EYE_HEIGHT;
int bot_pose;
float stride_phase,stride_rate;
unsigned combat_move_until,combat_jump_after;
bool combat_jump_pending;
float combat_forward,combat_side;
struct BotClip {
    int first,count;
    unsigned action,sample_time;
    float phase,frames_per_ms;
};
BotClip current_clip,previous_clip;
unsigned clip_switch;
unsigned idle_ms;
bool release_after_switch,semi_trigger_used,empty_click,bot_saw_player;
unsigned switch_end;
unsigned intermission_age;
int next_gun, previous_firearm;
unsigned akimbo_expires;
bool draw_akimbo,akimbo_left;
MatchPhase phase;
bool classic_assist;
MatchSound bot_sound;
MatchSound movement_sound, pickup_sound, pain_sound, bot_voice;
MatchSound step_sound,stance_sound;
struct ActorSounds {
    MatchSound effects[8];
    unsigned count;
    void add(MatchSound sound) { if(count<8) effects[count++]=sound; }
};
ActorSounds actor_sounds,network_sounds;
// These are the game sounds Windows sends with audiomanager::playsoundc.
struct NetworkSound { int id;MatchSound effect; };
constexpr NetworkSound network_effects[]={
    {S_JUMP,PLAYER_JUMP},{S_SOFTLAND,SOFT_LAND},{S_HARDLAND,PLAYER_LAND},
    {S_CROUCH,CROUCH_IN},{S_UNCROUCH,CROUCH_OUT},
    {S_ITEMAMMO,PICKUP_AMMO},{S_ITEMHEALTH,PICKUP_HEALTH},{S_ITEMHELMET,PICKUP_HELMET},
    {S_ITEMARMOUR,PICKUP_ARMOUR},{S_ITEMAKIMBO,AKIMBO_PICKUP},{S_AKIMBOOUT,AKIMBO_END}
};
PositionalSound air_sound,impact_sound,lan_air,lan_impact;
ShotLine bot_line;
bool was_crouching;
ItemView item_views[item_count];
// An SMG and rifle can fire at most 209 rounds in the 10-second decal lifetime.
BulletMark marks[MARK_CAPACITY];
unsigned first_mark, mark_count;
SplashParticle sparks[64];
BloodParticle blood[64];
unsigned next_blood,blood_count;
struct Gib {
    GibView view;
    MatchPoint velocity;
    unsigned remaining;
    bool resting;
};
Gib gibs[12]; // Two knife frags reach the three-point duel limit.
GibView gib_views[12];
unsigned gib_count,gib_remainder;
bool hide_bot,hide_player_body,gib_sound,lan_match,lan_gib_sound;
MatchPoint gib_origin;
unsigned lan_end_ms,scope_since;
bool scoped;
MatchSound lan_shot_sound;
unsigned player_shot_delay_ms;
MatchPoint shot_end,shot_direction;
int shot_contact,shotgun_info;
unsigned next_spark,spark_count,effect_seed;
unsigned effect_random(unsigned limit) {
    effect_seed=1664525u*effect_seed+1013904223u;
    return (effect_seed>>8)%limit;
}
void spawn_gibs(const Fighter &f) {
    if(&f==&player) hide_player_body=true;
    else hide_bot=true;
    gib_sound=true;gib_origin={f.x,f.y,f.eye_height};
    for (unsigned i=0;i<6 && gib_count<12;++i) {
        Gib &g=gibs[gib_count++];
        float angle=effect_random(360)*radians;
        g.view={{f.x,f.y,f.eye_height-PLAYER_ABOVE_EYE},(float)effect_random(360),(float)effect_random(360),0,effect_random(3)};
        g.velocity={sinf(angle)*effect_random(1000)*.0003f,cosf(angle)*effect_random(1000)*.0003f,
                    effect_random(1000)*.0003f};
        g.remaining=7000+100*effect_random(10);
        g.resting=false;
    }
}
static bool gib_blocked(MatchPoint p,MatchPoint d) {
    SurfaceHit hit;
    trace_surface(p.x,p.y,p.z,p.x+d.x,p.y+d.y,p.z+d.z,&hit);
    return hit.fraction<1 || prop_depth(p,d,1)<1;
}
void update_gibs(unsigned elapsed) {
    gib_remainder+=elapsed;
    unsigned ticks=gib_remainder/5;
    gib_remainder%=5;
    for (unsigned i=0;i<gib_count;) {
        Gib &g=gibs[i];
        if (elapsed>=g.remaining) { g=gibs[--gib_count]; continue; }
        g.remaining-=elapsed;
        float decay=g.remaining<2000?2000-g.remaining:0;
        g.view.sink=decay*decay*decay/4e9f;
        MatchPoint &p=g.view.pos,&v=g.velocity;
        for (unsigned t=0;t<ticks && !g.resting;++t) {
            float speed=p.z<WATER_LEVEL?.15f/12:.15f;
            v.z-=.5f*9.81f/4*.005f;
            float dx=v.x*speed,dy=v.y*speed,dz=v.z*speed;
            float rot=3*sqrtf(dx*dx+dy*dy+dz*dz);
            g.view.yaw+=rot; g.view.pitch+=rot;
            if (dx && gib_blocked(p,{dx+copysignf(.2f,dx),0,0})) { v.x*=-.7f; v.y*=.7f; v.z*=.7f; }
            else p.x+=dx;
            if (dy && gib_blocked(p,{0,dy+copysignf(.2f,dy),0})) { v.y*=-.7f; v.x*=.7f; v.z*=.7f; }
            else p.y+=dy;
            if (gib_blocked(p,{0,0,dz+(dz<0?-.3f:0)})) {
                g.resting=dz<0 && fabsf(v.z)<.02f && hypotf(v.x,v.y)<.02f;
                v.z*=-.5f; v.x*=.5f; v.y*=.5f;
            } else p.z+=dz;
        }
        ++i;
    }
}
SplashParticle splash_particle(float x,float y,float z,unsigned max_lifetime) {
    short dx,dy,dz;
    do {
        dx=(int)effect_random(300)-150;
        dy=(int)effect_random(300)-150;
        dz=(int)effect_random(300)-150;
    } while (dx*dx+dy*dy+dz*dz>150*150);
    return {x,y,z,dx,dy,dz,clock_ms,effect_random(max_lifetime)};
}
void spark_splash(float x,float y,float z) {
    for (int i=0;i<5;++i) {
        sparks[next_spark]=splash_particle(x,y,z,750);
        next_spark=(next_spark+1)%64;
        if (spark_count<64) ++spark_count;
    }
}
struct ActiveGrenade {
    GrenadeFlight flight;
    unsigned expires,id;
    bool local;
};
enum GrenadeState { GRENADE_IDLE,GRENADE_HELD,GRENADE_RELEASING };
GrenadeState grenade_state;
unsigned primed_at,thrown_at;
unsigned bot_primed_at;
GrenadeEvent grenade_events[16];
int event_count;
ActiveGrenade active_grenades[8];
unsigned active_grenade_count;
ExplosionView explosions[8];
unsigned explosion_count;
void queue_grenade(const GrenadeEvent &event) {
    if(lan_match && event_count<16) grenade_events[event_count++]=event;
}
void resolve_blast(const ActiveGrenade &g);
void throw_grenade(MatchPoint from,MatchPoint velocity,unsigned cooked,bool local=false) {
    if(active_grenade_count==8) return;
    ActiveGrenade &g=active_grenades[active_grenade_count++];
    g={{from,velocity,0,0,false},clock_ms+2000-cooked,clock_ms-cooked,local};
}
void update_grenades(unsigned previous) {
    for(unsigned i=0;i<explosion_count;) {
        if(clock_ms>=explosions[i].end_ms) explosions[i]=explosions[--explosion_count];
        else ++i;
    }
    ActorBounds actors[2];
    int count=0;
    if(!player.respawn_at) actors[count++]={player.x,player.y,feet_z,feet_z+stance+PLAYER_ABOVE_EYE};
    if(!hide_bot && !bot.respawn_at) actors[count++]={bot.x,bot.y,bot.eye_height-(lan_match?remote_stance:PLAYER_EYE_HEIGHT),bot.eye_height+PLAYER_ABOVE_EYE};
    for(unsigned i=0;i<active_grenade_count;) {
        ActiveGrenade &g=active_grenades[i];
        unsigned end=clock_ms<g.expires?clock_ms:g.expires;
        for(unsigned t=previous/5;t<end/5;++t) {
            if(grenade_step(g.flight,actors,count)) impact_sound={g.flight.position,effect_random(2)?GRENADE_BOUNCE1:GRENADE_BOUNCE2};
        }
        if(clock_ms<g.expires) {++i;continue;}
        MatchPoint p=g.flight.position;
        if(explosion_count<8) explosions[explosion_count++]={p,g.expires+600};
        impact_sound={p,GRENADE_EXPLODE};
        for(int j=0;j<50;++j) {
            sparks[next_spark]=splash_particle(p.x,p.y,p.z,300);
            next_spark=(next_spark+1)%64;
            if(spark_count<64) ++spark_count;
        }
        if(g.local || !lan_match) resolve_blast(g);
        active_grenades[i]=active_grenades[--active_grenade_count];
    }
}
void mark_surface(const SurfaceHit &hit) {
    if (!hit.can_mark) return;
    if (mark_count==MARK_CAPACITY) {
        first_mark=(first_mark+1)%MARK_CAPACITY;
        --mark_count;
    }
    unsigned tail=(first_mark+mark_count)%MARK_CAPACITY;
    marks[tail]={hit.x,hit.y,hit.z,hit.axis,clock_ms+10000};
    ++mark_count;
}
void update_blood(unsigned elapsed) {
    for (unsigned i=0;i<blood_count;++i) {
        BloodParticle &b=blood[i];
        SplashParticle &p=b.motion;
        unsigned age=clock_ms-p.born;
        if (b.on_floor || age>p.lifetime) continue;
        float travel=elapsed/20000.f;
        p.x+=travel*p.dx; p.y+=travel*p.dy;
        p.z+=travel*p.dz-age*(float)elapsed/30000.f;
        const Cube *c=map_cube((int)p.x,(int)p.y);
        if (c->kind==4 && p.z<=c->floor) {
            p.z=c->floor+.005f; p.born=clock_ms; p.lifetime=10000;
            b.on_floor=1;
        }
    }
}
unsigned random_number() {
    random_state = random_state*1664525u + 1013904223u;
    return random_state >> 8;
}
float ground(float x, float y) { return map_floor(map_cube((int)x,(int)y)); }
float distance(float x,float y,float tx,float ty) { return hypotf(tx-x,ty-y); }
float eye(const Fighter &f) { return f.eye_height; }
void spawn(Fighter &f, int index, float offset=.5f) {
    if(&f==&bot) {
        f.state.nextprimary=stock_round && !lan_match ? GUN_CARBINE+random_number()%5 : GUN_ASSAULT;
        last_bot_gun=f.state.nextprimary;
        bot_scope_since=0;bot_air_ms=0;bot_vertical=0;bot_airborne=bot_jump_request=false;
        bot_switch_after=bot_akimbo_until=combat_jump_after=0;combat_jump_pending=false;
        bot_primed_at=0;
    }
    f.x = starts[index][0]+offset; f.y = starts[index][1]+offset;
    f.yaw = (starts[index][2]*.1f-90)*radians; f.pitch = 0;
    f.eye_height=body_floor(f.x,f.y)+PLAYER_EYE_HEIGHT;
    if(!lan_match && stock_round) {
        (&f==&player?player_trail:bot_trail)={};
        hunt_after=0;hunting=false;
        bot_route.reset();patrol_retry=0;check_progress=recover_until=0;
        memset(skip_supply_until,0,sizeof(skip_supply_until));
    }
    if (&f==&player) {
        hide_player_body=false;scoped=false; grenade_state=GRENADE_IDLE;
        akimbo_expires=0;draw_akimbo=akimbo_left=false;
        radar_action_until=0;
        release_after_switch=false;switch_end=0; previous_firearm=f.state.nextprimary;
        memset(damage_directions,0,sizeof(damage_directions));
        ear_damage_until=hurt_until=0;
        feet_z=f.eye_height-PLAYER_EYE_HEIGHT; stance=PLAYER_EYE_HEIGHT; jump_speed=0; air_ms=jump_start_ticks=0;
        grounded=true; held_jump=lan_jump=on_ladder=false; net_vertical_velocity=0;
        jump_pending=crouched_in_air=false;last_jump=0;
        in_water=false; last_splash=0;
        sway_ms=0; sway_vertical=0;
        was_crouching=false; step_sound=stance_sound=SILENT;
    }
    if (&f==&bot) { supply_index=-1; next_supply_check=0; hide_bot=false; current_clip={}; previous_clip={}; }
    f.velocity_x=f.velocity_y=f.turn_boost=0;
    f.sway_x=f.sway_y=0;
    f.pitch_velocity=0; f.shots=f.shot_until=0;
    f.turn_direction=0;
    f.state.respawn();
    f.state.spawnstate(GMODE_BOTDEATHMATCH);
    f.respawn_at = f.ready_at = f.reload_at = f.flash_until = f.light_until = f.pain_until = 0;
    f.reload_hud_until=0;
    if(&f==&player && stock_round && !lan_match && phase==PLAYING) {
        next_gun=f.state.primary;
        switch_end=clock_ms+switch_ms/2;
    }
}
void aim_at_opponent(Fighter &f, const Fighter &opponent) {
    f.yaw = atan2f(opponent.y-f.y,opponent.x-f.x);
    f.pitch = atan2f(eye(opponent)-1-eye(f),fmaxf(1,distance(f.x,f.y,opponent.x,opponent.y)));
}
int next_ffa_start(int visits,const Fighter *opponent);
void begin(MatchPhase initial) {
    active_grenade_count=explosion_count=0; grenade_state=GRENADE_IDLE;event_count=0;
    first_mark=mark_count=0;
    bot_line={}; gib_count=gib_remainder=0; gib_sound=false;
    intermission_age=0;actor_sounds.count=network_sounds.count=0;
    lan_air=lan_impact={};
    memset(frag_messages,0,sizeof(frag_messages));
    next_spark=spark_count=0; effect_seed=1;
    next_blood=blood_count=0;
    clock_ms = frame_start_ms = 0; random_state = 7331; aim_state=7331; phase = initial;
    player.frags = bot.frags = player.deaths = bot.deaths = bot_pose = 0;
    offline_bot_name="Bot";
    stride_phase=0; idle_ms=0;
    combat_move_until=next_enemy_search = next_route = hurt_until = 0;
    bot_attack_at = 1500; bot_saw_player = semi_trigger_used = empty_click = false;
    memset(unavailable_until,0,sizeof(unavailable_until));
    respawn_trigger=request_respawn=false;
    if(stock_round && !lan_match) {
        aim_state=random_state=menu_time;
        offline_bot_name=bot_names[random_number()%(sizeof(bot_names)/sizeof(bot_names[0]))];
        spawn_cycle=-1;
        spawn(player,next_ffa_start(4,nullptr),0);
        spawn(bot,next_ffa_start(4,&player),0);
        return;
    }
    // Open in the same courtyard at two official free-for-all player starts.
    spawn(player,17); spawn(bot,18);
    aim_at_opponent(player,bot); aim_at_opponent(bot,player);
}
// Stock free-for-all selection visits successive map entities. Keep the last
// safe start visited, or the farthest visited start when all are occupied.
int next_ffa_start(int visits,const Fighter *opponent) {
    int best=-1;
    float best_distance=-1;
    for (int i=0;i<visits;++i) {
        spawn_cycle=(spawn_cycle+1)%start_count;
        const short *start=starts[spawn_cycle];
        float d=-1;
        if(opponent && !opponent->respawn_at) {
            float dx=start[0]-opponent->x,dy=start[1]-opponent->y,dz=start[3]-eye(*opponent);
            d=dx*dx+dy*dy+dz*dz;
            if(d>=15*15) d=-1;
        }
        if(best<0 || d<0 || (best_distance>=0 && d>best_distance)) {
            best=spawn_cycle;
            best_distance=d;
        }
    }
    return best;
}
void respawn_near(Fighter &f, const Fighter &opponent) {
    if(stock_round) {
        spawn(f,next_ffa_start(1+random_number()%10,&opponent),0);
        supply_index=-1;next_supply_check=next_enemy_search=next_route=0;
        bot_attack_at=clock_ms+1200;bot_saw_player=false;
        return;
    }
    // Sample FFA spawns, preferring covered positions with a route back to combat.
    route_to((int)opponent.x,(int)opponent.y);
    int best = -1, fallback = -1;
    float best_score = -100000;
    int first = random_number()%start_count;
    for (int n = 0; n < start_count; ++n) {
        int i = (n+first)%start_count;
        float x = starts[i][0]+.5f, y = starts[i][1]+.5f, tx, ty;
        float d = distance(x,y,opponent.x,opponent.y);
        if (d < 12) continue;
        if (fallback < 0) fallback = i;
        if (!route_next(x,y,&tx,&ty)) continue;
        float score = -fabsf(d-32);
        float spawn_eye=body_floor(x,y)+PLAYER_EYE_HEIGHT;
        if (!visible(x,y,spawn_eye,opponent.x,opponent.y,eye(opponent))) score += 8;
        if (score > best_score) { best_score = score; best = i; }
    }
    spawn(f,best >= 0 ? best : fallback);
    aim_at_opponent(f,opponent);
    supply_index=-1; next_supply_check=0;
    next_enemy_search = next_route = 0; bot_attack_at = clock_ms+1200; bot.shots=0;
    bot_saw_player = false;
    combat_move_until=0;
}
void impulse(Fighter &f,float x,float y,float z,float amount) {
    f.velocity_x+=amount*x;
    f.velocity_y+=amount*y;
    if (&f==&player && (!grounded || in_water)) {
        float speed=(stance<PLAYER_EYE_HEIGHT && !crouched_in_air?6.4f:16.f)*(in_water?.5f:1.f);
        jump_speed+=amount*z*speed;
    }
}
void hit_impulse(Fighter &f,int gun,int damage,float x,float y,float z) {
    impulse(f,x,y,z,damage*.01f*guns[gun].pushfactor);
    if(&f==&player && gun==GUN_GRENADE && damage>50) ear_damage_until=clock_ms+damage*100;
}
void firing_impulse(Fighter &f,int gun,float dx,float dy,float dz,bool crouched) {
    float recoil=guns[gun].recoil-(gun==GUN_ASSAULT ? effect_random(8)*.01f : 0.f);
    if (gun==GUN_SNIPER && ((&f==&player && scoped && clock_ms-scope_since>180) ||
        (&f==&bot && bot_scope_since && clock_ms-bot_scope_since>180))) recoil/=3;
    impulse(f,dx,dy,dz,-.01f*recoil*(crouched?.75f:1));
}
static MatchPoint retreat_camera(float yaw) {
    float x=player.x,y=player.y,feet=feet_z;
    float back_x=-cosf(yaw),back_y=-sinf(yaw),velocity=0;
    bool on_floor=false;
    // recomputecamera backs up for ten 50 ms movement steps with a reset velocity.
    for (int i=0;i<10;++i) {
        bool water=feet+stance<WATER_LEVEL-.5f;
        float friction=water?20.f:on_floor?6.f:30.f;
        velocity+=(1-velocity)*2.5f/friction;
        float travel=(water?.4f:.8f)*velocity;
        walk_body(&x,&y,back_x*travel,back_y*travel,feet,stance+PLAYER_ABOVE_EYE,1.25f);
        float floor,ceil;
        body_space(x,y,feet,&floor,&ceil);
        feet=fminf(ceil-stance-PLAYER_ABOVE_EYE,fmaxf(floor,feet-(water?.125f:.475f)));
        on_floor=feet<=floor+.01f;
    }
    return {x,y,feet+stance};
}
static bool camera_sees_body(MatchPoint p) {
    float chest=feet_z+stance*.5f;
    return visible(p.x,p.y,p.z,player.x,player.y,chest) &&
           prop_depth(p,{player.x-p.x,player.y-p.y,chest-p.z},1)>=1;
}
void place_death_camera() {
    death_camera=retreat_camera(player.yaw);
    float d=distance(death_camera.x,death_camera.y,player.x,player.y);
    if (!camera_sees_body(death_camera)) {
        death_camera={player.x,player.y,player.eye_height};
        d=0;
    }
    // If a wall pins the rear camera inside the model, use a clear side of the
    // same spot. Test the camera hull, including the level's solid prop bounds.
    if (d<4) {
        const float offsets[]={90,-90,180};
        for (float offset : offsets) {
            MatchPoint p=retreat_camera(player.yaw+offset*radians);
            float next=distance(p.x,p.y,player.x,player.y);
            if (next>d && camera_sees_body(p)) {
                death_camera=p; d=next;
            }
            if (d>=5) break;
        }
    }
    death_yaw=d>.01f?atan2f(player.y-death_camera.y,player.x-death_camera.x):player.yaw;
    death_pitch=atan2f(feet_z+stance*.5f-death_camera.z,fmaxf(1,d));
}
void record_frag(bool local_victim,bool suicide,int gun,bool gib,const char *opponent) {
    frag_messages[2]=frag_messages[1];
    frag_messages[1]=frag_messages[0];
    FragMessage &line=frag_messages[0];
    line.time=clock_ms;
    const char *verb=feedback[view_weapon(gun)].verb;
    if(gun==GUN_SHOTGUN && gib) verb="splattered";
    if(gun==GUN_SNIPER && gib) verb="headshot";
    if(gun==GUN_GRENADE) verb="gibbed";
    if(suicide) snprintf(line.text,sizeof(line.text),"%s suicided%s",local_victim?"you":opponent,local_victim?"!":"");
    else if(local_victim) snprintf(line.text,sizeof(line.text),"you were %s by %s",verb,opponent);
    else snprintf(line.text,sizeof(line.text),"you %s %s",verb,opponent);
}
void player_damage_feedback(int amount,bool show_direction) {
    unsigned remaining=hurt_until>clock_ms?hurt_until-clock_ms:0;
    hurt_until=clock_ms+fminf(3999,remaining+7.f*amount);
    if(show_direction) {
        float relative=atan2f(bot.y-player.y,bot.x-player.x)-player.yaw;
        int sector=(int)floorf(relative/(45*radians)+.5f);
        damage_directions[(unsigned)sector%8]=clock_ms+1000;
    }
}
void actor_blood(const Fighter &victim,int amount) {
    for(int i=0;i<amount/10 && i<64;++i) {
        blood[next_blood]={splash_particle(victim.x,victim.y,eye(victim),3000),0};
        next_blood=(next_blood+1)%64;
        if(blood_count<64) ++blood_count;
    }
}
void damage(Fighter &victim, Fighter &attacker, int gun,float dx,float dy,float dz,bool headshot=false,int spread_damage=0,bool spread_gib=false) {
    float length=sqrtf(dx*dx+dy*dy+dz*dz);
    int base_damage=(gun==GUN_SHOTGUN || gun==GUN_GRENADE)?spread_damage:guns[gun].damage;
    if (length>0) hit_impulse(victim,gun,base_damage,dx/length,dy/length,dz/length);
    int dealt=victim.state.dodamage(base_damage*(headshot?3:1),gun);
    victim.pain_until=clock_ms+300;
    bool dead=victim.state.health<=0;
    MatchSound death=dead && effect_random(2) ? ALT_DEATH : PLAYER_DEATH;
    if (&victim == &player) {
        player_damage_feedback(dealt,&victim!=&attacker);
        pain_sound=dead ? death : PLAYER_PAIN;
        if (dead) { place_death_camera(); switch_end=0; player.pitch_velocity=0; }
    } else {
        bot_voice=dead ? death : (MatchSound)(BOT_PAIN1+effect_random(5));
        actor_blood(victim,base_damage);
    }
    if (victim.state.health > 0) return;
    if (gun==GUN_KNIFE || spread_gib || headshot) spawn_gibs(victim);
    victim.state.health = 0;
    record_frag(&victim==&player,&victim==&attacker,gun,headshot || spread_gib,offline_bot_name);
    ++victim.deaths;
    victim.respawn_at=clock_ms+(&victim==&bot?bot_respawn_ms:player_respawn_ms);
    victim.flash_until = 0;
    attacker.frags+=&attacker==&victim?-1:(gun==GUN_KNIFE || headshot)?2:1;
    if (!stock_round && attacker.frags >= FRAG_LIMIT) phase = INTERMISSION;
}
void resolve_blast(const ActiveGrenade &g) {
    GrenadeEvent event={NADE_BLAST,g.id};
    Fighter *victims[]={&player,&bot};
    for(Fighter *victim:victims) {
        bool own=victim==&player;
        if(victim->respawn_at || (!own && hide_bot)) continue;
        float height=own?stance:lan_match?remote_stance:PLAYER_EYE_HEIGHT;
        MatchPoint dir={victim->x-g.flight.position.x,victim->y-g.flight.position.y,
                        eye(*victim)+(PLAYER_ABOVE_EYE-height)*.5f-g.flight.position.z};
        float dist=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
        if(dist>=EXPDAMRAD) continue;
        if(dist>.00001f) {dir.x/=dist;dir.y/=dist;dir.z/=dist;}
        event.hits[event.hit_count++]={own,dist,dir};
        int amount=(int)(guns[GUN_GRENADE].damage*(1-dist/EXPDAMRAD));
        if(!lan_match) damage(*victim,g.local?player:bot,GUN_GRENADE,dir.x,dir.y,dir.z,false,amount,true);
        else if(own) hit_impulse(player,GUN_GRENADE,amount,dir.x,dir.y,dir.z);
    }
    queue_grenade(event);
}
void launch_grenade(Fighter &owner,unsigned primed,bool drop) {
    bool local=&owner==&player;
    MatchPoint velocity={};
    if(!drop) {
        float speed=1.5f*cosf(owner.pitch);
        velocity={truncf(speed*cosf(owner.yaw)*DNF)/DNF,truncf(speed*sinf(owner.yaw)*DNF)/DNF,
                  truncf(1.5f*sinf(owner.pitch)*DNF)/DNF};
    }
    // moveoutsidebbox advances ten 10 ms steps, with ten collision probes each.
    GrenadeFlight start={{owner.x,owner.y,eye(owner)},velocity};
    const Fighter &opponent=local?bot:player;
    float height=local?(lan_match?remote_stance:PLAYER_EYE_HEIGHT):stance;
    ActorBounds other={opponent.x,opponent.y,eye(opponent)-height,eye(opponent)+PLAYER_ABOVE_EYE};
    for(int i=0;i<10;++i) grenade_step(start,&other,!opponent.respawn_at && (!local || !hide_bot),10,10);
    MatchPoint p=start.position;
    p={truncf(p.x*DNF)/DNF,truncf(p.y*DNF)/DNF,truncf(p.z*DNF)/DNF};
    unsigned cooked=clock_ms-primed;
    throw_grenade(p,velocity,cooked,local);
    if(local) queue_grenade({NADE_RELEASE,primed,cooked,p,velocity});
}
void release_grenade(bool drop) {
    launch_grenade(player,primed_at,drop);
    thrown_at=clock_ms; grenade_state=GRENADE_RELEASING;
}
MatchSound use_grenade(bool fire) {
    if(grenade_state==GRENADE_HELD) {
        if(clock_ms-primed_at>=2000 || player.respawn_at) {release_grenade(true);release_after_switch=true;return GRENADE_THROW;}
        if(!fire && clock_ms-primed_at>=(unsigned)attackdelay(GUN_GRENADE)) {release_grenade(false);return GRENADE_THROW;}
    } else if(grenade_state==GRENADE_RELEASING) {
        if(clock_ms-thrown_at>=325) {
            grenade_state=GRENADE_IDLE;
            if(!player.state.mag[GUN_GRENADE]) {
                next_gun=player.state.primary;player.state.gunselect=next_gun;
                switch_end=clock_ms+switch_ms/2;
            }
        }
    } else if(fire && !player.respawn_at && player.state.mag[GUN_GRENADE]>0 && player.ready_at<=clock_ms) {
        --player.state.mag[GUN_GRENADE];
        primed_at=clock_ms; player.ready_at=clock_ms+attackdelay(GUN_GRENADE);
        grenade_state=GRENADE_HELD;
        queue_grenade({NADE_PRIME,primed_at});
        return GRENADE_PULL;
    }
    return SILENT;
}
MatchSound trigger(Fighter &f, bool fire, bool refill) {
    int gun = f.state.gunselect;
    int &mag = f.state.mag[gun], &reserve = f.state.ammo[gun];
    if (f.respawn_at) return SILENT;
    bool manual=refill && mag<magsize(gun) && reserve>0 && f.reload_at<=clock_ms;
    if (!manual && f.ready_at>clock_ms) return SILENT;
    bool dry=&f==&player && fire && !mag && !manual;
    if (dry) {
        empty_click=true;
        f.shots=0;
        f.ready_at=clock_ms+250;
    }
    if (manual || (!mag && reserve>0 && (fire || &f==&player))) {
        int count = magsize(gun)-mag;
        if (count > reserve) count = reserve;
        mag += count; reserve -= count;
        // Manual reload starts immediately, retaining any outstanding shot wait.
        unsigned start=f.ready_at>clock_ms?f.ready_at:clock_ms;
        f.ready_at=start+reloadtime(gun);
        if (&f==&player) radar_action_until=clock_ms+500;
        f.reload_at=f.ready_at;
        f.reload_hud_until=clock_ms+reloadtime(gun);
        return feedback[view_weapon(gun)].reload;
    }
    if (!fire || !mag) return SILENT;
    --mag;
    // Preserve the part of this frame spent past the previous shot deadline.
    // An idle trigger or a long stall starts a fresh firing interval.
    unsigned delay=attackdelay(gun),action=clock_ms;
    if (f.ready_at && f.ready_at>=frame_start_ms && clock_ms-f.ready_at<delay)
        action=f.ready_at;
    f.ready_at = action+delay;
    if (&f==&player) player_shot_delay_ms=clock_ms-action;
    if (&f==&player) radar_action_until=action+500;
    f.flash_until = action+90;
    f.light_until=clock_ms+100;
    return feedback[view_weapon(gun)].shot;
}
MatchSound pickup_effect(int kind) {
    if(kind==I_AKIMBO) return AKIMBO_PICKUP;
    return kind==I_HEALTH?PICKUP_HEALTH:kind==I_HELMET?PICKUP_HELMET:
           kind==I_ARMOUR?PICKUP_ARMOUR:PICKUP_AMMO;
}
void pickup(Fighter &f,int kind) {
    f.state.pickup(kind);
    if(&f==&player && kind==I_AKIMBO) {
        akimbo_expires=clock_ms+30000;
        draw_akimbo=true;akimbo_left=true;
    }
    if(&f==&bot && kind==I_AKIMBO) bot_akimbo_until=clock_ms+30000;
}
int collect(Fighter &f) {
    if (f.respawn_at) return -1;
    for (int i = 0; i < item_count; ++i) {
        const ItemLocation &p = locations[i];
        float dz=(&f==&player?feet_z:f.eye_height-PLAYER_EYE_HEIGHT)-ground(p.x,p.y)-p.height_tenths*.1f;
        float dx=f.x-p.x, dy=f.y-p.y;
        if ((lan_match ? !lan_item_visible[i] || clock_ms<lan_retry_at[i] : unavailable_until[i]>clock_ms) || dx*dx+dy*dy+dz*dz>=6.25f ||
            !f.state.canpickup(p.kind)) continue;
        if (lan_match) {
            // Rejected requests have no reply. Retry only while still in range.
            lan_retry_at[i]=clock_ms+500;
            return p.entity;
        }
        pickup(f,p.kind);
        if (&f==&player) {
            pickup_sound=pickup_effect(p.kind);
        }
        unsigned delay = p.kind == I_AKIMBO ? 60000 : p.kind == I_GRENADE ? 9000 : p.kind == I_HEALTH ? 20000 : (p.kind == I_HELMET || p.kind == I_ARMOUR) ? 25000 : 8000;
        unavailable_until[i] = clock_ms+delay;
    }
    return -1;
}
// Both volumes use distance along the unit ray as their parameter. Rotating into
// actor-local space makes the pitched torso an upright finite cylinder.
static float torso_contact(MatchPoint delta,float yaw,float tilt,float height,float c,float s,float slope,float range) {
    float cy=cosf(yaw),sy=sinf(yaw),ct=cosf(tilt),st=sinf(tilt);
    float x=delta.x,y=delta.y,z=delta.z;
    float forward=x*cy+y*sy,side=y*cy-x*sy,df=c*cy+s*sy,ds=s*cy-c*sy;
    float ox=forward*ct+z*st,oz=z*ct-forward*st;
    float dx=df*ct+slope*st,dz=slope*ct-df*st;
    float speed2=dx*dx+ds*ds,enter=0,exit=range;
    if (speed2>.000001f) {
        float closest=-(ox*dx+side*ds)/speed2;
        float rx=ox+closest*dx,ry=side+closest*ds;
        float radius2=PLAYER_RADIUS*PLAYER_RADIUS-rx*rx-ry*ry;
        if (radius2<0) return -1;
        float half=sqrtf(radius2/speed2);
        enter=fmaxf(enter,closest-half); exit=fminf(exit,closest+half);
    } else if (ox*ox+side*side>PLAYER_RADIUS*PLAYER_RADIUS) return -1;
    if (fabsf(dz)>.000001f) {
        float bottom=-oz/dz,top=(height-oz)/dz;
        enter=fmaxf(enter,fminf(bottom,top)); exit=fminf(exit,fmaxf(bottom,top));
    } else if (oz<0 || oz>height) return -1;
    return enter<=exit?enter:-1;
}
MatchPoint listener_position() {
    return player.respawn_at?death_camera:MatchPoint{player.x,player.y,eye(player)};
}
void incoming_line(MatchPoint from,MatchPoint target,PositionalSound &air) {
    if(lan_match && !player.respawn_at && effect_random(3)) return;
    float dx=target.x-from.x,dy=target.y-from.y,dz=target.z-from.z;
    float length=hypotf(hypotf(dx,dy),dz);
    if(length<1) return;
    MatchPoint camera=listener_position();
    auto point=[&](float t) -> MatchPoint { return {from.x+t*dx,from.y+t*dy,from.z+t*dz}; };
    float fd=hypotf(distance(from.x,from.y,camera.x,camera.y),from.z-camera.z);
    float td=hypotf(distance(target.x,target.y,camera.x,camera.y),target.z-camera.z);
    float start=td<=10?8:5;
    bot_line={point(.1f+start/length),point((start+2)*.1f),clock_ms+75};
    if(fd<=8 || td<=8) return;
    MatchPoint near=point(fd/(fd+td));
    if(hypotf(distance(near.x,near.y,camera.x,camera.y),near.z-camera.z)<=15)
        air={near,effect_random(2)?BULLET_AIR1:BULLET_AIR2};
}
void incoming_mark(const SurfaceHit &hit,PositionalSound &impact) {
    mark_surface(hit);
    MatchPoint camera=listener_position();
    if(hit.can_mark && hypotf(distance(hit.x,hit.y,camera.x,camera.y),hit.z-camera.z)<=15)
        impact={{hit.x,hit.y,hit.z},hit.z<WATER_LEVEL?BULLET_WATER:BULLET_IMPACT};
}
void bot_fire_ray(float yaw,float pitch,int gun) {
    MatchPoint from={bot.x,bot.y,eye(bot)-.2f};
    float dx=cosf(pitch)*cosf(yaw),dy=cosf(pitch)*sinf(yaw),dz=sinf(pitch);
    MatchPoint target={from.x+512*dx,from.y+512*dy,from.z+512*dz};
    SurfaceHit hit;
    bool wall=trace_surface(from.x,from.y,from.z,target.x,target.y,target.z,&hit);
    float wall_depth=512*hit.fraction;
    float reach=prop_depth(from,{dx,dy,dz},wall_depth);
    MatchView ray={};
    if(gun==GUN_SHOTGUN || gun==GUN_SNIPER || gun==GUN_KNIFE) {
        ray.px=from.x;ray.py=from.y;ray.eye=from.z;ray.angle=yaw;ray.pitch=pitch;
        ray.tx=player.x;ray.ty=player.y;ray.bot_floor=feet_z;ray.bot_height=stance;
        ray.bot_alive=!player.respawn_at;ray.bot_yaw=player.yaw;ray.bot_tilt=player.pitch*.25f;
        ray.bot_head=bot_model_point(&ray,head_poses[was_crouching?135:0]);
    }
    if(gun==GUN_KNIFE) {
        if(target_contact(&ray,3,true)) damage(player,bot,gun,dx,dy,dz);
        return;
    }
    if(gun==GUN_SHOTGUN) {
        shotgun_pattern(from,{dx,dy,dz},reach,&ray,true);
        return;
    }
    bool reaches_wall=wall && reach==wall_depth;
    MatchPoint delta={from.x-player.x,from.y-player.y,from.z-feet_z};
    float contact=torso_contact(delta,player.yaw,player.pitch*.25f,stance,dx,dy,dz,reach);
    bool head=gun==GUN_SNIPER && target_contact(&ray,reach,false)==2;
    if (contact>=0 || head) {
        reach=head?hypotf(distance(from.x,from.y,ray.bot_head.x,ray.bot_head.y),from.z-ray.bot_head.z):contact;
        // Stock raydamage only awards the sniper headshot multiplier to player1.
        damage(player,bot,gun,dx,dy,dz);
    }
    target={from.x+reach*dx,from.y+reach*dy,from.z+reach*dz};
    // PC attackfx still marks a surface within one unit of a struck actor.
    if (reaches_wall && (contact<0 || wall_depth-contact<=1)) {
        incoming_mark(hit,impact_sound);
    }
    spark_splash(target.x,target.y,target.z);
    incoming_line(from,target,air_sound);
}
void move_bot(float dx,float dy) {
    ActorBounds opponent={player.x,player.y,feet_z,feet_z+stance+PLAYER_ABOVE_EYE};
    float floor=stock_round?eye(bot)-PLAYER_EYE_HEIGHT:body_floor(bot.x,bot.y);
    float step=bot_airborne?0.f:1.5f;
    if(stock_round && !bot_airborne && (dx || dy)) {
        float next,ceiling;
        body_space(bot.x+dx,bot.y+dy,floor,&next,&ceiling);
        if(next>floor+step && next<=floor+3.f && ceiling-next>PLAYER_EYE_HEIGHT+PLAYER_ABOVE_EYE)
            bot_jump_request=true;
    }
    walk_past_actor(&bot.x,&bot.y,dx,dy,floor,PLAYER_EYE_HEIGHT+PLAYER_ABOVE_EYE,step,
                    bot.respawn_at || player.respawn_at ? nullptr : &opponent);
}
// Return the remaining signed angle before the turn, used by the firing gate.
float turn_bot_toward(float ideal,float dt) {
    float delta=atan2f(sinf(ideal-bot.yaw),cosf(ideal-bot.yaw));
    bot.yaw+=fmaxf(-75*radians*dt,fminf(75*radians*dt,delta));
    return delta;
}
struct CombatRange { float near,far,max_fire; };
// WeaponInfoTable in the Windows bot's ac_bot_ai.cpp.
constexpr CombatRange rifle_range={20,80,150},pistol_range={3.5f,15,60};
constexpr CombatRange bot_ranges[NUMGUNS]={{0,5,3.5f},pistol_range,{15,80,180},{0,15,50},
    {0,25,60},{10,125,200},rifle_range,{5,30,50},{0,50,50}};
int preferred_bot_weapon(float dist) {
    // CACBot::ChoosePreferredWeapon rates loaded guns on distance and reserves.
    static const int wanted_ammo[]={0,6,2,3,10,3,6,1,0};
    int best=bot.state.gunselect,best_score=0;
    for(int gun=0;gun<NUMGUNS;++gun) {
        if((gun==GUN_AKIMBO && !bot.state.akimbo) ||
           (gun!=GUN_KNIFE && !bot.state.mag[gun])) continue;
        const CombatRange &r=bot_ranges[gun];
        int score=gun>GUN_PISTOL?10:5;
        if(dist>=r.near && dist<=r.far) {
            score+=5;
            if((gun==GUN_PISTOL || gun==GUN_KNIFE) &&
               (bot.state.primary==GUN_CARBINE || bot.state.primary==GUN_SNIPER)) score+=10;
            if(gun==GUN_KNIFE && bot.state.primary==GUN_SHOTGUN) score-=2;
        } else if(dist>(float)r.max_fire || (gun!=GUN_KNIFE && gun!=GUN_SHOTGUN && dist<3)) continue;
        float difference=fabsf(dist-(r.near+r.far)*.5f);
        if(gun==GUN_GRENADE) score+=30;
        score+=difference<.5f?4:difference<=1?2:0;
        int want=wanted_ammo[gun],ammo=bot.state.ammo[gun];
        score+=!want?10:ammo>=4*want?4:ammo>=2*want?3:ammo>=want?1:0;
        if(score>best_score) {best=gun;best_score=score;}
    }
    return best;
}
float bot_clearance(float yaw,float length) {
    MatchPoint from={bot.x,bot.y,eye(bot)},dir={cosf(yaw),sinf(yaw),0};
    SurfaceHit hit;
    trace_surface(from.x,from.y,from.z,from.x+length*dir.x,from.y+length*dir.y,from.z,&hit);
    return prop_depth(from,dir,length*hit.fraction);
}
// Good bots prepare an evasive direction for 125-250 ms before jumping.
bool combat_jump(float distance) {
    if(bot_airborne || clock_ms<=combat_jump_after || distance>=13) return false;
    float angle=atan2f(player.y-bot.y,player.x-bot.x);
    if(fabsf(remainderf(angle-bot.yaw,2*3.14159265f))>=80*radians ||
       1+(int)(random_number()%100)>100-(int)distance*8) return false;
    if(distance<=4 || bot.state.gunselect==GUN_KNIFE) {
        combat_forward=1;combat_side=0;
    } else {
        static const int directions[][2]={{1,-1},{1,1},{-1,0},{-1,-1},{-1,1},{0,-1},{0,1}};
        int total=0;
        for(const auto &dir:directions) {
            if(bot_clearance(bot.yaw+atan2f(dir[1],dir[0]),4)<3.999f) continue;
            int weight=dir[0]>0?(distance>8?20:10):dir[0]<0?(distance<=6?20:10):(distance<6?20:10);
            total+=weight;
            if((int)(random_number()%total)<weight) {combat_forward=dir[0];combat_side=dir[1];}
        }
        if(!total) return false;
    }
    combat_jump_pending=true;
    combat_move_until=clock_ms+125+random_number()%126;
    return true;
}
// Probe the forward or retreat arc in 45-degree steps, as in DoCombatNav.
void choose_combat_move(float distance,const CombatRange &range) {
    if(stock_round) {
        if(combat_jump_pending) {
            bot_jump_request=true;
            combat_jump_pending=false;
            combat_jump_after=clock_ms+1500+random_number()%1301;
            return;
        }
        if(combat_jump(hypotf(distance,eye(player)-eye(bot)))) return;
        if(bot.state.gunselect==GUN_KNIFE) {combat_forward=1;combat_side=0;return;}
    }
    combat_forward=0;
    if (distance<=range.near) combat_forward=-1;
    if (distance>=range.far) combat_forward=1;
    combat_side=0;
    if (!combat_forward) {
        if(skills[stock_round].circle_strafe) {
            int left=(int)bot_clearance(bot.yaw-75*radians,15);
            int right=(int)bot_clearance(bot.yaw+75*radians,15);
            if(left+right) {
                combat_side=(int)(random_number()%(left+right))<left?-1:1;
                combat_move_until=clock_ms+1500+random_number()%1501;
            }
        }
        return;
    }
    float best=-1;
    for (int side=-1;side<=1;++side) {
        float angle=bot.yaw+side*45*radians;
        float dx=combat_forward*cosf(angle),dy=combat_forward*sinf(angle);
        SurfaceHit hit;
        trace_surface(bot.x,bot.y,eye(bot),bot.x+40*dx,bot.y+40*dy,eye(bot),&hit);
        if (hit.fraction>best) { best=hit.fraction; combat_side=combat_forward*side; }
    }
    if (best*40<4) {
        // At a wall the eye ray can fit through a gap narrower than the bot.
        // Try a lateral step with the actual collision hull.
        combat_forward=combat_side=0;
        float best_travel=3.9f;
        for (int side=-1;side<=1;side+=2) {
            float dx=-side*sinf(bot.yaw),dy=side*cosf(bot.yaw),x=bot.x,y=bot.y;
            walk(&x,&y,4*dx,4*dy);
            float travel=(x-bot.x)*dx+(y-bot.y)*dy;
            if (travel>best_travel) { best_travel=travel; combat_side=side; }
        }
    }
    combat_move_until=clock_ms+500;
}
void advance_bot_velocity(float target_x,float target_y,float dt) {
    if (target_x==0 && target_y==0 && fabsf(bot.velocity_x)+fabsf(bot.velocity_y)<.0001f) return;
    float friction=stock_round && bot_airborne?.6f:.12f;
    float fraction=1-powf(1-.005f/friction,dt/.005f),carry=(friction-.005f)*fraction;
    float dx=bot.velocity_x-target_x,dy=bot.velocity_y-target_y;
    move_bot(16*(target_x*dt+dx*carry),16*(target_y*dt+dy*carry));
    bot.velocity_x-=dx*fraction; bot.velocity_y-=dy*fraction;
    if(!stock_round) bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
}
unsigned physics_steps() { return (clock_ms+4)/5-(frame_start_ms+4)/5; }
void update_bot_height() {
    if(!stock_round || lan_match || bot.respawn_at) return;
    float feet=eye(bot)-PLAYER_EYE_HEIGHT,floor,ceiling;
    body_space(bot.x,bot.y,feet,&floor,&ceiling);
    if(!bot_airborne) {
        if(bot_jump_request) {actor_sounds.add(PLAYER_JUMP);bot_airborne=true;bot_vertical=32*23.f/24;bot_air_ms=0;}
        else if(feet>floor+.1f) {bot_airborne=true;bot_vertical=0;bot_air_ms=0;}
        else feet=floor;
    }
    bot_jump_request=false;
    for(unsigned i=0,steps=physics_steps();bot_airborne && i<steps && i<20;++i) {
        bot_air_ms+=5;bot_vertical*=119.f/120;
        feet+=(bot_vertical-9.5f-bot_air_ms/30.f)*.005f;
        if(feet+PLAYER_EYE_HEIGHT+PLAYER_ABOVE_EYE>=ceiling) {
            feet=ceiling-PLAYER_EYE_HEIGHT-PLAYER_ABOVE_EYE;bot_vertical=fminf(bot_vertical,0);
        }
        if(feet<=floor) {
            if(bot_air_ms>200) actor_sounds.add(bot_air_ms>800?PLAYER_LAND:SOFT_LAND);
            feet=floor;bot_airborne=false;bot_vertical=0;bot_air_ms=0;
        }
    }
    bot.eye_height=feet+PLAYER_EYE_HEIGHT;
}
float recoil_degrees(Fighter &fighter,bool crouch) {
    float pitch_change=0;
    unsigned steps=physics_steps();
    float step_gain=.005f*16*(crouch?.75f:1);
    for (unsigned i=0;i<steps && i<20 && fighter.pitch_velocity!=0;++i) {
        pitch_change+=fighter.pitch_velocity*step_gain;
        fighter.pitch_velocity*=.875f;
        if (fighter.pitch_velocity>.001f && fighter.pitch_velocity<.05f)
            fighter.pitch_velocity-=guns[fighter.state.gunselect].recoilbackfade*.01f;
        if (fabsf(fighter.pitch_velocity)<.000001f) fighter.pitch_velocity=0;
    }
    return pitch_change;
}
void start_recoil(Fighter &fighter,int gun) {
    const guninfo &g=guns[gun];
    float ramp=(float)++fighter.shots/g.recoilincrease;
    fighter.pitch_velocity=fminf(ramp*ramp+g.recoilbase*.1f,g.maxrecoil*.1f);
}
void apply_spread(const Fighter &fighter,int gun,float &dx,float &dy,float &dz) {
    int spread=guns[gun].spread;
    if (gun==GUN_SNIPER && (&fighter==&player?scoped:bot_scope_since!=0)) {
        unsigned age=clock_ms-(&fighter==&player?scope_since:bot_scope_since);
        spread=age>=180?1:(int)fmaxf(1,spread*(180-(int)age)/180);
    }
    if (gun==GUN_ASSAULT) spread=fighter.shots>2?55:spread+(fighter.shots==1?5:15);
    if (gun==GUN_SUBGUN) spread=fighter.shots>2?70:spread+(fighter.shots==1?5:10);
    auto jitter=[spread]() { return ((int)(random_number()%spread)-spread/2)*.001f; };
    dx+=jitter(); dy+=jitter(); dz+=jitter();
}
// Windows AimToIdeal speed and offset depend on angular error.
float aim_random(float low,float high) {
    aim_state=aim_state*1664525u+1013904223u;
    return low+(high-low)*((aim_state>>8)/16777216.f);
}
float bot_aim_step(float current,float target,bool vertical,float dt) {
    float delta=remainderf(target-current,2*3.14159265f),degrees=fabsf(delta)/radians;
    const BotSkill &skill=skills[stock_round];
    float speed=vertical?aim_random(skill.pitch_min,skill.pitch_max):aim_random(skill.yaw_min,skill.yaw_max);
    float offset=vertical?aim_random(skill.pitch_error_min*.01f,skill.pitch_error_max*.01f):
                          aim_random(skill.yaw_error_min*.01f,skill.yaw_error_max*.01f);
    float desired=delta+fabsf(delta)*(aim_random(0,1)<.5f?offset:-offset);
    if (fabsf(desired)<=.5f*radians) return current+desired;
    speed*=degrees>=1?degrees/(vertical?80.f:70.f)+.25f:(vertical?.01f:.015f);
    float limit=speed*radians*dt;
    return current+fmaxf(-limit,fminf(limit,desired));
}
bool has_rounds(int gun) {
    return bot.state.mag[gun] || bot.state.ammo[gun];
}
int find_ammunition(bool sees,bool empty) {
    int gun=bot.state.gunselect;
    bool wants_primary=bot.state.ammo[bot.state.primary]<=6;
    bool wants_clips=gun==GUN_PISTOL && bot.state.ammo[GUN_PISTOL]<=6;
    if (!stock_round && !wants_primary && !wants_clips) return -1;
    if (stock_round && sees && distance(bot.x,bot.y,player.x,player.y)<8) return -1;
    float scores[item_count]={};
    for (int i=0;i<item_count;++i) {
        const ItemLocation &p=locations[i];
        if (unavailable_until[i]>clock_ms) continue;
        if(stock_round) {
            if(!bot.state.canpickup(p.kind) || clock_ms<skip_supply_until[i]) continue;
        } else if (!(p.kind==I_AMMO && wants_primary) && !(p.kind==I_CLIPS && wants_clips)) continue;
        float z=ground(p.x,p.y)+p.height_tenths*.1f+PLAYER_EYE_HEIGHT;
        float dist=hypotf(distance(bot.x,bot.y,p.x,p.y),eye(bot)-z);
        bool clear=visible(bot.x,bot.y,eye(bot),p.x,p.y,z);
        // Windows combat item searches use a 25-unit radius and one-unit height limit.
        if ((stock_round?sees:!empty) && (dist>25 || (sees && (fabsf(eye(bot)-z)>1 || !clear)))) continue;
        int weapon=p.kind==I_AMMO?bot.state.primary:GUN_PISTOL;
        int percent=100*bot.state.ammo[weapon]/ammostats[weapon].max;
        if(p.kind==I_HEALTH) percent=100*bot.state.health/powerupstats[0].max;
        if(p.kind==I_HELMET || p.kind==I_ARMOUR) percent=100*bot.state.armour/powerupstats[I_ARMOUR-I_HEALTH].max;
        int need=p.kind==I_AKIMBO || p.kind==I_GRENADE?75:(100-percent)/2;
        scores[i]=need+(100-(int)fminf(dist,100))/2+(clear?30:15);
    }
    for (;;) {
        int best=-1;
        for (int i=0;i<item_count;++i)
            if (scores[i]>0 && (best<0 || scores[i]>scores[best])) best=i;
        if (best<0) return -1;
        const ItemLocation &p=locations[best];
        route_to_pickup(p.x,p.y,ground(p.x,p.y)+p.height_tenths*.1f);
        float tx,ty;
        if (route_next(bot.x,bot.y,&tx,&ty)) return best;
        scores[best]=0;
    }
}
bool reachable_trail(MatchPoint p) {
    float dx=p.x-bot.x,dy=p.y-bot.y,length=hypotf(dx,dy);
    if(length<3 || length>250) return false;
    float side_x=-dy/length,side_y=dx/length;
    for(int side=-1;side<=1;++side)
        if(!visible(bot.x+side*side_x,bot.y+side*side_y,eye(bot),p.x,p.y,p.z)) return false;
    float previous=body_floor(bot.x,bot.y);
    for(float t=2;t<length+2;t+=2) {
        float k=fminf(t/length,1.f);
        float floor=body_floor(bot.x+dx*k,bot.y+dy*k);
        if(floor-previous>1.5f) return false;
        previous=floor;
    }
    return true;
}
bool hunt_player(bool sees) {
    if(sees || player.respawn_at) {hunting=false;return false;}
    if(hunting && distance(bot.x,bot.y,hunt_spot.x,hunt_spot.y)<3) hunting=false;
    if(clock_ms<hunt_after) return hunting;
    hunting=false;
    // CheckHunt prefers a reachable point on the opponent's trail. Failing
    // that, return to a reachable point on our own trail overlooking them.
    for(int owner=0;owner<2 && !hunting;++owner) {
        const MovementHistory &trail=owner?bot_trail:player_trail;
        float best=1e9f;
        for(int i=0;i<trail.size;++i) {
            const MatchPoint &p=trail.samples[i];
            const Fighter &reference=owner?bot:player;
            float dx=p.x-reference.x,dy=p.y-reference.y,dz=p.z-eye(reference);
            float d=dx*dx+dy*dy+dz*dz;
            if(d>=best || (owner && !visible(player.x,player.y,eye(player),p.x,p.y,p.z))) continue;
            if(reachable_trail(p)) {best=d;hunt_spot=p;hunting=true;}
        }
    }
    hunt_after=clock_ms+(hunting?1250:1500);
    return hunting;
}
bool stock_nav_target(bool pursuing,float &tx,float &ty) {
    if(pursuing) {
        tx=hunt_spot.x;ty=hunt_spot.y;bot_route.reset();
        return true;
    }
    bool collecting=supply_index>=0,steering=false;
    MatchPoint target;
    if(!collecting && bot_route.fallback && clock_ms>=patrol_retry) {
        bot_route.reset();patrol_retry=clock_ms+1500;
    }
    if(collecting && bot_route.fallback && progress_item==supply_index &&
       clock_ms<recover_until && distance(bot.x,bot.y,recover_x,recover_y)>.15f) {
        tx=recover_x;ty=recover_y;steering=true;
    } else if(bot_route.target({bot.x,bot.y,eye(bot)},supply_index,clock_ms,random_number,target)) {
        tx=target.x;ty=target.y;steering=true;bot_jump_request|=bot_route.jump;
    } else if(collecting) {
        const ItemLocation &item=locations[supply_index];
        route_to_pickup(item.x,item.y,ground(item.x,item.y)+item.height_tenths*.1f);
        steering=route_next(bot.x,bot.y,&tx,&ty);
        if(!check_progress || clock_ms>=check_progress || progress_item!=supply_index) {
            if(check_progress && progress_item==supply_index &&
               distance(bot.x,bot.y,progress_x,progress_y)<.5f) {
                if(!recover_until && route_recovery(bot.x,bot.y,&recover_x,&recover_y))
                    recover_until=clock_ms+700;
                else {
                    skip_supply_until[supply_index]=clock_ms+5000;
                    supply_index=-1;bot_route.reset();
                    next_supply_check=clock_ms+1000+random_number()%1001;
                }
            } else recover_until=0;
            progress_x=bot.x;progress_y=bot.y;progress_item=supply_index;
            check_progress=clock_ms+1000;
        }
    }
    return steering;
}
void update_bot(float dt) {
    bot_pose = 0;
    if(bot_primed_at && clock_ms-bot_primed_at>=(unsigned)attackdelay(GUN_GRENADE)) {
        bool drop=bot.respawn_at || clock_ms-bot_primed_at>=2000;
        if(drop || bot.state.gunselect!=GUN_GRENADE || !bot.state.mag[GUN_GRENADE]) {
            launch_grenade(bot,bot_primed_at,drop);
            bot_primed_at=0;bot_sound=GRENADE_THROW;
            bot.ready_at=clock_ms+325;
        }
    }
    if (bot.respawn_at || (!stock_round && player.respawn_at) || clock_ms < 1000) {
        advance_bot_velocity(0,0,dt);
        return;
    }
    int gun=bot.state.gunselect;
    if (bot.state.akimbo && clock_ms>=bot_akimbo_until) {
        bot.state.akimbo=false;actor_sounds.add(AKIMBO_END);
        if(gun==GUN_AKIMBO) bot.state.gunselect=bot.state.primary;
    }
    if (!stock_round && gun==GUN_PISTOL && has_rounds(bot.state.primary) && clock_ms>=bot.ready_at) {
        bot.state.gunselect=bot.state.primary;
        bot.shots=0; combat_move_until=0;
    } else if (!stock_round && gun!=GUN_PISTOL && !has_rounds(gun)) {
        bot.state.gunselect=GUN_PISTOL;
        combat_move_until=0;
    }
    float dx = player.x-bot.x, dy = player.y-bot.y;
    float d = fmaxf(.01f,hypotf(dx,dy));
    // Windows FindEnemy retains a visible opponent outside the acquisition cone.
    bool sees=false;
    if (!player.respawn_at && (bot_saw_player || clock_ms>=next_enemy_search)) {
        float dz=eye(player)-eye(bot);
        const BotSkill &skill=skills[stock_round];
        bool in_view=dx*cosf(bot.yaw)+dy*sinf(bot.yaw)>=d*skill.view_cosine;
        if (bot_saw_player || in_view || dx*dx+dy*dy+dz*dz<skill.detect_radius*skill.detect_radius)
            sees=visible(bot.x,bot.y,eye(bot),player.x,player.y,eye(player)) &&
                 prop_depth({bot.x,bot.y,eye(bot)},{dx,dy,dz},1)>=1;
        if (!sees) next_enemy_search=clock_ms+skill.search_min+random_number()%(skill.search_max-skill.search_min+1);
    }
    if(stock_round && sees && clock_ms>=bot_switch_after && clock_ms>=bot.reload_at) {
        int wanted=preferred_bot_weapon(hypotf(d,eye(player)-eye(bot)));
        if(wanted!=bot.state.gunselect) {
            bot.state.gunselect=wanted;bot.shots=0;
            combat_move_until=0;bot_scope_since=0;
            bot.ready_at=clock_ms+switch_ms;bot_switch_after=clock_ms+1000;
        }
    }
    const CombatRange &range=bot_ranges[bot.state.gunselect];
    bool pursuing=false;
    if(stock_round) {
        if(!player.respawn_at) player_trail.record({player.x,player.y,eye(player)},clock_ms);
        bot_trail.record({bot.x,bot.y,eye(bot)},clock_ms);
        pursuing=hunt_player(sees);
        if(supply_index>=0) {
            const ItemLocation &item=locations[supply_index];
            if(unavailable_until[supply_index]>clock_ms || !bot.state.canpickup(item.kind) || pursuing ||
               (sees && (distance(bot.x,bot.y,item.x,item.y)>25 ||
                         !visible(bot.x,bot.y,eye(bot),item.x,item.y,ground(item.x,item.y)+PLAYER_EYE_HEIGHT))))
                supply_index=-1;
        }
        if(!sees && !pursuing && supply_index<0 && clock_ms>=next_supply_check) {
            supply_index=find_ammunition(false,!has_rounds(bot.state.gunselect));
            next_supply_check=clock_ms+2500+random_number()%2501;
            bot_route.reset();
        }
    } else if (clock_ms>=next_supply_check || (supply_index>=0 && unavailable_until[supply_index]>clock_ms)) {
        int previous=supply_index;
        supply_index=find_ammunition(sees,!has_rounds(bot.state.gunselect));
        if (previous!=supply_index || supply_index<0) next_route=0;
        next_supply_check=clock_ms+450;
    }
    bool collecting=supply_index>=0;
    int equipped=bot.state.gunselect;
    if (!sees && !bot.state.mag[equipped] && bot.state.ammo[equipped] && clock_ms>=bot.ready_at) {
        bot_sound=trigger(bot,false,true);
        bot.shots=0;
    }
    float old_x=bot.x,old_y=bot.y;
    bool retreat=false;
    if (sees && !collecting) {
        if (!bot_saw_player || clock_ms>=combat_move_until) choose_combat_move(d,range);
        float c=cosf(bot.yaw),s=sinf(bot.yaw);
        retreat=combat_forward<0;
        if (!stock_round && dx*c+dy*s<0) advance_bot_velocity(0,0,dt);
        else advance_bot_velocity(c*combat_forward-s*combat_side,s*combat_forward+c*combat_side,dt);
    } else {
        advance_bot_velocity(0,0,dt);
        float tx=0,ty=0;
        bool steering=false;
        if(stock_round) {
            steering=stock_nav_target(pursuing,tx,ty);
        } else {
            if (!collecting && clock_ms >= next_route) {
                route_to((int)player.x,(int)player.y);
                next_route = clock_ms+450;
            }
            steering=route_next(bot.x,bot.y,&tx,&ty);
        }
        if (steering) {
            float mx=tx-bot.x,my=ty-bot.y,length=hypotf(mx,my);
            if (length>.001f) {
                if (!sees) turn_bot_toward(atan2f(my,mx),dt);
                float step=fminf(length,(stock_round?16:11)*dt);
                move_bot(mx/length*step,my/length*step);
                if(!stock_round) bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
            }
        }
    }
    float traveled=distance(old_x,old_y,bot.x,bot.y);
    if (traveled>.001f && dt>0 && (collecting || !sees || combat_forward || combat_side)) {
        float advance=traveled*(retreat?-1.5f:1.5f);
        stride_rate=advance/(dt*1000);
        stride_phase=fmodf(stride_phase+advance,6.f);
        if (stride_phase<0) stride_phase+=6;
        bot_pose=1+(int)stride_phase;
    }
    if (!sees) { bot_saw_player = false; combat_jump_pending=false; bot.shots=0; return; }
    if (!bot_saw_player) {
        const BotSkill &skill=skills[stock_round];
        bot_attack_at=clock_ms+skill.attack_min+random_number()%(skill.attack_max-skill.attack_min+1);
        bot_saw_player = true;
    }
    d=fmaxf(.01f,distance(player.x,player.y,bot.x,bot.y));
    float dz=eye(player)-eye(bot),fire_distance=hypotf(d,dz);
    float scale=fmaxf(.1f,fminf(1,fire_distance/60));
    float xy_error=stock_round?3:20,z_error=stock_round?6:35;
    float ax=player.x-bot.x+aim_random(-xy_error,xy_error)*scale;
    float ay=player.y-bot.y+aim_random(-xy_error,xy_error)*scale;
    float az=dz+aim_random(-z_error,z_error)*scale;
    if(stock_round && bot.state.gunselect==GUN_SNIPER) {
        MatchView target={};
        target.tx=player.x;target.ty=player.y;target.bot_yaw=player.yaw;
        target.bot_floor=feet_z;target.bot_tilt=player.pitch*.25f;
        MatchPoint head=bot_model_point(&target,head_poses[was_crouching?135:0]);
        ax+=head.x-player.x;ay+=head.y-player.y;az+=head.z-eye(player);
        if(d>15 && !bot_scope_since) bot_scope_since=clock_ms;
        else if(d<=15 && bot_scope_since && clock_ms-bot_scope_since<1000) bot_scope_since=0;
    } else bot_scope_since=0;
    if(stock_round && player.velocity_x*player.velocity_x+player.velocity_y*player.velocity_y+
       net_vertical_velocity*net_vertical_velocity>1) {
        float predict=aim_random(.4f,.6f);
        ax+=player.velocity_x*predict;ay+=player.velocity_y*predict;az+=net_vertical_velocity*predict;
    }
    float target_yaw=atan2f(ay,ax),target_pitch=atan2f(az,sqrtf(ax*ax+ay*ay+az*az));
    bot.yaw=bot_aim_step(bot.yaw,target_yaw,false,dt);
    bot.pitch=bot_aim_step(bot.pitch,target_pitch,true,dt);
    bot.pitch=fmaxf(-max_pitch,fminf(max_pitch,bot.pitch+recoil_degrees(bot,false)*radians));
    float min_fire=bot.state.gunselect==GUN_SHOTGUN || bot.state.gunselect==GUN_KNIFE?0:3;
    if (clock_ms<bot_attack_at || clock_ms<bot.ready_at || fire_distance<min_fire || fire_distance>range.max_fire) return;
    if(bot.state.gunselect==GUN_GRENADE) {
        if(!bot_primed_at && bot.state.mag[GUN_GRENADE]) {
            --bot.state.mag[GUN_GRENADE];bot_primed_at=clock_ms;
            bot.ready_at=clock_ms+attackdelay(GUN_GRENADE);
        }
        return;
    }
    if ((bot.state.gunselect==GUN_CARBINE || bot.state.gunselect==GUN_SNIPER) &&
        (fabsf(remainderf(bot.yaw-target_yaw,2*3.14159265f))>5*radians ||
         fabsf(bot.pitch-target_pitch)>15*radians)) return;
    if(bot.state.gunselect==GUN_KNIFE) {
        bot_sound=KNIFE_SWING;last_bot_gun=GUN_KNIFE;
        bot.shot_until=bot.ready_at=clock_ms+attackdelay(GUN_KNIFE);
        bot_fire_ray(bot.yaw,bot.pitch,GUN_KNIFE);
        return;
    }
    MatchSound shot=trigger(bot,true,false);
    if (shot==feedback[view_weapon(bot.state.gunselect)].shot) {
        int fired=bot.state.gunselect;
        last_bot_gun=fired;
        if (stock_round || fired==GUN_ASSAULT) bot.shot_until=bot.ready_at;
        start_recoil(bot,fired);
        float x=cosf(bot.pitch)*cosf(bot.yaw),y=cosf(bot.pitch)*sinf(bot.yaw),z=sinf(bot.pitch);
        firing_impulse(bot,fired,x,y,z,false);
        if(fired!=GUN_SHOTGUN) apply_spread(bot,fired,x,y,z);
        bot_fire_ray(atan2f(y,x),atan2f(z,hypotf(x,y)),fired);
    } else if (shot==feedback[view_weapon(bot.state.gunselect)].reload) bot.shots=0;
    bot_sound = shot;
}
}

void match_prepare(void) { stock_round=false;player.state.nextprimary=GUN_ASSAULT; navigation_prepare(); begin(READY); }
void match_lan_map(unsigned seed) {
    spawn_cycle=-1; initial_spawn_cycles=2; spawn_seed=seed;
    lan_match=true;begin(PLAYING);lan_end_ms=0;
    hide_bot=true; lan_pain_sound=lan_actor_voice=SILENT;
    match_lan_reset_items();
}
void match_lan_spawn(int hp,int armour,int primary,int gun,const int *ammo,const int *mag) {
    if (!lan_match) match_lan_map(0);
    request_respawn=false;
    player.state.nextprimary=primary;
    unsigned visits=4;
    if(initial_spawn_cycles) --initial_spawn_cycles;
    else {
        spawn_seed=1664525u*spawn_seed+1013904223u;
        visits=1+(spawn_seed>>8)%10;
    }
    spawn(player,next_ffa_start(visits,hide_bot?nullptr:&bot),0);
    player.state.gunselect=gun;
    next_gun=primary;switch_end=clock_ms+switch_ms/2;
    memcpy(player.state.ammo,ammo,sizeof(player.state.ammo));
    memcpy(player.state.mag,mag,sizeof(player.state.mag));
    player.state.health=hp; player.state.armour=armour;
}
int match_lan_take_respawn(void) {
    bool request=request_respawn;request_respawn=false;return request;
}
int match_lan_selected_weapon(void) { return switch_end ? -1 : player.state.gunselect; }
unsigned match_lan_shot_time(unsigned now) { return now-player_shot_delay_ms; }
void match_lan_remote_weapon(int gun) {
    if(gun>=0 && gun<NUMGUNS) bot.state.gunselect=gun;
}
void match_lan_remote_spawn(int gun) {
    match_lan_remote_weapon(gun);
    bot.respawn_at=bot.pain_until=bot.flash_until=bot.light_until=0;
    current_clip={}; previous_clip={}; bot_pose=0; hide_bot=true;
    remote_air_ms=0; remote_air_crouch=false; remote_stance=PLAYER_EYE_HEIGHT;
}
void match_lan_remote_reload(int gun) {
    if(lan_match && !hide_bot) lan_shot_sound=feedback[view_weapon(gun)].reload;
}
void match_lan_remote(const LanPose *pose) {
    // clientgame.cpp uses a 75 ms linear correction for position and angles.
    // A spawn or an eight-cube discontinuity starts at the new position.
    correction={bot.x-pose->feet.x,bot.y-pose->feet.y,eye(bot)-remote_stance-pose->feet.z};
    float d2=correction.x*correction.x+correction.y*correction.y+correction.z*correction.z;
    bool discontinuity=hide_bot || d2>=64 || clock_ms-pose_received>1000;
    if(discontinuity) {
        remote_stance=PLAYER_EYE_HEIGHT*(pose->crouching?.75f:1.f);
        remote_air_ms=0;
        bot.x=pose->feet.x;bot.y=pose->feet.y;bot.eye_height=pose->feet.z+remote_stance;
        bot.yaw=pose->yaw;bot.pitch=pose->pitch;
        correction={};
    }
    yaw_correction=remainderf(bot.yaw-pose->yaw,360*radians);
    pitch_correction=bot.pitch-pose->pitch;
    if(discontinuity || pose->on_floor || pose->on_ladder) remote_air_crouch=false;
    else if(pose->crouching && !remote_pose.crouching) remote_air_crouch=true;
    if(pose->on_floor || pose->on_ladder) remote_air_ms=0;
    remote_pose=*pose; pose_received=clock_ms;
    bot_pose=pose->move || pose->strafe; hide_bot=false;
}
static void predict_remote(float dt) {
    if(hide_bot || bot.respawn_at) return;
    unsigned age=clock_ms-pose_received;
    if(age>1000) return;
    LanPose &p=remote_pose;
    float c=cosf(p.yaw),s=sinf(p.yaw);
    bool water=p.feet.z+remote_stance<WATER_LEVEL+.5f;
    float forward=p.on_ladder && !p.on_floor && p.move<0?0.f:(float)p.move;
    if(water) forward*=cosf(p.pitch);
    float tx=forward*c+p.strafe*s,ty=forward*s-p.strafe*c;
    float friction=water?20.f:p.on_floor?6.f:p.on_ladder?1.5f:30.f;
    float retain=1-.005f/(friction*.02f);
    float fraction=1-powf(retain,dt/.005f);
    float carry=(friction*.02f-.005f)*fraction;
    float speed=(p.crouching && !remote_air_crouch?6.4f:16.f)*(water?.5f:1.f);
    float vx=p.velocity.x-tx,vy=p.velocity.y-ty;
    float dx=(tx*dt+vx*carry)*speed,dy=(ty*dt+vy*carry)*speed;
    p.velocity.x-=vx*fraction; p.velocity.y-=vy*fraction;
    if(dx || dy) walk_body(&p.feet.x,&p.feet.y,dx,dy,p.feet.z,
                          PLAYER_EYE_HEIGHT*(p.crouching?.75f:1.f)+PLAYER_ABOVE_EYE,p.on_floor || p.on_ladder?1.5f:0);
    float k=fmaxf(0,1-age/75.f);
    bot.x=p.feet.x+correction.x*k;bot.y=p.feet.y+correction.y*k;
    float wanted=PLAYER_EYE_HEIGHT*(p.crouching?.75f:1.f);
    float step=.6f*16*dt*(water?.5f:1.f);
    remote_stance+=fminf(step,fmaxf(-step,wanted-remote_stance));
    if(dx || dy || !p.on_floor || p.on_ladder || water) {
        float floor,ceiling;
        body_space(p.feet.x,p.feet.y,p.feet.z,&floor,&ceiling);
        if(p.on_floor && p.feet.z>floor+.1f) p.on_floor=0;
        for(unsigned i=0,steps=physics_steps();i<steps && i<20;++i) {
            if(p.on_ladder) {
                p.feet.z+=p.velocity.z*speed*.005f;
                remote_air_ms=0;
            } else if(water) {
                p.velocity.z+=(p.move*sinf(p.pitch)-p.velocity.z)*.0125f;
                p.feet.z+=(p.velocity.z*speed-2.5f)*.005f;
                remote_air_ms=0;
            } else if(!p.on_floor) {
                remote_air_ms+=5;
                p.velocity.z*=119.f/120;
                p.feet.z+=(p.velocity.z*speed-9.5f-remote_air_ms/30.f)*.005f;
            }
            if(p.feet.z+remote_stance+PLAYER_ABOVE_EYE>ceiling) {
                p.feet.z=ceiling-remote_stance-PLAYER_ABOVE_EYE;
                p.velocity.z=fminf(0,p.velocity.z);
            }
            if(p.feet.z<=floor) {
                p.feet.z=floor;p.on_floor=1;remote_air_ms=0;remote_air_crouch=false;
                if(!p.on_ladder) p.velocity.z=0;
            }
        }
    }
    bot.eye_height=p.feet.z+remote_stance+correction.z*k;
    bot.yaw=p.yaw+yaw_correction*k;bot.pitch=p.pitch+pitch_correction*k;
}
void match_lan_hide(void) { hide_bot=true; }
void match_lan_remote_death(int gib) {
    if(hide_bot) return;
    bot_pose=0;
    bot.respawn_at=clock_ms+bot_respawn_ms;
    bot.flash_until=bot.light_until=0;
    if(gib) { spawn_gibs(bot); lan_gib_sound=true; }

}
void match_lan_shot(int gun,float x,float y,float z) {
    if(!lan_match || hide_bot) return;
    if(gun==GUN_GRENADE) {bot.state.gunselect=gun;lan_shot_sound=GRENADE_PULL;return;}
    bot.state.gunselect=gun;
    bot.flash_until=clock_ms+90;
    bot.light_until=gun==GUN_KNIFE?0:clock_ms+100;
    lan_shot_sound=feedback[view_weapon(gun)].shot;
    if(gun==GUN_KNIFE) return;
    // SV_SHOTFX supplies the quantized endpoint. Windows starts remote
    // effects at the shooter's eye and compares the endpoint to the map hit.
    MatchPoint from={bot.x,bot.y,eye(bot)},to={x,y,z};
    MatchPoint d={x-from.x,y-from.y,z-from.z};
    float length=hypotf(hypotf(d.x,d.y),d.z);
    if(length*length>.07f) {
        float scale=(length+1)/length;
        SurfaceHit hit;
        if(trace_surface(from.x,from.y,from.z,from.x+d.x*scale,from.y+d.y*scale,from.z+d.z*scale,&hit)
           && fabsf((length+1)*hit.fraction-length)<=1)
            incoming_mark(hit,lan_impact);
        if(gun!=GUN_SHOTGUN) incoming_line(from,to,lan_air);
    }
    if(gun==GUN_SHOTGUN) {
        if(length>.001f) shotgun_pattern(from,{d.x/length,d.y/length,d.z/length},length,nullptr);
    } else spark_splash(x,y,z);
}
int match_lan_take_jump(void) {
    bool jumped=lan_jump; lan_jump=false;
    return jumped;
}
int match_lan_grenade_events(const GrenadeEvent **events) {
    *events=grenade_events;int count=event_count;event_count=0;return count;
}
void match_lan_throw(MatchPoint from,MatchPoint velocity,int cooked) {
    if(!lan_match || cooked<=0 ||
       from.x<0 || from.x>=MAP || from.y<0 || from.y>=MAP || fabsf(from.z)>256 ||
       fabsf(velocity.x)>2 || fabsf(velocity.y)>2 || fabsf(velocity.z)>2) return;
    throw_grenade(from,velocity,cooked>2000?2000:cooked);
    lan_shot_sound=GRENADE_THROW;
}
void match_lan_hitpush(int gun,int damage,float x,float y,float z) {
    if(lan_match && !player.respawn_at && damage>0 && gun>=0 && gun<NUMGUNS)
        hit_impulse(player,gun,damage,x,y,z);
}
void match_lan_health(int hp,int armour) {
    if(hp<=0 && !player.respawn_at) {
        place_death_camera();
        player.respawn_at=clock_ms+player_respawn_ms;
    }
    player.state.health=hp;player.state.armour=armour;
}
void match_lan_damage(int local_victim,int from_remote,int amount,int hp,int armour) {
    if(local_victim) {
        match_lan_health(hp,armour);
        player.pain_until=clock_ms+300;
        player_damage_feedback(amount,from_remote && !hide_bot);
        if(hp>0) lan_pain_sound=PLAYER_PAIN;
    } else if(!hide_bot) {
        bot.pain_until=clock_ms+300;
        actor_blood(bot,amount);
        lan_actor_voice=(MatchSound)(BOT_PAIN1+effect_random(5));
    }
}
void match_lan_frag(int local_victim,int suicide,int gun,int gib,const char *opponent) {
    record_frag(local_victim,suicide,gun,gib,opponent);
    if(local_victim && gib) {spawn_gibs(player);lan_gib_sound=true;}
    (local_victim?lan_pain_sound:lan_actor_voice)=effect_random(2)?ALT_DEATH:PLAYER_DEATH;
}
void match_lan_time(unsigned remaining_ms) {
    lan_end_ms=clock_ms+remaining_ms;
    if(!remaining_ms) phase=INTERMISSION;
}
void match_lan_score(int local,int frags) { (local?player:bot).frags=frags; }
void match_lan_reset_items(void) {
    memset(lan_item_visible,0,sizeof(lan_item_visible));
    memset(lan_retry_at,0,sizeof(lan_retry_at));
    lan_pickup_sound=SILENT;
}
int match_lan_item(int entity,int visible,int picked_by_us) {
    for (int i=0;i<item_count;++i) if (locations[i].entity==entity) {
        bool awarded=lan_item_visible[i] && picked_by_us && lan_match && !player.respawn_at;
        if (awarded) {
            pickup(player,locations[i].kind);
            lan_pickup_sound=pickup_effect(locations[i].kind);
        }
        lan_item_visible[i]=visible;
        lan_retry_at[i]=0;
        return awarded && locations[i].kind==I_AKIMBO;
    }
    return 0;
}
int match_lan_touch_item(void) { return lan_match ? collect(player) : -1; }
int match_lan_sound_id(MatchSound sound) {
    for(const NetworkSound &entry:network_effects) if(entry.effect==sound) return entry.id;
    return -1;
}
void match_lan_sound(int id) {
    if(!lan_match || hide_bot || bot.respawn_at || phase!=PLAYING) return;
    for(const NetworkSound &entry:network_effects) if(entry.id==id) {
        network_sounds.add(entry.effect);return;
    }
}
void match_lan_stop(void) { lan_shot_sound=SILENT; if(lan_match) {lan_match=false;begin(READY);} }
void match_update(const MatchInput *in, unsigned elapsed, float motion_seconds) {
    actor_sounds=network_sounds;network_sounds.count=0;
    gib_sound=lan_gib_sound; lan_gib_sound=false;
    if (gib_count) update_gibs(elapsed);
    if (phase!=INTERMISSION) idle_ms=(idle_ms+elapsed)%4000;
    bot_sound=lan_shot_sound; lan_shot_sound=SILENT;
    movement_sound=SILENT; pain_sound=lan_pain_sound; bot_voice=lan_actor_voice;
    lan_pain_sound=lan_actor_voice=SILENT;
    pickup_sound=lan_match?lan_pickup_sound:SILENT;lan_pickup_sound=SILENT;
    step_sound=stance_sound=SILENT;
    air_sound=lan_air;impact_sound=lan_impact;lan_air=lan_impact={};
    if (phase != PLAYING) {
        if (phase==INTERMISSION && intermission_age<frag_message_ms) intermission_age+=elapsed;
        if(lan_match) return;
        menu_time+=elapsed;
        if(in->refill) stock_round=!stock_round;
        if (in->assist_choice) classic_assist=in->assist_choice<0;
        if (in->swap) {
            switch (player.state.nextprimary) {
            case GUN_ASSAULT: player.state.nextprimary=GUN_SUBGUN; break;
            case GUN_SUBGUN: player.state.nextprimary=GUN_CARBINE; break;
            case GUN_CARBINE: player.state.nextprimary=GUN_SNIPER; break;
            case GUN_SNIPER: player.state.nextprimary=GUN_SHOTGUN; break;
            default: player.state.nextprimary=GUN_ASSAULT; break;
            }
            if (phase==READY) {
                player.state.respawn();
                player.state.spawnstate(GMODE_BOTDEATHMATCH);
            }
        }
        if (in->start) begin(PLAYING);
        return;
    }
    frame_start_ms=clock_ms;
    clock_ms += elapsed;
    if(player.state.akimbo && (clock_ms>=akimbo_expires || player.respawn_at)) {
        player.state.akimbo=false;draw_akimbo=false;
        if(previous_firearm==GUN_AKIMBO) previous_firearm=GUN_PISTOL;
        if(player.state.gunselect==GUN_AKIMBO || (switch_end && next_gun==GUN_AKIMBO)) {
            int primary=player.state.primary;
            next_gun=player.state.ammo[primary] && !player.respawn_at ? primary :
                     player.state.mag[GUN_GRENADE] && !player.respawn_at ? GUN_GRENADE :
                     player.state.ammo[GUN_PISTOL] ? GUN_PISTOL : GUN_KNIFE;
            switch_end=clock_ms+switch_ms;
        }
        if(!player.respawn_at) pickup_sound=AKIMBO_END;
    }
    update_grenades(frame_start_ms);
    while (mark_count && marks[first_mark].expires<=clock_ms) {
        first_mark=(first_mark+1)%MARK_CAPACITY;
        --mark_count;
    }
    update_blood(elapsed);
    if (!lan_match && clock_ms >= (stock_round?bot_match_duration:duration)) { phase = INTERMISSION; return; }
    if(player.respawn_at && clock_ms>player.respawn_at && (respawn_trigger && !in->fire)) {
        if(lan_match) request_respawn=true;
        else respawn_near(player,bot);
    } else if(!lan_match && !stock_round && player.respawn_at && player.respawn_at<=clock_ms)
        respawn_near(player,bot);
    respawn_trigger=in->fire;
    if (bot.respawn_at && bot.respawn_at <= clock_ms) {
        if(lan_match) hide_bot=true;
        else respawn_near(bot,player);
    }
    float dt = fminf(.10f,motion_seconds);
    float old_dx=bot.x-player.x, old_dy=bot.y-player.y;
    if(lan_match) predict_remote(dt);
    float manual_yaw=0;
    MatchInput neutral={};
    if (player.respawn_at) {
        death_yaw=fmodf(death_yaw+in->turn*150*radians*dt,6.2831853f);
        death_pitch=fmaxf(-max_pitch,fminf(max_pitch,death_pitch+in->look*105*radians*dt));
        // Keep knockback, drag and gravity on the body while stick input controls the camera.
        neutral.crouch=was_crouching;
        in=&neutral;
    }
    {
        bool want_scope=in->scope && player.state.gunselect==GUN_SNIPER && !switch_end &&
                        !in->swap && !in->knife_select && !player.respawn_at && player.reload_at<=clock_ms;
        if (want_scope && !scoped) scope_since=clock_ms;
        scoped=want_scope;
        float aim_scale=scoped?SCOPE_TAN_HALF_FOV:1.f;
        float turn=fmaxf(-1,fminf(1,in->turn));
        int direction=(turn>0)-(turn<0);
        if (direction!=player.turn_direction || fabsf(turn)<.85f) player.turn_boost=0;
        player.turn_direction=direction;
        if (fabsf(turn)>=.85f) player.turn_boost=fminf(1,player.turn_boost+dt/.4f);
        float outer=fmaxf(0,(fabsf(turn)-.85f)/.15f);
        float yaw_rate=turn*(150+70*outer*player.turn_boost)*radians;
        manual_yaw=yaw_rate*dt*aim_scale;
        player.yaw=fmodf(player.yaw+manual_yaw,6.2831853f);
        float look=fmaxf(-1,fminf(1,in->look));
        float pitch_change=look*105*dt*aim_scale;
        // The PC applies recoil on a 5 ms physics clock. Keep stick input on
        // the display clock so a new aim report affects this rendered frame.
        pitch_change+=recoil_degrees(player,in->crouch);
        player.pitch=fmaxf(-max_pitch,fminf(max_pitch,player.pitch+pitch_change*radians));

        float floor,ceiling;
        body_space(player.x,player.y,feet_z,&floor,&ceiling);
        ActorBounds opponent={bot.x,bot.y,eye(bot)-(lan_match?remote_stance:PLAYER_EYE_HEIGHT),eye(bot)+PLAYER_ABOVE_EYE};
        const ActorBounds *blocker=(bot.respawn_at || hide_bot)?nullptr:&opponent;
        actor_clearance(blocker,player.x,player.y,feet_z,stance+PLAYER_ABOVE_EYE,&floor,&ceiling);
        float ladder_top=0;
        bool climbing=!in->crouch && ladder_space(player.x,player.y,feet_z,&ladder_top);
        on_ladder=climbing;
        float target=fminf(in->crouch?.75f*PLAYER_EYE_HEIGHT:PLAYER_EYE_HEIGHT,ceiling-feet_z-PLAYER_ABOVE_EYE);
        float stance_step=.6f*16*dt*(player.eye_height<WATER_LEVEL?.5f:1.f);
        stance+=fmaxf(-stance_step,fminf(stance_step,target-stance));
        bool swimming=player.eye_height<WATER_LEVEL+.5f;
        if (swimming && !in_water) jump_speed=0;
        if(!in->jump) jump_pending=false;
        else if(!held_jump && !was_crouching) jump_pending=true;
        if(grounded || swimming || climbing) crouched_in_air=false;
        bool air_crouch=!grounded && !swimming && !climbing && in->crouch && !was_crouching && !crouched_in_air;
        if ((grounded || swimming) && !climbing && jump_pending && !in->crouch && stance>=PLAYER_EYE_HEIGHT) {
            grounded=false; jump_speed=swimming?16:32; air_ms=0;
            jump_pending=false;
            float target_speed=hypotf(in->move,in->strafe);
            if(last_jump && clock_ms-last_jump<250 && fabsf(in->strafe)>.001f &&
               feet_z-last_jump_floor>.2f && target_speed>.1f) {
                float boost=1.25f/fmaxf(1,hypotf(player.velocity_x,player.velocity_y)/target_speed);
                player.velocity_x*=boost;player.velocity_y*=boost;jump_speed*=boost;
            }
            last_jump=clock_ms;last_jump_floor=feet_z;
            jump_start_ticks=swimming?0:2;
            lan_jump=true;
            if (swimming) { player.velocity_x/=8; player.velocity_y/=8; }
            else movement_sound=PLAYER_JUMP;
        }
        held_jump=in->jump;
        if(air_crouch) {
            // Stock adds 0.3 to normalized vertical velocity once per flight.
            jump_speed+=.3f*16;
            crouched_in_air=true;
        }
        float speed=stance<PLAYER_EYE_HEIGHT && !crouched_in_air?6.4f:16.f;
        if (swimming) speed*=.5f;
        float c=cosf(player.yaw), s=sinf(player.yaw);
        float forward=climbing && !grounded && in->move<0 ? 0 : in->move;
        if (swimming) forward*=cosf(player.pitch);
        float target_x=forward*c-in->strafe*s;
        float target_y=forward*s+in->strafe*c;
        // Sum the PC's 5 ms friction steps, including distance travelled
        // during acceleration, over the current display interval.
        float friction=swimming?20.f:grounded?6.f:climbing?1.5f:30.f;
        float retain=1-.005f/(friction*.02f);
        float fraction=1-powf(retain,dt/.005f);
        float carry=(friction*.02f-.005f)*fraction;
        float dx=player.velocity_x-target_x,dy=player.velocity_y-target_y;
        float travel_x=(target_x*dt+dx*carry)*speed;
        float travel_y=(target_y*dt+dy*carry)*speed;
        player.velocity_x-=dx*fraction;
        player.velocity_y-=dy*fraction;
        walk_past_actor(&player.x,&player.y,travel_x,travel_y,
                  feet_z,stance+PLAYER_ABOVE_EYE,grounded||climbing?1.5f:0.f,blocker);
        if (was_crouching!=bool(in->crouch)) {
            was_crouching=in->crouch;
            stance_sound=was_crouching?CROUCH_IN:CROUCH_OUT;
        }
        if ((grounded || air_ms<=50) && !swimming && (in->move || in->strafe)) {
            bool wading=feet_z+.25f<WATER_LEVEL;
            if (!wading || WATER_LEVEL-map_cube((int)player.x,(int)player.y)->floor<=stance)
                step_sound=in->crouch?(wading?WATER_CROUCH_STEPS:CROUCH_STEPS):(wading?WATER_STEPS:FOOTSTEPS);
        }
        body_space(player.x,player.y,feet_z,&floor,&ceiling);
        actor_clearance(blocker,player.x,player.y,feet_z,stance+PLAYER_ABOVE_EYE,&floor,&ceiling);
        if (feet_z>floor+.01f) grounded=false;
        if (climbing) {
            feet_z=fminf(fminf(ladder_top,ceiling-stance-PLAYER_ABOVE_EYE),feet_z+(swimming?8:16)*in->move*dt);
            air_ms=jump_start_ticks=0; jump_speed=0;
            grounded=feet_z<=floor;
        } else if (swimming) {
            float desired=8*in->move*sinf(player.pitch);
            jump_speed+=(desired-jump_speed)*fraction;
            feet_z=fminf(ceiling-stance-PLAYER_ABOVE_EYE,feet_z+(jump_speed-2.5f)*dt);
            grounded=feet_z<=floor;
            air_ms=jump_start_ticks=0;
        } else if (!grounded) {
            // PC movement computes displacement before assigning the jump impulse.
            // The first moving tick still uses floor friction, then air friction.
            unsigned steps=physics_steps();
            for (unsigned i=0;i<steps && i<20;++i) {
                if (jump_start_ticks==2) { --jump_start_ticks; continue; }
                if (jump_start_ticks) { jump_start_ticks=0; jump_speed*=23.f/24; }
                else { air_ms+=5; jump_speed*=119.f/120; }
                feet_z+=(jump_speed-9.5f-air_ms/30.f)*.005f;
                if (feet_z+stance+PLAYER_ABOVE_EYE>=ceiling) { feet_z=ceiling-stance-PLAYER_ABOVE_EYE; jump_speed=0; }
                if (feet_z<=floor+.01f) { feet_z=floor; break; }
            }
        }
        if (feet_z<=floor && !jump_start_ticks) {
            if (air_ms>200 && !player.respawn_at) movement_sound=air_ms>800?PLAYER_LAND:SOFT_LAND;
            feet_z=floor; grounded=true; jump_speed=0; air_ms=0;
        }
        float delta=feet_z+stance-player.eye_height;
        player.eye_height+=grounded?fmaxf(-12*dt,fminf(12*dt,delta)):delta;
        player.eye_height=fmaxf(floor+.1f,fminf(ceiling-.1f,player.eye_height));
        if (swimming!=in_water) {
            if (!swimming) movement_sound=WATER_EXIT;
            else if (!last_splash || clock_ms-last_splash>500) {
                movement_sound=WATER_ENTER;
                last_splash=clock_ms;
            }
            in_water=swimming;
        }
        if (grounded || climbing || swimming) sway_ms+=elapsed;
        // hudgun.h follows the physics velocity before crouch and water scale travel.
        float gain=1-powf(.7f,100*dt);
        float vertical=climbing?in->move:jump_speed/(swimming?8.f:16.f);
        net_vertical_velocity=vertical;
        player.sway_x+=gain*(player.velocity_x-player.sway_x);
        player.sway_y+=gain*(player.velocity_y-player.sway_y);
        sway_vertical+=gain*(vertical-sway_vertical);
        if (switch_end) {
            if (clock_ms+switch_ms/2>switch_end) player.state.gunselect=next_gun;
            if (clock_ms>switch_end) switch_end=0;
        }
        if ((draw_akimbo || in->swap || in->knife_select || (in->grenade_select && player.state.mag[GUN_GRENADE])) &&
            grenade_state==GRENADE_IDLE && !switch_end && !player.respawn_at && player.reload_at<=clock_ms) {
            int current=player.state.gunselect;
            if(draw_akimbo) {
                next_gun=GUN_AKIMBO;draw_akimbo=false;
            } else if(in->grenade_select && current!=GUN_GRENADE) {
                if(current!=GUN_KNIFE) previous_firearm=current;
                next_gun=GUN_GRENADE;
            } else if (current==GUN_KNIFE || current==GUN_GRENADE) next_gun=previous_firearm;
            else if (in->knife_select) { previous_firearm=current; next_gun=GUN_KNIFE; }
            else next_gun=current==GUN_PISTOL || current==GUN_AKIMBO ? player.state.primary :
                          player.state.akimbo ? GUN_AKIMBO : GUN_PISTOL;
            radar_action_until=clock_ms+500;
            release_after_switch=in->fire;
            switch_end=clock_ms+switch_ms;
            player.ready_at=0;
            player.shots=player.shot_until=0;
        }
    }
    if(lan_match) {
        stride_rate=(remote_pose.move<0?-16.f:16.f)/1860;
        if(bot_pose) stride_phase=fmodf(stride_phase+elapsed*stride_rate,6);
        return;
    }
    collect(player); update_bot(dt); update_bot_height(); collect(bot);
    // During fine two-stick tracking, share a fraction of the target's angular
    // motion. An existing aiming error never pulls the crosshair to center.
    if (!player.respawn_at && !bot.respawn_at && fabsf(in->strafe)>.05f &&
        fabsf(in->turn)>.001f && fabsf(in->turn)<.45f && fabsf(in->look)<.45f) {
        float dx=bot.x-player.x, dy=bot.y-player.y;
        float forward=dx*cosf(player.yaw)+dy*sinf(player.yaw);
        if (forward<=1 || forward>55) return;
        float side=dy*cosf(player.yaw)-dx*sinf(player.yaw);
        float chest=eye(bot)-1;
        float offset=hypotf(side,chest-eye(player)-forward*tanf(player.pitch))/forward;
        // Keep the gain steady across the torso. Fade smoothly through the
        // surrounding cone so crossing its inner edge doesn't change turn speed.
        float edge=fmaxf(0,fminf(1,(.14f-offset)/.10f));
        float proximity=classic_assist ? fmaxf(0,1-offset/.14f) : edge*edge*(3-2*edge);
        if (proximity>0 && visible(player.x,player.y,eye(player),bot.x,bot.y,chest)) {
            float bearing_change=atan2f(old_dx*dy-old_dy*dx,old_dx*dx+old_dy*dy);
            float cap=32*radians*dt;
            float strength=(classic_assist ? .65f : .86f)*proximity*fminf(1,(.45f-fabsf(in->turn))/.20f)
                           *fminf(1,fabsf(in->strafe)/.25f);
            player.yaw+=strength*fmaxf(-cap,fminf(cap,bearing_change-manual_yaw));
        }
    }
}
MatchPoint bot_model_point(const MatchView *v,MatchPoint p) {
    float st=sinf(v->bot_tilt),ct=cosf(v->bot_tilt);
    float forward=ct*p.x-st*p.z,c=cosf(v->bot_yaw),s=sinf(v->bot_yaw);
    return {v->tx+c*forward-s*p.y,v->ty+s*forward+c*p.y,
            v->bot_floor+st*p.x+ct*p.z};
}
static BotPose sample_clip(const BotClip &clip,unsigned now) {
    float frame=clip.phase+(now-clip.sample_time)*clip.frames_per_ms;
    bool loop=clip.first==0 || clip.first==40 || clip.first==135 || clip.first==154;
    frame=loop?fmodf(frame,clip.count):fminf(frame,clip.count-1);
    if (loop && frame<0) frame+=clip.count;
    int i=(int)frame;
    int j=loop?(i+1)%clip.count:(i+1<clip.count?i+1:i);
    return {clip.first+i,clip.first+j,frame-i};
}
static MatchPoint head_position(BotPose pose) {
    return mix_point(head_poses[pose.first],head_poses[pose.second],pose.blend);
}
static void bot_view(MatchView *v) {
    v->bot_height=lan_match?remote_stance:PLAYER_EYE_HEIGHT;
    v->bot_yaw = bot.yaw; v->tx = bot.x; v->ty = bot.y; v->bot_floor=eye(bot)-v->bot_height; v->bot_alive = !bot.respawn_at;
    v->bot_weapon=view_weapon(bot.state.gunselect);
    if (!lan_match && clock_ms<bot.shot_until) v->bot_weapon=view_weapon(last_bot_gun);
    v->bot_flash_end_ms = v->bot_weapon!=VIEW_KNIFE && v->bot_weapon!=VIEW_GRENADE && bot.flash_until>=40 ? bot.flash_until-40 : 0;
    // MD2 sequences and frame periods used by the PC renderclient.
    unsigned now=clock_ms+intermission_age;
    bool crouch=lan_match && remote_pose.crouching;
    BotClip clip={crouch?135:0,crouch?19:40,0,now,idle_ms/100.f,.01f};
    v->bot_sink=0;
    if (bot.respawn_at) {
        unsigned age=now+bot_respawn_ms-bot.respawn_at;
        clip={178,6,bot.respawn_at,now,age/100.f,.01f};
        // renderclient lowers the resting model after two seconds.
        float seconds=age>2000?(age-2000)*.001f:0;
        v->bot_sink=.1f*seconds*seconds*seconds;
    } else if (bot.pain_until>clock_ms)
        clip={crouch?169:54,4,bot.pain_until,now,(now+300-bot.pain_until)/75.f,1/75.f};
    else if (bot.flash_until && clock_ms<bot.flash_until+210)
        clip={crouch?160:46,crouch?9:8,bot.flash_until,now,(now+90-bot.flash_until)/37.5f,1/37.5f};
    else if((lan_match && remote_air_ms>50) || (!lan_match && stock_round && bot_air_ms>50))
        clip={crouch?159:68,1,0,now,0,0};
    else if (bot_pose)
        clip={crouch?154:40,6,0,now,stride_phase,stride_rate};

    // vertmodel.h keeps the older outgoing clip when another change occurs
    // within the first half of the 150 ms transition.
    if (!current_clip.count || now-current_clip.sample_time>150) previous_clip={};
    else if (clip.first!=current_clip.first || clip.action!=current_clip.action) {
        if (!previous_clip.count || now-clip_switch>75) previous_clip=current_clip;
        clip_switch=now;
    }
    current_clip=clip;
    v->current_pose=sample_clip(clip,now);
    v->pose_transition=previous_clip.count?fminf(1,(now-clip_switch)/150.f):1;
    v->previous_pose=v->pose_transition<1?sample_clip(previous_clip,now):v->current_pose;
    v->bot_tilt=(bot.respawn_at?.1f*radians:bot.pitch)*.25f;
    MatchPoint head=head_position(v->current_pose);
    if (v->pose_transition<1) head=mix_point(head_position(v->previous_pose),head,v->pose_transition);
    v->bot_head=bot_model_point(v,head);
}
// Stock createrays rotates 21 spokes in each of three independently jittered rings.
static void shotgun_pattern(MatchPoint from,MatchPoint direction,float length,MatchView *target,bool incoming) {
    const int base[]={SGCObase,SGCMbase,SGCCbase},width[]={SGCOrange,SGCMrange,SGCCrange};
    const float horizontal=hypotf(direction.x,direction.y);
    MatchPoint u=horizontal>.0001f?MatchPoint{-direction.y/horizontal,direction.x/horizontal,0}:MatchPoint{1,0,0};
    MatchPoint v={direction.z*u.y,-direction.z*u.x,direction.y*u.x-direction.x*u.y};
    int hits[3]={};
    float victim_distance=target?hypotf(distance(player.x,player.y,bot.x,bot.y),eye(player)-eye(bot)):0;
    for(int ring=0;ring<3;++ring) {
        float phase=effect_random(65536)*(6.2831853f/65536);
        for(int i=0;i<SGRAYS;++i) {
            float angle=phase+i*(6.2831853f/SGRAYS);
            float radius=(base[ring]+(int)effect_random(width[ring]))*.001f;
            float a=radius*cosf(angle),b=radius*sinf(angle);
            MatchPoint d={direction.x+a*u.x+b*v.x,direction.y+a*u.y+b*v.y,direction.z+a*u.z+b*v.z};
            float reach=length;
            if(target && !hide_bot) {
                float xy=hypotf(d.x,d.y);
                target->angle=atan2f(d.y,d.x);target->pitch=atan2f(d.z,xy);
                if(target_contact(target,reach*hypotf(xy,d.z),true)) {
                    ++hits[ring];
                    reach=victim_distance/sqrtf(xy*xy+d.z*d.z);
                }
            }
            MatchPoint end={from.x+reach*d.x,from.y+reach*d.y,from.z+reach*d.z};
            if(!ring) spark_splash(end.x,end.y,end.z);
            if(i%(SGRAYS/3)==0) {
                SurfaceHit hit;
                if(trace_surface(from.x,from.y,from.z,end.x+d.x,end.y+d.y,end.z+d.z,&hit) &&
                    fabsf((reach+1)*hit.fraction-reach)<=1.f) mark_surface(hit);
            }
        }
    }
    if(!target) return;
    int total=hits[0]+hits[1]+hits[2];
    int bonus=total && victim_distance<=SGDMGDISTB/10.f?SGDMGBONUS:0;
    if(total && !lan_match) {
        float weighted=(hits[0]*SGCOdmg+hits[1]*SGCMdmg+hits[2]*SGCCdmg)/10.f;
        int dealt=(int)ceilf(bonus+weighted*(SGDMGTOTAL/(SGRAYS*100.f)));
        damage(incoming?player:bot,incoming?bot:player,GUN_SHOTGUN,direction.x,direction.y,direction.z,false,dealt,total==3*SGRAYS);
    }
    if(incoming) return;
    shotgun_info=(hits[0]<<24)|(hits[1]<<16)|(hits[2]<<8)|bonus;
    shot_contact=total!=0;
    float endpoint=total?victim_distance:length;
    shot_direction=direction;
    shot_end={from.x+endpoint*direction.x,from.y+endpoint*direction.y,from.z+endpoint*direction.z};
}
static float aim_from_weapon(MatchPoint &direction) {
    // Windows aims at the world under the crosshair, from 0.2 units below
    // the eye. Resolve that depth without stalling the GPU for a readback.
    float dx=cosf(player.pitch)*cosf(player.yaw),dy=cosf(player.pitch)*sinf(player.yaw);
    float dz=sinf(player.pitch),eye_z=eye(player);
    SurfaceHit hit;
    trace_surface(player.x,player.y,eye_z,player.x+512*dx,player.y+512*dy,eye_z+512*dz,&hit);
    float depth=prop_depth({player.x,player.y,eye_z},{dx,dy,dz},512*hit.fraction);
    dx*=depth; dy*=depth; dz=dz*depth+.2f;
    float length=fmaxf(.00001f,sqrtf(dx*dx+dy*dy+dz*dz));
    direction={dx/length,dy/length,dz/length};
    return length;
}
MatchSound match_attack(const MatchInput *in) {
    empty_click=false;
    if (!in->fire) semi_trigger_used=release_after_switch=false;
    bool fire = in->fire && !release_after_switch && (guns[player.state.gunselect].isauto || !semi_trigger_used);
    if(phase==PLAYING && player.state.gunselect==GUN_GRENADE && !switch_end)
        return use_grenade(in->fire && !release_after_switch && !draw_akimbo);
    if (switch_end && !player.respawn_at && phase==PLAYING)
        return switch_end==clock_ms+switch_ms ? WEAPON_CHANGE : SILENT;
    // PC gun::attack only observes a trigger release after gunwait expires.
    if (!fire && clock_ms>=player.ready_at) player.shots=0;
    if (player.state.gunselect==GUN_KNIFE) {
        if (phase!=PLAYING || player.respawn_at || !fire || player.ready_at>clock_ms) return SILENT;
        unsigned action=player.ready_at>=frame_start_ms?player.ready_at:clock_ms;
        player_shot_delay_ms=clock_ms-action;
        radar_action_until=action+500;
        player.ready_at=player.shot_until=action+attackdelay(GUN_KNIFE);
        semi_trigger_used=true;
        MatchPoint direction;
        aim_from_weapon(direction);
        float dx=3*direction.x,dy=3*direction.y,dz=3*direction.z,z=eye(player)-.2f;
        SurfaceHit hit;
        trace_surface(player.x,player.y,z,player.x+dx,player.y+dy,z+dz,&hit);
        float reach=prop_depth({player.x,player.y,z},{dx,dy,dz},hit.fraction);
        dx*=reach; dy*=reach; dz*=reach;
        if (player.pitch<0) dz+=2.5f*sinf(player.pitch);
        MatchView ray={};
        ray.px=player.x; ray.py=player.y; ray.eye=z; ray.angle=player.yaw;
        ray.pitch=atan2f(dz,hypotf(dx,dy));
        bot_view(&ray);
        shot_contact=!hide_bot && target_contact(&ray,hypotf(hypotf(dx,dy),dz),true);
        float length=sqrtf(dx*dx+dy*dy+dz*dz);
        shot_direction=length>0?MatchPoint{dx/length,dy/length,dz/length}:direction;
        if (shot_contact) {
            length=hypotf(distance(player.x,player.y,bot.x,bot.y),eye(player)-eye(bot));
            if (!lan_match) damage(bot,player,GUN_KNIFE,dx,dy,dz);
        }
        shot_end={player.x+length*shot_direction.x,player.y+length*shot_direction.y,z+length*shot_direction.z};
        return KNIFE_SWING;
    }
    MatchSound shot=phase == PLAYING ? trigger(player,fire,in->refill) : SILENT;
    if (shot==SNIPER_RELOAD) scoped=false;
    if (shot==feedback[view_weapon(player.state.gunselect)].shot) {
        const int gun=player.state.gunselect;
        if(gun==GUN_AKIMBO) akimbo_left=!akimbo_left;
        const guninfo &g=guns[gun];
        if (!g.isauto) semi_trigger_used=true;
        start_recoil(player,gun);
        player.shot_until=player.ready_at;
        MatchPoint direction;
        float length=aim_from_weapon(direction);
        float dx=direction.x,dy=direction.y,dz=direction.z,z=eye(player)-.2f;
        SurfaceHit hit;
        firing_impulse(player,gun,dx,dy,dz,in->crouch);
        if(gun==GUN_SHOTGUN) {
            MatchView ray={};
            ray.px=player.x;ray.py=player.y;ray.eye=z;
            bot_view(&ray);
            shotgun_pattern({player.x,player.y,z},direction,length,&ray);
            flash_angle=effect_random(360)*radians;
            return shot;
        }
        apply_spread(player,gun,dx,dy,dz);
        MatchView ray={};
        ray.px=player.x; ray.py=player.y; ray.eye=z;
        ray.angle=atan2f(dy,dx); ray.pitch=atan2f(dz,hypotf(dx,dy));
        bot_view(&ray);
        float magnitude=sqrtf(dx*dx+dy*dy+dz*dz);
        shot_direction={dx/magnitude,dy/magnitude,dz/magnitude};
        shot_contact=hide_bot?0:target_contact(&ray,length*magnitude,false);
        if (shot_contact) {
            if (!lan_match) damage(bot,player,gun,dx,dy,dz,gun==GUN_SNIPER && shot_contact==2);
            // PC raydamage shortens the effects endpoint to the struck actor.
            length=hypotf(distance(player.x,player.y,bot.x,bot.y),eye(player)-eye(bot))/magnitude;
        }
        shot_end={player.x+length*dx,player.y+length*dy,z+length*dz};
        spark_splash(shot_end.x,shot_end.y,shot_end.z);
        // Only leave a decal when the struck surface is within one map unit
        // of the endpoint. This also covers an opponent backed against a wall.
        float reach=length+1/magnitude;
        if (length*length*magnitude*magnitude>.07f &&
            trace_surface(player.x,player.y,z,player.x+reach*dx,player.y+reach*dy,z+reach*dz,&hit) &&
            fabsf(reach*hit.fraction-length)*magnitude<=1.f) {
            mark_surface(hit);
        }
        flash_angle=effect_random(360)*radians;
    }
    return shot;
}
void match_view(MatchView *v) {
    unsigned now=clock_ms+intermission_age;
    for (unsigned i=0;i<3;++i) {
        unsigned age=now-frag_messages[i].time;
        v->frag_messages[i]={age<frag_message_ms && frag_messages[i].text[0]?frag_messages[i].text:nullptr,age};
    }
    v->player_light=player.light_until>now?(unsigned)fminf(50,player.light_until-now):0;
    v->bot_light=bot.light_until>now?(unsigned)fminf(50,bot.light_until-now):0;
    v->empty_click=empty_click;
    v->marks=marks; v->first_mark=first_mark; v->mark_count=mark_count; v->elapsed_ms=clock_ms;
    v->bot_line=bot_line;
    if (bot_line.expires<=clock_ms+intermission_age) v->bot_line.expires=0;
    v->air=air_sound; v->impact=impact_sound;
    v->sparks=sparks; v->spark_count=spark_count;
    v->blood=blood; v->blood_count=blood_count;
    v->classic_assist=classic_assist; v->selected_primary=view_weapon(player.state.nextprimary);
    v->px = player.x; v->py = player.y; v->eye = eye(player);
    v->angle = player.yaw; v->pitch = player.pitch;
    v->camera=listener_position();
    v->camera_yaw=player.respawn_at?death_yaw:player.yaw;
    v->camera_pitch=player.respawn_at?death_pitch:player.pitch;
    if((lan_match || stock_round) && phase==INTERMISSION) {
        v->camera={vantage[0]/DMF,vantage[1]/DMF,vantage[2]/DMF};
        v->camera_yaw=(vantage[3]-90)*radians;
        v->camera_pitch=vantage[4]*radians;
    }
    v->bot_supply=supply_index;
    v->bot_rifle_mag=bot.state.mag[GUN_ASSAULT]; v->bot_rifle_ammo=bot.state.ammo[GUN_ASSAULT];
    v->bot_pistol_mag=bot.state.mag[GUN_PISTOL]; v->bot_pistol_ammo=bot.state.ammo[GUN_PISTOL];
    v->radar_active=clock_ms<radar_action_until;
    v->player_floor=feet_z;
    v->player_velocity={player.velocity_x,player.velocity_y,net_vertical_velocity};
    v->on_floor=grounded; v->on_ladder=on_ladder; v->crouching=was_crouching;
    v->death_frame=player.respawn_at?fminf(5,(now+player_respawn_ms-player.respawn_at)/100.f):0;
    bot_view(v);
    v->bot_name=offline_bot_name;v->player_deaths=player.deaths;v->bot_deaths=bot.deaths;
    v->player_frags = player.frags; v->bot_frags = bot.frags;
    v->health = player.state.health; v->armour = player.state.armour;
    int gun = player.state.gunselect;
    v->magazine = player.state.mag[gun]; v->reserve = player.state.ammo[gun];
    v->scoped=scoped && phase==PLAYING && !player.respawn_at && !switch_end && gun==GUN_SNIPER;
    v->shot_end=shot_end; v->shot_direction=shot_direction; v->shot_contact=shot_contact; v->shotgun_info=shotgun_info;
    v->weapon=view_weapon(gun);
    v->respawning = !!player.respawn_at; v->reload = player.reload_at > clock_ms;
    v->hide_crosshair=player.reload_hud_until>clock_ms;
    float lower=0;
    if (v->reload) {
        float duration=reloadtime(gun);
        float elapsed=1-((float)player.reload_hud_until-clock_ms)/duration;
        float limit=fmaxf(.5f,1-(player.reload_at-clock_ms)/duration);
        lower=sinf(3.14159265f*fminf(elapsed,limit));
    } else if (switch_end && switch_end>clock_ms) {
        lower=sinf(3.14159265f*(switch_end-clock_ms)/switch_ms);
    }
    v->gun_pose_blend=0;
    float speed=fminf(1,hypotf(player.velocity_x,player.velocity_y));
    float step=sway_ms/105.f, bob=(v->reload || switch_end)?0:.025f*speed*(was_crouching?.75f:1);
    float cp=cosf(player.pitch),sp=sinf(player.pitch);
    float forward=(1.5f/16)*(player.sway_x*cosf(player.yaw)+player.sway_y*sinf(player.yaw));
    float up=(.4f/16)*sway_vertical-bob*fabsf(cosf(step));
    v->gun_side=(1.5f/16)*(player.sway_y*cosf(player.yaw)-player.sway_x*sinf(player.yaw))
                +bob*sinf(step)*cp;
    v->gun_up=up*cp-forward*sp;
    v->gun_forward=forward*cp+up*sp;
    v->gun_rotation=-90*lower; v->gun_back=0;
    v->gun_pose=v->next_gun_pose=0;
    v->flash=0; v->flash_angle=flash_angle;
    v->akimbo_left=akimbo_left;
    v->grenade_ammo=player.state.mag[GUN_GRENADE];
    if(gun==GUN_GRENADE && grenade_state!=GRENADE_IDLE) {
        unsigned first=grenade_state==GRENADE_HELD?1:14;
        float frame=fminf(12,(now-(grenade_state==GRENADE_HELD?primed_at:thrown_at))/25.f);
        v->gun_pose=first+(unsigned)frame;
        v->next_gun_pose=first+(unsigned)fminf(12,frame+1);
        v->gun_pose_blend=frame-floorf(frame);
    } else if (gun!=GUN_GRENADE && !v->reload && player.shot_until>clock_ms) {
        unsigned age=attackdelay(gun)-(player.shot_until-clock_ms);
        float t=age/(float)attackdelay(gun);
        // renderhudmodel samples the MD3 shoot loop every 40 ms.
        float frame=age/40.f;
        if (gun==GUN_KNIFE) {
            v->gun_pose=1+(unsigned)fminf(11,frame);
            v->next_gun_pose=v->gun_pose<12?v->gun_pose+1:12;
            v->gun_pose_blend=frame-floorf(frame);
        } else {
            unsigned poses=gun==GUN_SHOTGUN?25:(gun==GUN_CARBINE || gun==GUN_SNIPER)?50:8;
            v->gun_pose=1+((unsigned)frame%poses); v->next_gun_pose=1+v->gun_pose%poses;
            v->gun_pose_blend=frame-floorf(frame);
            v->flash=age<(unsigned)fmaxf(120,attackdelay(gun))/4;
        }
        float phase=1.5f*t-1.5f;
        float kick=-sinf(phase*phase*phase);
        if (stance<PLAYER_EYE_HEIGHT) kick*=.75f;
        v->gun_rotation=guns[gun].mdl_kick_rot*kick;
        v->gun_back=guns[gun].mdl_kick_back*kick*.1f;
    }
    v->wide_crosshair = gun==GUN_ASSAULT && player.shots>3;
    v->underwater_audio=phase==PLAYING && !player.respawn_at && eye(player)+PLAYER_ABOVE_EYE<WATER_LEVEL;
    v->ear_ringing=phase==PLAYING && !player.respawn_at && ear_damage_until && clock_ms<=ear_damage_until;
    v->damage_alpha=hurt_until>clock_ms?.45f*fminf(1,(hurt_until-clock_ms)/125.f):0;
    v->bot_shot_pending=phase==PLAYING && bot_saw_player && !bot.respawn_at &&
                        bot_attack_at<=clock_ms+40 && bot.ready_at<=clock_ms+40;
    v->respawn_work_pending=(!lan_match && bot.respawn_at && bot.respawn_at<=clock_ms+40) ||
                           (v->bot_shot_pending && v->health<=guns[bot.state.gunselect].damage);
    v->damage_directions=damage_directions;
    v->phase = phase; v->bot_sound = bot_sound;
    v->stock_round=stock_round && !lan_match;
    v->movement_sound=movement_sound; v->pickup_sound=pickup_sound; v->pain_sound=pain_sound;
    v->stance_sound=stance_sound;
    v->footsteps=phase==PLAYING && !player.respawn_at && clock_ms>=player.pain_until ? step_sound:SILENT;
    v->actor_effect_count=actor_sounds.count;
    memcpy(v->actor_effects,actor_sounds.effects,actor_sounds.count*sizeof(MatchSound));
    v->bot_footsteps=SILENT;
    bool crouched=lan_match && remote_pose.crouching;
    if(phase==PLAYING && !hide_bot && !bot.respawn_at && bot_pose && clock_ms>=bot.pain_until &&
       (lan_match?remote_air_ms:bot_air_ms)<=50 && eye(bot)>=WATER_LEVEL+.5f &&
       hypotf(distance(bot.x,bot.y,v->camera.x,v->camera.y),eye(bot)-v->camera.z)<(crouched?10:20)) {
        bool wet=v->bot_floor+.25f<WATER_LEVEL;
        if(!wet || WATER_LEVEL-ground(bot.x,bot.y)<=v->bot_height)
            v->bot_footsteps=crouched?(wet?WATER_CROUCH_STEPS:CROUCH_STEPS):(wet?WATER_STEPS:FOOTSTEPS);
    }
    v->bot_voice=bot_voice; v->gib_sound=gib_sound; v->hide_bot=hide_bot;
    v->hide_player_body=hide_player_body;v->gib_origin=gib_origin;
    for (unsigned i=0;i<gib_count;++i) gib_views[i]=gibs[i].view;
    v->grenade_count=active_grenade_count;
    for(unsigned i=0;i<active_grenade_count;++i) v->grenades[i]={active_grenades[i].flight.position,active_grenades[i].flight.rotation};
    v->explosions=explosions;v->explosion_count=explosion_count;
    v->gibs=gib_views; v->gib_count=gib_count;
    unsigned end_ms=lan_match?lan_end_ms:stock_round?bot_match_duration:duration;
    v->seconds = clock_ms >= end_ms ? 0 : (end_ms-clock_ms+999)/1000;
    v->items = item_views; v->item_count = 0;
    for (int i = 0; i < item_count; ++i) if (lan_match ? lan_item_visible[i] : clock_ms >= unavailable_until[i]) {
        const ItemLocation &p = locations[i];
        int icon = p.kind-I_CLIPS;
        item_views[v->item_count++] = {p.x,p.y,icon,ground(p.x,p.y)+p.height_tenths*.1f};
    }
}

static int target_contact(const MatchView *v,float range,bool check_geometry) {
    if (!v->bot_alive || v->respawning) return 0;
    float c=cosf(v->angle),s=sinf(v->angle);
    if (fabsf((v->ty-v->py)*c-(v->tx-v->px)*s)>4) return 0;
    float x=v->bot_head.x-v->px,y=v->bot_head.y-v->py,z=v->bot_head.z-v->eye;
    float xy=cosf(v->pitch),dx=c*xy,dy=s*xy,dz=sinf(v->pitch);
    float closest=x*dx+y*dy+z*dz;
    float rx=x-closest*dx,ry=y-closest*dy,rz=z-closest*dz;
    float radius2=.4f*.4f-rx*rx-ry*ry-rz*rz;
    MatchPoint delta={v->px-v->tx,v->py-v->ty,v->eye-v->bot_floor};
    auto torso=[&](float limit) { return torso_contact(delta,v->bot_yaw,v->bot_tilt,v->bot_height,dx,dy,dz,limit); };
    float enter=radius2>=0?closest-sqrtf(radius2):-1;
    bool head=enter>=0 && enter<=range;
    if (!head) enter=torso(range);
    else if (check_geometry) {
        // A waist-high prop can separate intersections with the two volumes.
        float body=torso(enter);
        if (body>=0) enter=body;
    }
    if (enter<0) return 0;
    if (!check_geometry) return head?2:1;
    return visible(v->px,v->py,v->eye,v->px+dx*enter,v->py+dy*enter,v->eye+dz*enter) &&
           prop_depth({v->px,v->py,v->eye},{dx,dy,dz},enter)>=enter;
}
int match_targeted(const MatchView *v) {
    return !v->hide_bot && target_contact(v,512,true);
}

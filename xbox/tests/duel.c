#include "../match.h"
#include "../geometry.h"
#include "../navigation.h"
#include "../ambience.h"
#include "../shot_light.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../generated/starts.h"
#include "../generated/bot_head.h"

static void test_routes_to_spawn(int gx,int gy) {
    route_to(gx,gy);
    for (unsigned i=0;i<sizeof(starts)/sizeof(starts[0]);++i) {
        float x=starts[i][0]+.5f,y=starts[i][1]+.5f;
        int n;
        for (n=0;n<2000;++n) {
            float tx,ty;
            assert(route_next(x,y,&tx,&ty));
            if (hypotf(tx-x,ty-y)<.001f) {
                float range=hypotf(gx+.5f-x,gy+.5f-y);
                if (gx==150 && gy==102) {
                    assert(range<=5.001f);
                    assert(visible(x,y,body_floor(x,y)+PLAYER_EYE_HEIGHT,gx+.5f,gy+.5f,
                                   body_floor(gx+.5f,gy+.5f)+PLAYER_EYE_HEIGHT-1));
                } else assert(range<.001f);
                break;
            }
            float dx=tx-x,dy=ty-y;
            float scale=.18f/fmaxf(.18f,hypotf(dx,dy));
            walk(&x,&y,dx*scale,dy*scale);
        }
        assert(n<2000);
    }
}

static void test_heightfield_impact(void) {
    MatchInput input={.start=1};
    match_prepare(); match_update(&input,0,0);
    input=(MatchInput){.turn=-.5f,.look=-.825f};
    for (int i=0;i<24;++i) match_update(&input,20,.02f);
    MatchView v; match_view(&v);
    float dx=cosf(v.pitch)*cosf(v.angle),dy=cosf(v.pitch)*sinf(v.angle),dz=sinf(v.pitch);
    SurfaceHit hit;
    assert(trace_surface(v.px,v.py,v.eye,v.px+20*dx,v.py+20*dy,v.eye+20*dz,&hit));
    assert((int)hit.x==125 && (int)hit.y==131 && !hit.can_mark);
    input=(MatchInput){.fire=1};
    assert(match_attack(&input)==RIFLE_SHOT);
    match_view(&v);
    assert(v.mark_count==0 && v.spark_count==5);
    assert(hypotf(v.sparks[0].x-hit.x,v.sparks[0].y-hit.y)<.2f);
}

static void test_shot_light(void) {
    extern const uint8_t cell_light[MAP][MAP][3];
    ShotLight lights[2]={{129,132,50},{0}};
    LightPatch dirty[4];
    shot_light_init();
    assert(shot_light_update(lights,dirty)==1);
    unsigned peak=lit_cell(129,132)[0];
    assert(peak>cell_light[132][129][0]);
    assert(shot_light_update(lights,dirty)==0);
    lights[0].strength=10;
    assert(shot_light_update(lights,dirty)>0 && lit_cell(129,132)[0]<peak);
    lights[0].x-=2; lights[1]=(ShotLight){130,132,50};
    assert(shot_light_update(lights,dirty)>0);
    lights[0].strength=lights[1].strength=0;
    assert(shot_light_update(lights,dirty)==2);
    for (int y=125;y<140;++y) for (int x=119;x<137;++x)
        assert(memcmp(lit_cell(x,y),cell_light[y][x],3)==0);
}

static void test_wind_playback(void) {
    // At this position only the wind emitter is audible.
    int start[512][2]={0}, next[512][2]={0}, replay[1024][2]={0};
    ambience_update(10,133,7,0);
    ambience_mix(start,512);
    ambience_update(10,133,7,1);
    ambience_mix(next,512);
    assert(memcmp(start,next,sizeof(start))!=0);
    int stereo=0;
    for (int i=0;i<512;++i) stereo |= start[i][0]!=start[i][1];
    assert(stereo);
    ambience_update(255,255,64,0);
    ambience_mix(replay,1024);
    for (int i=0;i<1024;++i) assert(!replay[i][0] && !replay[i][1]);
    ambience_update(10,133,7,2);
    ambience_mix(replay,1024);
    assert(memcmp(replay,start,sizeof(start))==0);
    assert(memcmp(replay+512,next,sizeof(next))==0);
}

static void test_strafe_reversal(unsigned hz) {
    MatchInput in={.start=1};
    MatchView start,v;
    match_prepare(); match_update(&in,0,0); match_view(&start);
    float velocity=0,distance=0;
    for (int section=0;section<3;++section) {
        in=(MatchInput){.strafe=section==0?.6f:section==1?-.6f:0};
        for (unsigned frame=0;frame<hz/5;++frame) {
            unsigned dt=(frame+1)*1000/hz-frame*1000/hz;
            match_update(&in,dt,dt*.001f);
        }
        // Reference 200 ms of Windows movement at its native physics tick.
        for (int tick=0;tick<40;++tick) {
            velocity+=(in.strafe-velocity)/24;
            distance+=velocity*16*.005f;
        }
        match_view(&v);
        float actual=(v.py-start.py)*cosf(start.angle)-(v.px-start.px)*sinf(start.angle);
        assert(fabsf(actual-distance)<.001f);
    }
}

static void test_weapon_sway(int crouch) {
    MatchInput in={.start=1};
    MatchView v;
    match_prepare(); match_update(&in,0,0);
    in=(MatchInput){.strafe=.5f,.move=.25f,.look=.3f,.crouch=crouch};
    float velocity=0,filtered=0;
    for (int ms=20;ms<=200;ms+=20) {
        for (int tick=0;tick<4;++tick) velocity+=(1-velocity)/24;
        filtered=.49f*filtered+.51f*velocity;
        match_update(&in,20,.020f); match_view(&v);
        float bob=.025f*hypotf(in.strafe,in.move)*velocity*(crouch?.75f:1);
        float side=1.5f/16*in.strafe*filtered+bob*sinf(ms/105.f)*cosf(v.pitch);
        float up=-bob*fabsf(cosf(ms/105.f)),forward=1.5f/16*in.move*filtered;
        assert(fabsf(v.gun_side-side)<.00001f);
        assert(fabsf(v.gun_up-(up*cosf(v.pitch)-forward*sinf(v.pitch)))<.00001f);
        assert(fabsf(v.gun_forward-(up*sinf(v.pitch)+forward*cosf(v.pitch)))<.00001f);
    }
}

static void test_rifle_recoil(unsigned hz,int crouch) {
    MatchView v;
    MatchInput in={.start=1};
    match_prepare(); match_update(&in,0,0);
    in=(MatchInput){.crouch=crouch};
    for (int i=0;i<10;++i) match_update(&in,20,.02f);
    match_view(&v); float before=v.pitch,x=v.px,y=v.py,yaw=v.angle;
    in.fire=1; assert(match_attack(&in)==RIFLE_SHOT); in.fire=0;
    unsigned previous=0;
    for (unsigned frame=1;frame<=hz*3/10;++frame) {
        unsigned now=frame*1000/hz,dt=now-previous;
        match_update(&in,dt,dt*.001f);
        if (now==100 || now==300) {
            // Integrating the Windows rifle at 200 Hz gives these angles.
            float expected=(now==100?1.555456f:.947958f)*(crouch?.75f:1);
            match_view(&v);
            assert(fabsf((v.pitch-before)/.017453293f-expected)<.001f);
            float velocity=-.30f*(crouch?.75f:1),travel=0;
            for (unsigned tick=0;tick<now/5;++tick) {
                velocity*=23.f/24;
                travel+=velocity*.005f*(crouch?6.4f:16.f);
            }
            float actual=(v.px-x)*cosf(yaw)+(v.py-y)*sinf(yaw);
            assert(fabsf(actual-travel)<.002f);
        }
        previous=now;
    }
}

static void test_firing_pose(int pistol) {
    MatchInput in={.start=1};
    MatchView v;
    match_prepare(); match_update(&in,0,0);
    if (pistol) {
        in=(MatchInput){.swap=1}; match_update(&in,1,.001f);
        in=(MatchInput){0}; match_update(&in,401,.1f);
    }
    in=(MatchInput){.fire=1};
    assert(match_attack(&in)==(pistol?PISTOL_SHOT:RIFLE_SHOT));
    in.fire=0;
    unsigned delay=pistol?160:120;
    match_view(&v); float angle=v.flash_angle;
    for (unsigned age=0;age<delay;++age) {
        match_view(&v);
        assert(v.gun_pose==1+age/40 && v.next_gun_pose==2+age/40);
        assert(fabsf(v.gun_pose_blend-(age%40)/40.f)<.00001f);
        assert(v.flash==(age<delay/4) && v.flash_angle==angle);
        assert(v.player_light==(age<50?50:age<100?100-age:0));
        match_update(&in,1,.001f);
    }
    match_view(&v); assert(v.gun_pose==0 && !v.flash);
}

static void test_rifle_cadence(unsigned hz) {
    MatchInput in={.start=1};
    match_prepare();
    match_update(&in,0,0);
    in=(MatchInput){.fire=1};
    unsigned shots=0,first=0,previous=0;
    for (unsigned frame=1;frame<=3*hz && shots<20;++frame) {
        unsigned now=frame*1000/hz,dt=now-previous;
        match_update(&in,dt,dt*.001f);
        if (match_attack(&in)==RIFLE_SHOT) {
            if (!shots) first=now;
            unsigned due=first+shots*120;
            assert(previous<due && now>=due);
            ++shots;
        }
        previous=now;
    }
    assert(shots==20);
}

static void test_tracking_breakaway(void) {
    MatchView view;
    MatchInput input = {.start=1};
    match_prepare();
    match_update(&input,16,1.f/60);
    input = (MatchInput){.strafe=.6f,.turn=-.108f};
    for (int i=0; i<20; ++i) match_update(&input,16,1.f/60);
    match_view(&view);
    assert(match_targeted(&view));
    for (int direction=1; direction>=-1; direction-=2) {
        float before=view.angle;
        input.turn=(float)direction;
        match_update(&input,17,1.f/60);
        match_view(&view);
        float degrees=direction*remainderf(view.angle-before,6.2831853f)/.017453293f;
        assert(degrees>2.5f && degrees<2.6f);
    }
}

static void test_stance(unsigned hz) {
    MatchView v;
    MatchInput input={.start=1};
    match_prepare(); match_update(&input,0,0); match_view(&v);
    float standing=v.eye, peak=standing;
    assert(fabsf(standing-map_floor(map_cube((int)v.px,(int)v.py))-4.5f)<.001f);
    int jumps=0, landings=0;
    input=(MatchInput){.jump=1};
    for (unsigned frame=1;frame<=hz;++frame) {
        unsigned now=frame*1000/hz,previous=(frame-1)*1000/hz,dt=now-previous;
        match_update(&input,dt,dt*.001f); match_view(&v);
        jumps+=v.movement_sound==PLAYER_JUMP;
        landings+=v.movement_sound==SOFT_LAND;
        peak=fmaxf(peak,v.eye);
        // Heights from the original 5 ms physics loop on a flat floor.
        if (now==100) assert(fabsf(v.eye-standing-1.660f)<.002f);
        if (now==300) assert(fabsf(v.eye-standing-2.941f)<.002f);
        if (now==600) assert(fabsf(v.eye-standing-.099f)<.002f);
        if (v.movement_sound==SOFT_LAND) assert(previous<605 && now>=605);
        if (now>=650) assert(fabsf(v.eye-standing)<.001f);
    }
    assert(fabsf(peak-standing-2.943f)<.01f);
    assert(jumps==1 && landings==1);
    input=(MatchInput){.crouch=1,.strafe=.2f};
    for (int i=0;i<15;++i) {
        match_update(&input,16,1.f/60); match_view(&v);
        assert(v.stance_sound==(i==0?CROUCH_IN:SILENT));
        assert(fabsf(v.eye-standing+fminf(1.125f,(i+1)*9.6f/60))<.001f);
        assert(v.footsteps==CROUCH_STEPS);
    }
    match_view(&v); assert(fabsf(v.eye-standing+1.125f)<.001f);
    input.crouch=0;
    for (int i=0;i<15;++i) {
        match_update(&input,16,1.f/60); match_view(&v);
        assert(v.stance_sound==(i==0?CROUCH_OUT:SILENT));
        assert(v.footsteps==FOOTSTEPS);
    }
    match_view(&v); assert(fabsf(v.eye-standing)<.001f);
    input.strafe=0; match_update(&input,16,1.f/60); match_view(&v);
    assert(v.footsteps==SILENT);
}

static void test_weapon_switch(void) {
    MatchView v;
    MatchInput in={.start=1};
    match_prepare();
    match_update(&in,16,.016f);
    in=(MatchInput){.swap=1};
    match_update(&in,16,.016f);
    assert(match_attack(&in)==WEAPON_CHANGE);
    in=(MatchInput){.fire=1};
    match_update(&in,199,.016f);
    match_view(&v);
    assert(!v.pistol && v.gun_rotation < -89 && match_attack(&in)==SILENT);
    match_update(&in,2,.002f);
    match_view(&v);
    assert(v.pistol && v.gun_rotation < -89 && match_attack(&in)==SILENT);
    in.fire=0;
    match_update(&in,199,.016f);
    assert(match_attack(&in)==SILENT);
    in.fire=1;
    match_update(&in,1,.001f);
    assert(match_attack(&in)==PISTOL_SHOT);
    MatchView fired;
    match_view(&fired);
    in.fire=0;
    for (int i=0;i<20;++i) {
        match_update(&in,5,.005f);
        assert(match_attack(&in)==SILENT);
    }
    match_view(&v);
    float backwards=(fired.px-v.px)*cosf(fired.angle)+(fired.py-v.py)*sinf(fired.angle);
    assert(fabsf(backwards-.1f*16*.115f*(1-powf(23.f/24,20)))<.001f);
    // Press again during the last 60 ms of the pistol cooldown and hold.
    in.fire=1;
    assert(match_attack(&in)==SILENT);
    match_update(&in,60,.060f);
    assert(match_attack(&in)==PISTOL_SHOT);
    match_update(&in,160,.160f);
    assert(match_attack(&in)==SILENT);
}

static void test_knife(void) {
    MatchInput in={.start=1};
    MatchView v;
    match_prepare(); match_update(&in,16,.016f);
    in=(MatchInput){.knife_select=1};
    match_update(&in,16,.016f);
    assert(match_attack(&in)==WEAPON_CHANGE);
    in=(MatchInput){.fire=1};
    match_update(&in,401,.016f);
    assert(match_attack(&in)==KNIFE_SWING);
    match_view(&v);
    assert(v.knife && !v.flash && !v.blood_count && !v.spark_count && !v.player_light);
    match_update(&in,460,.016f); match_view(&v);
    assert(v.gun_pose==12 && v.next_gun_pose==12);
    match_update(&in,40,.016f);
    assert(match_attack(&in)==SILENT);
    in.fire=0; match_attack(&in);
    for (int i=0;i<500 && !v.player_frags;++i) {
        match_view(&v);
        float dx=v.tx-v.px,dy=v.ty-v.py,d=hypotf(dx,dy);
        in.move=d>1.8f;
        in.turn=remainderf(atan2f(dy,dx)-v.angle,6.2831853f)/(.016f*150*.017453293f);
        in.look=(atan2f(v.bot_floor+3-v.eye,d)-v.pitch)/(.016f*105*.017453293f);
        in.fire=d<3 && (i%2);
        match_update(&in,16,.016f); match_attack(&in); match_view(&v);
    }
    assert(v.player_frags==2 && !v.bot_alive);
    in=(MatchInput){.knife_select=1}; match_update(&in,16,.016f);
    assert(match_attack(&in)==WEAPON_CHANGE);
    in=(MatchInput){.fire=1}; match_update(&in,401,.016f);
    assert(match_attack(&in)==RIFLE_SHOT); match_view(&v);
    assert(!v.knife && !v.pistol && v.magazine==19);
}
static void test_fighter_contact(void) {
    MatchView v;
    match_prepare(); match_view(&v);
    float x=v.px,y=v.py,feet=v.eye-PLAYER_EYE_HEIGHT;
    float height=PLAYER_EYE_HEIGHT+PLAYER_ABOVE_EYE;
    ActorBounds bot={x+3,y,feet,feet+height};
    walk_past_actor(&x,&y,3,0,feet,height,0,&bot);
    assert(x>v.px && bot.x-x>=2*PLAYER_RADIUS);
    float stopped=x;
    walk_past_actor(&x,&y,.5f,.5f,feet,height,0,&bot);
    assert(bot.x-x>=2*PLAYER_RADIUS && y>v.py+.45f);
    walk_past_actor(&x,&y,-.5f,0,feet,height,0,&bot);
    assert(x<stopped-.35f);
    float floor=feet,ceiling=feet+30;
    actor_clearance(&bot,bot.x,bot.y,bot.top,height,&floor,&ceiling);
    assert(floor==bot.top);
}
static void test_rifle_trigger(int keep_firing) {
    MatchInput in={.start=1};
    MatchView v;
    match_prepare(); match_update(&in,0,0);
    in=(MatchInput){.fire=1};
    for (int i=0;i<20;++i) {
        assert(match_attack(&in)==RIFLE_SHOT);
        // Brief trigger releases inside the cooldown preserve burst spread.
        in.fire=0;
        match_update(&in,30,.03f); assert(match_attack(&in)==SILENT);
        match_view(&v); assert(v.wide_crosshair==(i>=3));
        in.fire=1;
        match_update(&in,90,.09f);
    }
    in.fire=keep_firing;
    assert(match_attack(&in)==RIFLE_RELOAD);
    match_view(&v);
    assert(v.magazine==20 && v.empty_click==keep_firing && v.reload && !v.wide_crosshair);
    in.fire=1;
    unsigned wait=2000+(keep_firing?250:0);
    for (unsigned ms=0;ms<wait-1;++ms) {
        match_update(&in,1,.001f);
        assert(match_attack(&in)==SILENT);
    }
    match_update(&in,1,.001f);
    assert(match_attack(&in)==RIFLE_SHOT);
}

static void test_reload_tap(void) {
    MatchView v;
    MatchInput in={.start=1};
    match_prepare(); match_update(&in,0,0);
    in=(MatchInput){.fire=1};
    assert(match_attack(&in)==RIFLE_SHOT);
    in=(MatchInput){.refill=1};
    match_update(&in,30,.030f);
    assert(match_attack(&in)==RIFLE_RELOAD);
    match_view(&v);
    assert(v.magazine==20 && v.reserve==39 && v.reload && v.hide_crosshair && !v.flash);
    in=(MatchInput){.fire=1};
    for (unsigned ms=1;ms<2090;++ms) {
        in.swap=ms==2001;
        match_update(&in,1,.001f);
        assert(match_attack(&in)==SILENT);
        if (ms==1000 || ms==1090 || ms==2000) {
            match_view(&v);
            assert(v.reload && !v.pistol && v.hide_crosshair==(ms<2000));
            if (ms<2000) assert(fabsf(v.gun_rotation+90)<.001f);
            else assert(v.gun_rotation < -12 && v.gun_rotation > -13);
        }
    }
    match_update(&in,1,.001f);
    assert(match_attack(&in)==RIFLE_SHOT);
}
static void test_well_ladder(void) {
    MatchView v;
    MatchInput in={.start=1};
    match_prepare(); match_update(&in,16,.016f);
    // Walk from the opening spawn around the west side of the courtyard.
    const float route[][2]={{115.5f,132.5f},{115.5f,154.5f},{145.5f,154.5f},{145.5f,161.5f}};
    for (unsigned i=0;i<sizeof(route)/sizeof(route[0]);++i) {
        int n;
        for (n=0;n<250;++n) {
            match_view(&v);
            float dx=route[i][0]-v.px,dy=route[i][1]-v.py;
            if (hypotf(dx,dy)<.12f) break;
            float scale=1/fmaxf(4.f,hypotf(dx,dy));
            in=(MatchInput){.move=scale*(dx*cosf(v.angle)+dy*sinf(v.angle)),
                            .strafe=scale*(dy*cosf(v.angle)-dx*sinf(v.angle))};
            match_update(&in,16,.016f);
        }
        assert(n<250);
    }
    // Face east and jump backward over the rim.
    for (int i=0;i<100;++i) {
        match_view(&v);
        in=(MatchInput){.turn=fmaxf(-.8f,fminf(.8f,-remainderf(v.angle,6.2831853f)/(.016f*150*.017453293f)))};
        match_update(&in,16,.016f);
    }
    in=(MatchInput){.move=-1};
    for (int i=0;i<8;++i) match_update(&in,16,.016f);
    in.jump=1;
    for (int i=0;i<32;++i) match_update(&in,16,.016f);
    in=(MatchInput){0};
    int entered=0,exited=0;
    for (int i=0;i<100;++i) {
        match_update(&in,16,.016f); match_view(&v);
        entered+=v.movement_sound==WATER_ENTER;
    }
    // Standing on the well floor puts the eyes exactly at the swim threshold.
    assert(fabsf(v.eye+8.5f)<.001f && entered==0);
    in.crouch=1;
    for (int i=0;i<20;++i) {
        match_update(&in,16,.016f); match_view(&v);
        entered+=v.movement_sound==WATER_ENTER;
    }
    assert(v.eye < -9 && entered==1);
    in.crouch=0;
    in.move=1;
    for (int i=0;i<150 && v.eye < -3;++i) {
        match_update(&in,16,.016f); match_view(&v);
        exited+=v.movement_sound==WATER_EXIT;
    }
    match_view(&v); float halfway=v.eye;
    assert(halfway>=-3 && halfway<0 && exited==1);
    in.move=0;
    for (int i=0;i<12;++i) match_update(&in,16,.016f);
    match_view(&v); assert(fabsf(v.eye-halfway)<.001f);
    in.move=-1;
    for (int i=0;i<8;++i) match_update(&in,16,.016f);
    match_view(&v); assert(v.eye<halfway-1);
    in=(MatchInput){.crouch=1};
    for (int i=0;i<60;++i) match_update(&in,16,.016f);
    match_view(&v); assert(v.eye < -9);
    in=(MatchInput){.move=1};
    for (int i=0;i<110;++i) match_update(&in,16,.016f);
    match_view(&v); assert(v.px>142 && v.eye>3);
}
static void test_incoming_fire(int strafe) {
    MatchInput in={.start=1};
    MatchView v;
    match_prepare(); match_update(&in,0,0);
    int heard=0;
    for (unsigned f=0;f<1200 && !heard;++f) {
        in=(MatchInput){0};
        if (strafe && f<30) { in.strafe=.7f; in.move=.45f; }
        match_update(&in,(f+1)*1000/60-f*1000/60,1.f/60);
        match_view(&v);
        PositionalSound event=strafe?v.air:v.impact;
        if (!event.sound) continue;
        assert(v.bot_sound==RIFLE_SHOT);
        assert(v.bot_line.expires==v.elapsed_ms+75);
        float distance=hypotf(hypotf(event.pos.x-v.px,event.pos.y-v.py),event.pos.z-v.eye);
        assert(distance<=15);
        if (strafe) assert(event.sound==BULLET_AIR1 || event.sound==BULLET_AIR2);
        else {
            assert(event.sound==BULLET_IMPACT);
            assert(v.mark_count && v.spark_count);
            const BulletMark *m=v.marks+v.mark_count-1;
            assert(m->x==event.pos.x && m->y==event.pos.y && m->z==event.pos.z);
        }
        heard=1;
    }
    assert(heard);
}

static void test_bot_hitpush(void) {
    MatchInput in={.start=1};
    MatchView before,after;
    match_prepare(); match_update(&in,0,0); match_view(&before);
    in=(MatchInput){.fire=1};
    assert(match_attack(&in)==RIFLE_SHOT);
    match_view(&after);
    assert(after.blood_count>0);
    assert(after.spark_count==5);
    float effect_distance=hypotf(hypotf(after.sparks[0].x-before.px,after.sparks[0].y-before.py),
                                 after.sparks[0].z-(before.eye-.2f));
    float target_distance=hypotf(hypotf(before.tx-before.px,before.ty-before.py),
                                 map_floor(map_cube((int)before.tx,(int)before.ty))+PLAYER_EYE_HEIGHT-before.eye);
    assert(fabsf(effect_distance-target_distance)<.001f);
    in.fire=0;
    for (int i=0;i<20;++i) match_update(&in,5,.005f);
    match_view(&after);
    // An MTP-57 hit adds 22/100 velocity. Integrate 20 PC ground ticks.
    float speed=.22f,travel=0;
    for (int i=0;i<20;++i) { speed*=23.f/24; travel+=speed*.005f*16; }
    float forward=(after.tx-before.tx)*cosf(before.angle)+(after.ty-before.ty)*sinf(before.angle);
    assert(fabsf(forward-travel)<.002f);
}

static void test_idle_animation(void) {
    MatchView v;
    MatchInput in={0};
    match_prepare();
    for (int ms=0;ms<=4000;ms+=25) {
        if (ms) match_update(&in,25,.025f);
        match_view(&v);
        assert(v.phase==READY && v.current_pose.first==(ms/100)%40);
        assert(v.current_pose.second==(v.current_pose.first+1)%40);
        assert(fabsf(v.current_pose.blend-(ms%100)/100.f)<.0001f);
    }
}

static void test_bot_hit_volumes(void) {
    MatchView v;
    MatchInput in={.start=1};
    match_prepare(); match_update(&in,0,0); match_view(&v);
    float c=cosf(v.angle),s=sinf(v.angle);
    // Level rays through the idle head tag hit within the PC's 0.4 radius.
    v.pitch=0; v.eye=v.bot_head.z+.05f;
    v.px=v.bot_head.x-3*c-.35f*s; v.py=v.bot_head.y-3*s+.35f*c;
    assert(match_targeted(&v));
    v.px-=.15f*s; v.py+=.15f*c;
    assert(!match_targeted(&v));
    // The 1.1-radius torso is wider than the head and ends at eye height.
    v.px=v.tx-3*c-1.05f*s; v.py=v.ty-3*s+1.05f*c;
    v.eye=map_floor(map_cube((int)v.tx,(int)v.ty))+2;
    assert(match_targeted(&v));
    v.px-=.15f*s; v.py+=.15f*c;
    assert(!match_targeted(&v));
    // Interpolated head coordinates follow the very same pain frames as the mesh.
    MatchPoint idle_head=v.bot_head;
    in=(MatchInput){.fire=1};
    assert(match_attack(&in)==RIFLE_SHOT);
    match_view(&v); assert(v.current_pose.first==54);
    assert(v.previous_pose.first==0 && v.pose_transition==0);
    assert(fabsf(v.bot_head.z-idle_head.z)<.0001f);
    in.fire=0;
    for (int ms=10;ms<=150;ms+=10) {
        match_update(&in,10,.01f); match_view(&v);
        // Sample both MD2 sequences at the current time, then crossfade the tags.
        int idle=ms/100,pain=54+ms/75;
        float t=ms/150.f;
        MatchPoint from=mix_point(head_poses[idle],head_poses[idle+1],ms%100/100.f);
        MatchPoint to=mix_point(head_poses[pain],head_poses[pain+1],ms%75/75.f);
        MatchPoint expected=bot_model_point(&v,mix_point(from,to,t));
        assert(fabsf(v.pose_transition-t)<.0001f);
        assert(v.current_pose.first==pain);
        assert(hypotf(hypotf(v.bot_head.x-expected.x,v.bot_head.y-expected.y),
                      v.bot_head.z-expected.z)<.0001f);
    }
}

static void test_prop_depth(void) {
    // From the courtyard floor, the -33 degree sightline crosses the visible
    // top of the fallen barrel before reaching the floor behind it.
    MatchPoint eye={160.5f,156.5f,3.6f};
    float pitch=-33.f*.017453293f;
    MatchPoint aim={cosf(pitch),0,sinf(pitch)};
    float depth=prop_depth(eye,aim,10);
    assert(fabsf(eye.x+depth*aim.x-163.19169f)<.001f);
    assert(fabsf(eye.z+depth*aim.z-1.85200f)<.001f);
    assert(prop_depth(eye,aim,2)==2);
    // This ray passes through the barrel's bounding box beside its curved rim.
    assert(prop_depth((MatchPoint){162.33f,154.49f,4},(MatchPoint){0,0,-1},4)==4);
    // The bench seat and backrest have different depths inside the same box.
    MatchPoint down={0,0,-1};
    assert(fabsf(prop_depth((MatchPoint){104.9f,127.5f,3},down,6)-2.636f)<.002f);
    assert(fabsf(prop_depth((MatchPoint){105.5f,127.5f,3},down,6)-3.485f)<.002f);
}

int main(void) {
    test_prop_depth();
    test_heightfield_impact();
    test_shot_light();
    test_wind_playback();
    test_bot_hitpush();
    test_bot_hit_volumes();
    test_idle_animation();
    test_incoming_fire(0);
    test_incoming_fire(1);
    navigation_prepare();
    for (unsigned i=0;i<sizeof(starts)/sizeof(starts[0]);++i) test_routes_to_spawn(starts[i][0],starts[i][1]);
    test_strafe_reversal(60);
    test_weapon_sway(0);
    test_weapon_sway(1);
    test_strafe_reversal(50);
    test_rifle_recoil(60,0);
    test_rifle_recoil(60,1);
    test_rifle_recoil(50,0);
    test_rifle_recoil(50,1);
    test_firing_pose(0); test_firing_pose(1);
    test_rifle_cadence(60);
    test_rifle_cadence(50);
    test_well_ladder();
    test_weapon_switch();
    test_knife();
    test_fighter_contact();
    test_rifle_trigger(0);
    test_rifle_trigger(1);
    test_reload_tap();
    MatchInput floor_shot={.start=1};
    MatchView effects;
    match_prepare();
    match_update(&floor_shot,16,.016f);
    floor_shot=(MatchInput){.look=-1};
    for (int i=0;i<20;++i) match_update(&floor_shot,16,.016f);
    match_view(&effects);
    float floor_z=map_floor(map_cube((int)effects.px,(int)effects.py));
    float depth=(floor_z-effects.eye)/sinf(effects.pitch);
    float horizontal=depth*cosf(effects.pitch);
    float length=hypotf(horizontal,floor_z-(effects.eye-.2f));
    // Original attackphysics adds three spread offsets to the target, scaled
    // by muzzle-to-target distance. These are the seeded first rifle offsets.
    float target_x=effects.px+horizontal*cosf(effects.angle)+.009f*length;
    float target_y=effects.py+horizontal*sinf(effects.angle)+.010f*length;
    floor_shot.fire=1;
    assert(match_attack(&floor_shot)==RIFLE_SHOT);
    match_view(&effects);
    assert(effects.mark_count==1 && effects.spark_count==5);
    assert(fabsf(effects.sparks[0].x-target_x)<.00005f);
    assert(fabsf(effects.sparks[0].y-target_y)<.00005f);
    assert(fabsf(effects.sparks[0].z-(floor_z+.011f*length))<.00005f);
    for (unsigned i=0;i<effects.spark_count;++i) {
        const SplashParticle *p=effects.sparks+i;
        // Sparks start at the spread-adjusted endpoint. The hole lies on
        // the map surface and has its separate offset to avoid z-fighting.
        assert(hypotf(hypotf(p->x-effects.marks[0].x,p->y-effects.marks[0].y),
                      p->z-effects.marks[0].z)<1.015f);
        assert(p->x==effects.sparks[0].x && p->y==effects.sparks[0].y && p->z==effects.sparks[0].z);
        assert(p->born==effects.elapsed_ms && p->lifetime<750);
        assert(p->dx*p->dx+p->dy*p->dy+p->dz*p->dz<=150*150);
    }
    // This floor has opposite high corners. The body uses the cell mean,
    // while a vertical shot at the center lands on its rendered diagonal.
    SurfaceHit slope;
    // The solid boundary above the east battlement is a sky portal.
    assert(!trace_surface(179,156.5f,12,190,156.5f,12,&slope));
    // The low rotated obstacle at (164,156) stops walking but can be jumped.
    float x=160.5f,y=156.5f,floor,ceiling;
    // Only the middle of the southern edge overlaps the solid cell (159,160).
    body_space(159.5f,159.5f,0,&floor,&ceiling);
    assert(floor>ceiling);
    // The southeast half of the corner near spawn is solid. Both endpoints
    // of this shot lie inside the same cell, on opposite sides of its seam.
    body_space(130.32f,133.32f,-2,&floor,&ceiling);
    assert(floor==-2 && ceiling==16);
    body_space(130.57f,133.57f,-2,&floor,&ceiling);
    assert(floor>ceiling);
    assert(visible(131.1f,134.1f,3,131.2f,134.2f,3));
    assert(trace_surface(131.1f,134.1f,3,131.9f,134.9f,3,&slope));
    assert(fabsf(slope.fraction-.5f)<.001f);
    assert(slope.axis==3 && fabsf(slope.x-131.489f)<.001f && fabsf(slope.y-134.489f)<.001f);
    assert(slope.can_mark);
    float slide_x=130.39f,slide_y=133.39f;
    walk_body(&slide_x,&slide_y,.1f,0,-2,4.5f,1.5f);
    assert(fabsf(slide_x-130.4376f)<.001f && fabsf(slide_y-133.34f)<.001f);
    // A two-cell-wide corner has a four-unit platform on one side. Rays
    // above the platform can cross the entire diagonal in either direction.
    body_space(167.72f,119.72f,0,&floor,&ceiling);
    assert(floor==0 && ceiling==16);
    body_space(169.5f,121.5f,4,&floor,&ceiling);
    assert(floor==4 && ceiling==16);
    assert(visible(168.4f,120.4f,5,169.6f,121.6f,5));
    assert(trace_surface(168.4f,120.5f,2,169.6f,121.7f,2,&slope));
    assert(slope.axis==3 && fabsf(slope.x+slope.y-289.978f)<.001f);
    walk_body(&x,&y,5,0,0,4.5f,1.5f);
    assert(x>161 && x<163);
    walk_body(&x,&y,2,0,2,4.5f,0);
    body_space(x,y,2,&floor,&ceiling);
    assert(x>163 && fabsf(floor-1.8f)<.001f);
    x=160.5f;
    walk(&x,&y,5,0);
    assert(x>161 && x<163);
    // The wider hull overlaps the rotated clip here, then clears its west edge.
    body_space(162.4f,154.4f,0,&floor,&ceiling);
    assert(fabsf(floor-1.8f)<.001f);
    body_space(161.6f,154.4f,0,&floor,&ceiling);
    assert(floor==0 && ceiling==16);
    assert(map_floor(map_cube(91,72))==2.375f);
    assert(trace_surface(91.5f,72.5f,4,91.5f,72.5f,1,&slope));
    assert(slope.axis==2 && fabsf(slope.z-2.515f)<.001f);
    assert(!slope.can_mark);
    test_stance(50);
    test_stance(60);
    test_stance(200);
    test_tracking_breakaway();
    MatchView view;
    MatchInput input = {0};
    match_prepare();
    match_view(&view);
    assert(view.phase == READY);
    assert(match_targeted(&view));
    MatchView miss = view;
    miss.angle += 3.14159265f;
    assert(!match_targeted(&miss));
    miss = view;
    miss.pitch = 1.f;
    assert(!match_targeted(&miss));
    miss = view;
    miss.bot_alive = 0;
    assert(!match_targeted(&miss));
    input.start = 1;
    match_update(&input,16,16*.001f);
    input.start = 0;

    int saw_bot_death = 0, saw_bot_respawn = 0, saw_player_death = 0, saw_player_respawn = 0;
    unsigned burst_shots=0;
    int saw_pain=0,saw_fall=0,saw_corpse=0;
    int heard_bot_pain=0,heard_bot_death=0;
    int saw_splash=0,saw_stain=0;
    for (unsigned tick = 0; tick < 3000; ++tick) {
        match_view(&view);
        input.fire = !view.player_frags;
        // Track the torso during the opening burst, including its vertical recoil.
        float dx=view.tx-view.px, dy=view.ty-view.py;
        float chest=view.bot_floor+PLAYER_EYE_HEIGHT-1;
        input.look=input.fire ? (atan2f(chest-view.eye,hypotf(dx,dy))-view.pitch)/(.033f*105*.017453293f) : 0;
        input.turn=input.fire ? remainderf(atan2f(dy,dx)-view.angle,6.2831853f)/(.033f*150*.017453293f) : 0;
        match_update(&input,33,33*.001f);
        match_view(&view);
        if (match_attack(&input)==RIFLE_SHOT) ++burst_shots;
        match_view(&view);
        if (input.fire) assert(view.wide_crosshair == (burst_shots>3));
        for (unsigned i=0;i<view.blood_count;++i) {
            const BloodParticle *b=view.blood+i;
            if (b->on_floor) {
                assert(b->motion.lifetime==10000);
                saw_stain=1;
            } else if (b->motion.born==view.elapsed_ms) {
                assert(b->motion.x==view.tx && b->motion.y==view.ty);
                saw_splash=1;
            }
        }
        if (view.bot_alive && view.current_pose.first>=54 && view.current_pose.first<=57) saw_pain=1;
        if (view.bot_voice>=BOT_PAIN1 && view.bot_voice<=BOT_PAIN5) heard_bot_pain=1;
        if (view.bot_voice==PLAYER_DEATH || view.bot_voice==ALT_DEATH) heard_bot_death=1;
        if (!view.bot_alive) {
            assert(!match_targeted(&view));
            if (view.current_pose.first>178 && view.current_pose.first<183) saw_fall=1;
            if (view.current_pose.first==183 && view.current_pose.second==183) saw_corpse=1;
        }
        if (view.player_frags && !view.bot_alive) saw_bot_death = 1;
        if (saw_bot_death && view.bot_alive) saw_bot_respawn = 1;
        if (view.respawning) saw_player_death = 1;
        if (saw_player_death && view.health == 100) saw_player_respawn = 1;
        if (view.phase == INTERMISSION) break;
    }
    // This scenario caught the bot getting stuck on a ledge after the first respawn.
    assert(saw_bot_death && saw_bot_respawn && saw_player_death && saw_player_respawn);
    assert(saw_pain && saw_fall && saw_corpse);
    assert(heard_bot_pain && heard_bot_death);
    assert(saw_splash && saw_stain);
    assert(view.phase == INTERMISSION && view.player_frags == 1 && view.bot_frags == FRAG_LIMIT);
    input.start = 1;
    match_update(&input,16,16*.001f);
    match_view(&view);
    assert(view.phase == PLAYING && view.health == 100 && view.magazine == 20 && view.reserve == 40);
    assert(view.player_frags == 0 && view.bot_frags == 0);
    assert(view.blood_count==0);
    input.start = 0;
    match_update(&input,90000,90000*.001f);
    match_view(&view);
    assert(view.phase == INTERMISSION && view.seconds == 0);
    input = (MatchInput){.start=1};
    match_update(&input,16,1.f/60);
    input.start=0;
    input.turn=.5f;
    match_view(&view);
    float first=view.angle;
    match_update(&input,16,1.f/60);
    match_view(&view);
    float second=view.angle;
    match_update(&input,17,1.f/60);
    match_view(&view);
    assert(fabsf((second-first)-(view.angle-second))<.00001f);
    input.turn=1;
    for (int i=0;i<26;++i) match_update(&input,16,1.f/60);
    match_view(&view);
    float boosted=view.angle;
    match_update(&input,16,1.f/60);
    match_view(&view);
    assert(fabsf(remainderf(view.angle-boosted,6.2831853f)-220*.017453293f/60)<.0001f);
    float before_reverse=view.angle;
    input.turn=-1;
    match_update(&input,16,1.f/60);
    match_view(&view);
    float reversed=remainderf(view.angle-before_reverse,6.2831853f);
    assert(reversed<0 && fabsf(reversed)<155*.017453293f/60);
    float stopped=view.angle;
    input.turn=0;
    input.look=1;
    for (int i=0;i<60;++i) match_update(&input,16,1.f/60);
    match_view(&view);
    assert(view.angle==stopped && fabsf(view.pitch-75*.017453293f)<.0001f);
    puts("Passed bot duel, respawns, score limit, rematch and time limit.");
}

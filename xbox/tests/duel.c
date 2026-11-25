#include "../match.h"
#include "../navigation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

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

static void test_stance(void) {
    MatchView v;
    MatchInput input={.start=1};
    match_prepare(); match_update(&input,16,1.f/60); match_view(&v);
    float standing=v.eye, peak=standing;
    int jumps=0, landings=0;
    input=(MatchInput){.jump=1};
    for (int i=0;i<90;++i) {
        match_update(&input,16,1.f/60); match_view(&v);
        jumps+=v.movement_sound==PLAYER_JUMP;
        landings+=v.movement_sound==PLAYER_LAND;
        peak=fmaxf(peak,v.eye);
        if (i>60) assert(fabsf(v.eye-standing)<.001f);
    }
    assert(peak>standing+2 && peak<standing+5);
    assert(jumps==1 && landings==1);
    input=(MatchInput){.crouch=1,.strafe=.2f};
    for (int i=0;i<15;++i) {
        match_update(&input,16,1.f/60); match_view(&v);
        assert(v.stance_sound==(i==0?CROUCH_IN:SILENT));
        assert(v.footsteps==CROUCH_STEPS);
    }
    match_view(&v); assert(fabsf(v.eye-standing+.9f)<.001f);
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
    for (int i=0;i<20;++i) match_update(&in,5,.005f);
    match_view(&v);
    float backwards=(fired.px-v.px)*cosf(fired.angle)+(fired.py-v.py)*sinf(fired.angle);
    assert(fabsf(backwards-.1f*16*.115f*(1-powf(23.f/24,20)))<.001f);
}
static void test_well_ladder(void) {
    MatchView v;
    MatchInput in={.start=1};
    match_prepare(); match_update(&in,16,.016f);
    // Walk from the opening spawn around the west side of the courtyard.
    const float route[][2]={{117.5f,132.5f},{117.5f,142.5f},{123.5f,142.5f},
        {123.5f,143.5f},{127.5f,143.5f},{127.5f,147.5f},{128.5f,147.5f},
        {128.5f,148.5f},{129.5f,148.5f},{129.5f,149.5f},{145.5f,149.5f},{145.5f,161.5f}};
    for (unsigned i=0;i<sizeof(route)/sizeof(route[0]);++i) {
        int n;
        for (n=0;n<150;++n) {
            match_view(&v);
            float dx=route[i][0]-v.px,dy=route[i][1]-v.py;
            if (hypotf(dx,dy)<.12f) break;
            float scale=1/fmaxf(4.f,hypotf(dx,dy));
            in=(MatchInput){.move=scale*(dx*cosf(v.angle)+dy*sinf(v.angle)),
                            .strafe=scale*(dy*cosf(v.angle)-dx*sinf(v.angle))};
            match_update(&in,16,.016f);
        }
        assert(n<150);
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
    assert(v.eye < -9 && entered==1);
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
    in.fire=0;
    for (int i=0;i<20;++i) match_update(&in,5,.005f);
    match_view(&after);
    // An MTP-57 hit adds 22/100 velocity. Integrate 20 PC ground ticks.
    float speed=.22f,travel=0;
    for (int i=0;i<20;++i) { speed*=23.f/24; travel+=speed*.005f*16; }
    float forward=(after.tx-before.tx)*cosf(before.angle)+(after.ty-before.ty)*sinf(before.angle);
    assert(fabsf(forward-travel)<.002f);
}

int main(void) {
    test_bot_hitpush();
    test_incoming_fire(0);
    test_incoming_fire(1);
    test_strafe_reversal(60);
    test_strafe_reversal(50);
    test_rifle_recoil(60,0);
    test_rifle_recoil(60,1);
    test_rifle_recoil(50,0);
    test_rifle_recoil(50,1);
    test_rifle_cadence(60);
    test_rifle_cadence(50);
    test_well_ladder();
    test_weapon_switch();
    MatchInput floor_shot={.start=1};
    MatchView effects;
    match_prepare();
    match_update(&floor_shot,16,.016f);
    floor_shot=(MatchInput){.look=-1};
    for (int i=0;i<20;++i) match_update(&floor_shot,16,.016f);
    floor_shot.fire=1;
    assert(match_attack(&floor_shot)==RIFLE_SHOT);
    match_view(&effects);
    assert(effects.mark_count==1 && effects.spark_count==5);
    for (unsigned i=0;i<effects.spark_count;++i) {
        const SplashParticle *p=effects.sparks+i;
        assert(p->x==effects.marks[0].x && p->y==effects.marks[0].y && p->z==effects.marks[0].z);
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
    walk_body(&x,&y,5,0,0,4.5f,1.5f);
    assert(x>161 && x<163);
    walk_body(&x,&y,2,0,2,4.5f,0);
    body_space(x,y,2,&floor,&ceiling);
    assert(x>163 && fabsf(floor-1.8f)<.001f);
    x=160.5f;
    walk(&x,&y,5,0);
    assert(x>161 && x<163);
    // Opposite corners of the clip's bounding box remain walkable.
    body_space(162.4f,154.4f,0,&floor,&ceiling);
    assert(floor==0 && ceiling==16);
    assert(map_floor(map_cube(91,72))==2.375f);
    assert(trace_surface(91.5f,72.5f,4,91.5f,72.5f,1,&slope));
    assert(slope.axis==2 && fabsf(slope.z-2.515f)<.001f);
    test_stance();
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
        float chest=map_floor(map_cube((int)view.tx,(int)view.ty))+2.6f;
        input.look=input.fire ? (atan2f(chest-view.eye,hypotf(dx,dy))-view.pitch)/(.033f*105*.017453293f) : 0;
        input.turn=input.fire ? remainderf(atan2f(dy,dx)-view.angle,6.2831853f)/(.033f*150*.017453293f) : 0;
        match_update(&input,33,33*.001f);
        match_view(&view);
        if (!input.fire) burst_shots=0;
        if (match_attack(&input)==RIFLE_SHOT) ++burst_shots;
        match_view(&view);
        assert(view.wide_crosshair == (burst_shots>3));
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
        if (view.bot_alive && view.bot_pose>=54 && view.bot_pose<=57) saw_pain=1;
        if (view.bot_voice>=BOT_PAIN1 && view.bot_voice<=BOT_PAIN5) heard_bot_pain=1;
        if (view.bot_voice==PLAYER_DEATH || view.bot_voice==ALT_DEATH) heard_bot_death=1;
        if (!view.bot_alive) {
            assert(!match_targeted(&view));
            if (view.bot_pose>178 && view.bot_pose<183) saw_fall=1;
            if (view.bot_pose==183 && view.next_bot_pose==183) saw_corpse=1;
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

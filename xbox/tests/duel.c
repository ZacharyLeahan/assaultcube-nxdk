#include "../match.h"
#include "../navigation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

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
    input=(MatchInput){.crouch=1};
    for (int i=0;i<15;++i) match_update(&input,16,1.f/60);
    match_view(&v); assert(fabsf(v.eye-standing+.9f)<.001f);
    input.crouch=0;
    for (int i=0;i<15;++i) match_update(&input,16,1.f/60);
    match_view(&v); assert(fabsf(v.eye-standing)<.001f);
}

int main(void) {
    // This floor has opposite high corners. The body uses the cell mean,
    // while a vertical shot at the center lands on its rendered diagonal.
    SurfaceHit slope;
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
        if (view.player_frags && !view.bot_alive) saw_bot_death = 1;
        if (saw_bot_death && view.bot_alive) saw_bot_respawn = 1;
        if (view.respawning) saw_player_death = 1;
        if (saw_player_death && view.health == 100) saw_player_respawn = 1;
        if (view.phase == INTERMISSION) break;
    }
    // This scenario caught the bot getting stuck on a ledge after the first respawn.
    assert(saw_bot_death && saw_bot_respawn && saw_player_death && saw_player_respawn);
    assert(view.phase == INTERMISSION && view.player_frags == 1 && view.bot_frags == FRAG_LIMIT);
    input.start = 1;
    match_update(&input,16,16*.001f);
    match_view(&view);
    assert(view.phase == PLAYING && view.health == 100 && view.magazine == 20 && view.reserve == 40);
    assert(view.player_frags == 0 && view.bot_frags == 0);
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

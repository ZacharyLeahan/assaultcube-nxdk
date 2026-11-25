#include "../match.h"
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

int main(void) {
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
    for (unsigned tick = 0; tick < 3000; ++tick) {
        match_view(&view);
        input.fire = !view.player_frags;
        // Track the torso during the opening burst, including its vertical recoil.
        float dx=view.tx-view.px, dy=view.ty-view.py;
        float chest=map_cube((int)view.tx,(int)view.ty)->floor+2.6f;
        input.look=input.fire ? (atan2f(chest-view.eye,hypotf(dx,dy))-view.pitch)/(.033f*105*.017453293f) : 0;
        input.turn=input.fire ? remainderf(atan2f(dy,dx)-view.angle,6.2831853f)/(.033f*150*.017453293f) : 0;
        match_update(&input,33,33*.001f);
        match_view(&view);
        match_attack(&input);
        match_view(&view);
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

#include "../match.h"
#include <assert.h>
#include <stdio.h>


int main(void) {
    MatchView view;
    MatchInput input = {0};
    match_prepare();
    match_view(&view);
    assert(view.phase == READY);
    input.start = 1;
    match_update(&input,16);
    input.start = 0;

    int saw_bot_death = 0, saw_bot_respawn = 0, saw_player_death = 0, saw_player_respawn = 0;
    for (unsigned tick = 0; tick < 3000; ++tick) {
        input.fire = tick < 34;
        match_update(&input,33);
        match_view(&view);
        int aimed = input.fire && match_targeted(&view);
        match_attack(&input,aimed);
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
    match_update(&input,16);
    match_view(&view);
    assert(view.phase == PLAYING && view.health == 100 && view.magazine == 20 && view.reserve == 40);
    assert(view.player_frags == 0 && view.bot_frags == 0);
    input.start = 0;
    match_update(&input,90000);
    match_view(&view);
    assert(view.phase == INTERMISSION && view.seconds == 0);
    puts("Passed bot duel, respawns, score limit, rematch and time limit.");
}

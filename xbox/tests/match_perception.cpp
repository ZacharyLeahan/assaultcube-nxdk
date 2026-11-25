// Test the live bot at controlled positions on a flat, open part of ac_desert.
#include "../match.cpp"
#include <cassert>

static void place_target(float degrees,float range,float dz=0) {
    player.x=bot.x+range*cosf(degrees*radians);
    player.y=bot.y+range*sinf(degrees*radians);
    player.eye_height=bot.eye_height+dz;
}

static void start_perception_test() {
    match_prepare(); begin(PLAYING); clock_ms=1000;
    bot.x=154.5f; bot.y=110.5f; bot.yaw=0;
    bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
}

extern "C" void test_easy_bot_perception(void) {
    for (int sign=-1; sign<=1; sign+=2) {
        start_perception_test();
        place_target(sign*56,5);
        update_bot(0);
        assert(!bot_saw_player);
        unsigned scan=next_enemy_search;
        assert(scan>=1300 && scan<=1360);

        place_target(sign*54,5);
        clock_ms=scan-1;
        update_bot(0);
        assert(!bot_saw_player);
        clock_ms=scan;
        update_bot(0);
        assert(bot_saw_player);
        assert(bot_attack_at>=scan+1500 && bot_attack_at<=scan+2000);

        place_target(180,5);
        clock_ms++;
        update_bot(0);
        assert(bot_saw_player);
        place_target(0,100);
        clock_ms++;
        update_bot(0);
        assert(!bot_saw_player);
    }
    start_perception_test();
    place_target(180,3.9f);
    update_bot(0);
    assert(bot_saw_player);

    start_perception_test();
    place_target(180,4);
    update_bot(0);
    assert(!bot_saw_player);

    start_perception_test();
    place_target(180,3.9f,1.2f);
    update_bot(0);
    assert(!bot_saw_player);
}

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

extern "C" void test_easy_bot_spacing(void) {
    start_perception_test();
    place_target(0,8);
    float x=bot.x,y=bot.y;
    update_bot(.016f);
    assert(combat_forward==-1 && combat_move_until==1500 && bot.x<x);
    assert(stride_rate<0 && stride_phase>=0 && stride_phase<6);
    assert(distance(x,y,bot.x,bot.y)<16*.016f);
    float side=combat_side;
    clock_ms+=100;
    update_bot(.016f);
    assert(combat_side==side && combat_move_until==1500);

    start_perception_test();
    choose_combat_move(30,rifle_range);
    assert(combat_forward==0 && combat_side==0);
    choose_combat_move(81,rifle_range);
    assert(combat_forward==1);
    choose_combat_move(8,pistol_range);
    assert(combat_forward==0);
    choose_combat_move(16,pistol_range);
    assert(combat_forward==1);

    start_perception_test();
    // This courtyard corner has less than four units of lateral clearance.
    // Hold position instead of oscillating between the two walls.
    bot.x=107.20012f; bot.y=97.10065f; bot.yaw=1.146f;
    bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
    choose_combat_move(17,rifle_range);
    assert(combat_forward==0 && combat_side==0);

    start_perception_test();
    place_target(180,3);
    x=bot.x; y=bot.y;
    update_bot(.016f);
    assert(bot_saw_player && bot.x==x && bot.y==y);

    place_target(0,2.5f);
    bot_attack_at=0;
    bot.ready_at=0;
    int rounds=bot.state.mag[GUN_ASSAULT];
    update_bot(0);
    assert(bot.state.mag[GUN_ASSAULT]==rounds);

    bot.state.mag[GUN_ASSAULT]=bot.state.ammo[GUN_ASSAULT]=0;
    place_target(0,8);
    update_bot(0);
    assert(bot.state.gunselect==GUN_PISTOL && combat_forward==0);
}

extern "C" void test_easy_bot_aim(void) {
    start_perception_test();
    for (int sign=-1;sign<=1;sign+=2) {
        float pitch=bot_aim_step(0,sign*25*radians,true,.016f);
        assert(sign*pitch>0 && sign*pitch<1*radians);
    }
    float yaw=bot_aim_step(179*radians,-175*radians,false,.016f);
    assert(yaw>179*radians && yaw<180*radians);

    place_target(0,5);
    feet_z=eye(bot)-PLAYER_EYE_HEIGHT;
    player.yaw=player.pitch=0;
    int health=player.state.health;
    stance=PLAYER_EYE_HEIGHT/2;
    bot_fire_ray(0,0,GUN_ASSAULT);
    assert(player.state.health==health);
    stance=PLAYER_EYE_HEIGHT;
    bot_fire_ray(0,0,GUN_ASSAULT);
    assert(player.state.health<health);
}

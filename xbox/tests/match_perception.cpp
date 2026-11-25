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
    MatchView v={};
    bot.shot_until=bot.ready_at=clock_ms+120;
    update_bot(0);
    bot_view(&v);
    assert(!v.bot_pistol);
    clock_ms=bot.ready_at;
    update_bot(0);
    bot_view(&v);
    assert(v.bot_pistol && combat_forward==0);
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

    // Fire through the standing barrel, then over its rim at the same player.
    start_perception_test();
    bot.x=93; bot.y=141; bot.yaw=-90*radians; bot.eye_height=-5.8f;
    place_target(-90,8);
    feet_z=-8; stance=PLAYER_EYE_HEIGHT; player.yaw=player.pitch=0;
    health=player.state.health;
    bot_fire_ray(bot.yaw,0,GUN_ASSAULT);
    assert(player.state.health==health && mark_count==0);
    assert(sparks[0].y>135 && sparks[0].y<136 && fabsf(sparks[0].x-93)<.001f);
    update_bot(0);
    assert(!bot_saw_player);
    bot.eye_height=player.eye_height=-4.8f;
    clock_ms=next_enemy_search;
    update_bot(0);
    assert(bot_saw_player);
    bot_fire_ray(bot.yaw,0,GUN_ASSAULT);
    assert(player.state.health<health);
}

extern "C" void test_wall_impact_lifetime(void) {
    match_prepare(); begin(PLAYING);
    SurfaceHit hit={0,0,0,1,1,1};
    MatchInput in={};
    MatchView view={};
    // Both fighters producing impacts during a sustained exchange.
    for (int i=0;i<80;++i) {
        hit.x=(float)i;
        mark_surface(hit);
        match_update(&in,1,.001f);
    }
    match_view(&view);
    assert(view.mark_count==80 && view.marks[view.first_mark].x==0);
    bot.ready_at=duration;
    match_update(&in,9919,.016f);
    match_view(&view);
    assert(view.mark_count==80 && view.marks[view.first_mark].expires==clock_ms+1);
    match_update(&in,1,.001f);
    match_view(&view);
    assert(view.mark_count==79 && view.marks[view.first_mark].x==1);
    match_update(&in,79,.016f);
    match_view(&view);
    assert(!view.mark_count);
    for (int i=0;i<160;++i) { hit.x=(float)i; mark_surface(hit); }
    match_view(&view);
    assert(view.mark_count==160);
    for (unsigned i=0;i<view.mark_count;++i)
        assert(view.marks[(view.first_mark+i)%MARK_CAPACITY].x==(float)i);

    start_perception_test();
    place_target(0,5);
    player.yaw=player.pitch=0;
    feet_z=eye(bot)-PLAYER_EYE_HEIGHT;
    int health=player.state.health;
    bot_fire_ray(0,atanf(-.95f),GUN_ASSAULT);
    assert(player.state.health<health && mark_count==1);
    assert(impact_sound.sound==BULLET_IMPACT);
    // A shallower shot strikes the same player farther from the floor.
    bot_fire_ray(0,atanf(-.45f),GUN_ASSAULT);
    assert(mark_count==1);
}

extern "C" void test_smg_loadout(void) {
    MatchInput in={};
    MatchView v={};
    match_prepare();
    in.swap=1;
    match_update(&in,0,0);
    match_view(&v);
    assert(v.phase==READY && v.weapon==VIEW_SMG && v.selected_primary==VIEW_SMG);
    in.swap=0; in.start=1;
    match_update(&in,0,0);
    in.start=0; in.fire=1;
    bot.ready_at=duration;
    for (int i=0;i<30;++i) {
        assert(match_attack(&in)==SMG_SHOT);
        match_view(&v);
        assert(v.magazine==29-i && v.reserve==60 && v.flash && !v.wide_crosshair);
        assert(fabsf(player.pitch_velocity-fminf(5,2.5f+((i+1)/5.f)*((i+1)/5.f)))<.0001f);
        match_update(&in,79,.079f);
        assert(match_attack(&in)==SILENT);
        match_update(&in,1,.001f);
    }
    in.fire=0;
    assert(match_attack(&in)==SMG_RELOAD);
    match_view(&v);
    assert(v.reload && v.magazine==30 && v.reserve==30);
    match_update(&in,1649,.016f);
    in.fire=1;
    assert(match_attack(&in)==SILENT);
    match_update(&in,1,.001f);
    assert(match_attack(&in)==SMG_SHOT);
    in.fire=0; in.swap=1;
    match_update(&in,1,.001f); in.swap=0;
    match_update(&in,401,.016f); match_view(&v);
    assert(v.weapon==VIEW_PISTOL && v.weapon!=VIEW_SMG);
    in.swap=1; match_update(&in,1,.001f); in.swap=0;
    match_update(&in,401,.016f); match_view(&v);
    assert(v.weapon==VIEW_SMG && v.magazine==29);
    spawn(player,17); match_view(&v);
    assert(v.weapon==VIEW_SMG && v.magazine==30 && v.reserve==60);
    bot.state.health=1;
    damage(bot,player,GUN_SUBGUN,1,0,0); match_view(&v);
    assert(!strcmp(v.frag_messages[0].text,"you sprayed Bot"));
    phase=INTERMISSION; in.start=1;
    match_update(&in,1,.001f); match_view(&v);
    assert(v.weapon==VIEW_SMG && v.phase==PLAYING && v.player_frags==0);
    match_prepare(); match_view(&v);
    assert(v.weapon!=VIEW_SMG && v.magazine==20);
}

extern "C" void test_carbine_loadout(void) {
    match_prepare();
    MatchInput in={};
    MatchView v={};
    in.swap=1;
    match_update(&in,0,0); match_update(&in,0,0);
    match_view(&v);
    assert(v.selected_primary==VIEW_CARBINE && v.weapon==VIEW_CARBINE);
    assert(v.magazine==10 && v.reserve==20);
    in.swap=0; in.start=1;
    match_update(&in,0,0);
    in.start=0; in.fire=1;
    assert(match_attack(&in)==CARBINE_SHOT);
    match_view(&v);
    assert(v.magazine==9 && v.flash && player.ready_at==720 && !v.wide_crosshair);
    match_update(&in,179,.016f); match_view(&v); assert(v.flash);
    match_update(&in,1,.001f); match_view(&v); assert(!v.flash && v.gun_back!=0);
    match_update(&in,541,.016f);
    assert(match_attack(&in)==SILENT); // Held through the next ready frame.
    in.fire=0; match_attack(&in);
    in.fire=1;
    assert(match_attack(&in)==CARBINE_SHOT);
    in.fire=0; in.refill=1;
    assert(match_attack(&in)==CARBINE_RELOAD);
    match_view(&v);
    assert(v.reload && v.magazine==10 && v.reserve==18 && v.hide_crosshair);
    assert(player.ready_at==720+720+1800);
    bot.state.health=100; bot.state.armour=0;
    damage(bot,player,GUN_CARBINE,1,0,0); assert(bot.state.health==40);
    damage(bot,player,GUN_CARBINE,1,0,0); match_view(&v);
    assert(!strcmp(v.frag_messages[0].text,"you picked off Bot"));
    spawn(player,17); match_view(&v);
    assert(v.weapon==VIEW_CARBINE && v.magazine==10 && v.reserve==20);
    phase=INTERMISSION;
    in={}; in.swap=1; in.start=1;
    match_update(&in,0,0); match_view(&v);
    assert(v.weapon==VIEW_RIFLE && v.phase==PLAYING);
}

extern "C" void test_bot_ammunition(void) {
    start_perception_test();
    int box=0;
    while (locations[box].kind!=I_AMMO) ++box;
    bot.x=123.2f; bot.y=127.2f;
    bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
    bot.state.mag[GUN_ASSAULT]=bot.state.ammo[GUN_ASSAULT]=0;
    bot.state.mag[GUN_PISTOL]=bot.state.ammo[GUN_PISTOL]=0;
    for (int i=0;i<item_count;++i) unavailable_until[i]=duration;
    assert(find_ammunition(false,true)==-1);
    unavailable_until[box]=0;
    assert(find_ammunition(false,true)==box);
    MatchInput in={};
    for (int i=0;i<1800 && !bot.state.mag[GUN_ASSAULT];++i)
        match_update(&in,16,.016f);
    assert(unavailable_until[box]>clock_ms);
    assert(bot.state.gunselect==GUN_ASSAULT && bot.state.mag[GUN_ASSAULT]==20);
    assert(bot.state.ammo[GUN_ASSAULT]==20 && bot.reload_at>clock_ms);

    // Resolve the score limit independently of the route the bot took for supplies.
    bot.frags=FRAG_LIMIT-1;
    player.state.health=1; player.state.armour=0;
    damage(player,bot,GUN_ASSAULT,1,0,0);
    assert(phase==INTERMISSION && bot.frags==FRAG_LIMIT);
}

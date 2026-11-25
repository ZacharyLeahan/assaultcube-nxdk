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
    assert(v.bot_weapon!=VIEW_PISTOL);
    clock_ms=bot.ready_at;
    update_bot(0);
    bot_view(&v);
    assert(v.bot_weapon==VIEW_PISTOL && combat_forward==0);
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
    assert(v.weapon==VIEW_SNIPER && v.phase==PLAYING);
}

extern "C" void test_sniper_scope(void) {
    match_prepare();
    MatchView v={};
    MatchInput in={};
    in.swap=1;
    for (int i=0;i<3;++i) match_update(&in,0,0);
    in={}; in.start=1; match_update(&in,0,0);
    in={}; in.turn=.4f; in.look=.3f;
    float base_yaw=player.yaw,base_pitch=player.pitch;
    match_update(&in,16,.016f);
    float yaw_step=player.yaw-base_yaw,pitch_step=player.pitch-base_pitch;
    base_yaw=player.yaw; base_pitch=player.pitch;
    in.scope=1;
    match_update(&in,16,.016f); match_view(&v);
    assert(v.scoped && fabsf(player.yaw-base_yaw-yaw_step*SCOPE_TAN_HALF_FOV)<.0001f);
    assert(fabsf(player.pitch-base_pitch-pitch_step*SCOPE_TAN_HALF_FOV)<.0001f);
    in.scope=0; match_update(&in,16,.016f); match_view(&v); assert(!v.scoped);
    in.scope=1; match_update(&in,16,.016f); match_update(&in,181,.016f);
    float x=1,y=0,z=0; apply_spread(player,GUN_SNIPER,x,y,z);
    assert(x==1 && y==0 && z==0);
    in.fire=1; assert(match_attack(&in)==SNIPER_SHOT);
    in.fire=0; in.refill=1; assert(match_attack(&in)==SNIPER_RELOAD);
    match_view(&v); assert(!v.scoped && v.reload);
    phase=INTERMISSION; in={}; in.start=in.swap=1;
    match_update(&in,0,0);
    in={}; in.scope=1; match_update(&in,16,.016f); match_view(&v);
    assert(v.weapon==VIEW_SHOTGUN && !v.scoped);
    phase=INTERMISSION; in={}; in.start=in.swap=1;
    match_update(&in,0,0);match_view(&v);assert(v.weapon==VIEW_RIFLE);
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

extern "C" void test_lan_pickups(void) {
    match_prepare();
    match_lan_reset_items();
    int ammo_state[NUMGUNS]={},mag_state[NUMGUNS]={};
    match_lan_spawn(56,0,GUN_ASSAULT,GUN_ASSAULT,ammo_state,mag_state);
    int box=0;
    while(locations[box].kind!=I_AMMO) ++box;
    const ItemLocation &ammo=locations[box];
    player.x=ammo.x;player.y=ammo.y;feet_z=ground(ammo.x,ammo.y);
    int &reserve=player.state.ammo[GUN_ASSAULT];
    reserve=0;
    // A late join omits items that the PC has already collected.
    assert(match_lan_touch_item()==-1);
    match_lan_item(ammo.entity,1,0);
    assert(match_lan_touch_item()==ammo.entity && reserve==0);
    assert(match_lan_touch_item()==-1);
    match_lan_item(ammo.entity,0,0);
    assert(match_lan_touch_item()==-1 && reserve==0);
    match_lan_item(ammo.entity,1,0);
    assert(match_lan_touch_item()==ammo.entity);
    match_lan_item(ammo.entity,0,1);
    match_lan_item(ammo.entity,0,1);
    assert(reserve==ammostats[GUN_ASSAULT].add);
    int kit=0;
    while(locations[kit].kind!=I_HEALTH) ++kit;
    match_lan_item(locations[kit].entity,1,0);
    match_lan_item(locations[kit].entity,0,1);
    assert(player.state.health==56+powerupstats[0].add);
    match_lan_stop();
}

extern "C" void test_lan_sidearm(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    ammo[GUN_PISTOL]=50;mag[GUN_PISTOL]=10;mag[GUN_ASSAULT]=3;
    match_prepare();
    match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    MatchInput in={};in.fire=1;
    assert(match_attack(&in)==RIFLE_SHOT);
    unsigned first=match_lan_shot_time(1000);
    for (unsigned shot=1;shot<=2;++shot) {
        while (player.state.mag[GUN_ASSAULT]>2-(int)shot) {
            match_update(&in,17,.017f);
            match_attack(&in);
        }
        assert(match_lan_shot_time(1000+clock_ms)==first+shot*attackdelay(GUN_ASSAULT));
    }
    in.swap=1;match_update(&in,16,.016f);
    assert(match_lan_selected_weapon()==-1);
    in.swap=0;match_update(&in,201,.016f);
    assert(player.state.gunselect==GUN_PISTOL && match_lan_selected_weapon()==-1);
    match_update(&in,200,.016f);
    assert(match_lan_selected_weapon()==GUN_PISTOL && match_attack(&in)==SILENT);
    in.fire=0;match_attack(&in);in.fire=1;
    assert(match_attack(&in)==PISTOL_SHOT);
    match_update(&in,500,.016f);
    assert(match_attack(&in)==SILENT && player.state.mag[GUN_PISTOL]==9);
    in.fire=0;in.refill=1;
    assert(match_attack(&in)==PISTOL_RELOAD);
    in.refill=0;in.swap=1;match_update(&in,100,.016f);
    assert(match_lan_selected_weapon()==GUN_PISTOL);
    match_update(&in,reloadtime(GUN_PISTOL),.016f);
    in.swap=0;match_update(&in,401,.016f);
    assert(match_lan_selected_weapon()==GUN_ASSAULT && player.state.mag[GUN_ASSAULT]==0);
    assert(player.state.mag[GUN_PISTOL]==10 && player.state.ammo[GUN_PISTOL]==49);
    match_lan_stop();
}

static void place_remote(float x,float y,float floor,float yaw,float pitch,int move) {
    match_lan_hide();
    LanPose pose={{x,y,floor},{},yaw,pitch,move,0,1,0,0};
    match_lan_remote(&pose);
}

extern "C" void test_lan_knife(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();
    match_lan_spawn(100,0,GUN_ASSAULT,GUN_KNIFE,ammo,mag);
    player.x=121; player.y=121; player.yaw=0; player.pitch=-.2f;
    feet_z=ground(player.x,player.y);
    player.eye_height=feet_z+PLAYER_EYE_HEIGHT;
    MatchInput in={}; in.fire=1;
    place_remote(126,121,feet_z,0,0,0);
    assert(match_attack(&in)==KNIFE_SWING && !shot_contact);
    assert(shot_end.x>123 && shot_end.x<=124);
    place_remote(124,121,feet_z,0,0,0);
    clock_ms=500;
    assert(match_attack(&in)==SILENT);
    in.fire=0;match_attack(&in);in.fire=1;
    int hp=bot.state.health;
    assert(match_attack(&in)==KNIFE_SWING && shot_contact);
    assert(bot.state.health==hp && !player.frags);
    match_lan_shot(GUN_KNIFE,121,121,feet_z);
    MatchView v={};match_view(&v);
    assert(v.bot_weapon==VIEW_KNIFE && !v.bot_light && !v.bot_flash_end_ms);
    match_lan_remote_death(1);
    in={};match_update(&in,16,.016f);match_view(&v);
    assert(v.hide_bot && v.gib_count==6 && v.gib_sound);
    match_lan_remote_spawn(GUN_PISTOL);
    place_remote(126,121,feet_z,0,0,0);
    match_lan_remote_death(0);
    match_view(&v);
    assert(!v.bot_alive && !v.hide_bot && v.current_pose.first>=178);
    for(int i=0;i<330;++i) match_update(&in,16,.016f);
    match_view(&v);
    assert(v.hide_bot && !v.bot_alive && bot.x==126);
    match_lan_remote_spawn(GUN_ASSAULT);
    place_remote(127,121,feet_z,0,0,0);
    match_view(&v);
    assert(!v.hide_bot && v.bot_alive && v.bot_weapon==VIEW_RIFLE);
    match_lan_stop();
}

extern "C" void test_lan_hitpush(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();
    match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    player.x=121;player.y=121;player.yaw=0;
    feet_z=ground(player.x,player.y);
    player.eye_height=feet_z+PLAYER_EYE_HEIGHT;
    match_lan_hitpush(GUN_PISTOL,18,1,0,0);
    MatchInput in={};
    for(int i=0;i<200;++i) match_update(&in,5,.005f);
    // Stock floor friction retains 23/24 of velocity every 5 ms.
    assert(fabsf(player.x-121-.18f*16*.005f*23)<.001f);
    assert(player.y==121 && player.state.health==100);
    player.velocity_y=.7f;
    match_lan_hitpush(GUN_PISTOL,18,-.99f,0,-.1f);
    assert(fabsf(player.velocity_x+.99f*.18f)<.0001f && player.velocity_y==.7f);
    match_lan_stop();
}

extern "C" void test_lan_primary_loadouts(void) {
    const int weapons[]={GUN_SUBGUN,GUN_CARBINE};
    for (int gun:weapons) {
        int ammo[NUMGUNS]={},mag[NUMGUNS]={};
        mag[gun]=magsize(gun);ammo[gun]=2*magsize(gun);
        match_prepare();
        match_lan_spawn(100,0,gun,gun,ammo,mag);
        MatchInput in={};in.fire=1;
        MatchWeapon weapon=view_weapon(gun);
        assert(match_attack(&in)==feedback[weapon].shot);
        match_update(&in,attackdelay(gun),.016f);
        assert(match_attack(&in)==(gun==GUN_SUBGUN?SMG_SHOT:SILENT));
        unsigned fired=gun==GUN_SUBGUN?2:1;
        assert(player.state.mag[gun]==magsize(gun)-(int)fired);
        in.fire=0;in.refill=1;
        assert(match_attack(&in)==feedback[weapon].reload);
        assert(player.state.mag[gun]==magsize(gun) && player.state.ammo[gun]==2*magsize(gun)-(int)fired);
        match_lan_remote_weapon(gun);
        place_remote(124,121,0,0,0,0);
        MatchView v={};match_view(&v);
        assert(v.weapon==weapon && v.bot_weapon==weapon);
        match_lan_stop();
    }
}

extern "C" void test_shotgun_pellets(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    ammo[GUN_SHOTGUN]=14;mag[GUN_SHOTGUN]=7;
    match_prepare();match_lan_spawn(100,0,GUN_SHOTGUN,GUN_SHOTGUN,ammo,mag);
    player.x=121;player.y=121;feet_z=ground(121,121);player.eye_height=feet_z+PLAYER_EYE_HEIGHT;
    player.yaw=0;player.pitch=-.2f;
    place_remote(124,121,feet_z,0,0,0);
    MatchInput in={};in.fire=1;
    assert(match_attack(&in)==SHOTGUN_SHOT);
    assert(shotgun_info==((21<<24)|(21<<16)|(21<<8)|65));
    assert(player.state.mag[GUN_SHOTGUN]==6 && !player.frags);
    assert(match_attack(&in)==SILENT);
    clock_ms+=880;in.fire=0;match_attack(&in);
    player.yaw=1.5f;in.fire=1;
    assert(match_attack(&in)==SHOTGUN_SHOT && !shot_contact && !shotgun_info);
    in.fire=0;in.refill=1;
    assert(match_attack(&in)==SHOTGUN_RELOAD && player.state.mag[GUN_SHOTGUN]==7 && player.state.ammo[GUN_SHOTGUN]==12);
    match_lan_stop();
}

extern "C" void test_stock_bot_round(void) {
    match_prepare();
    MatchInput in={};in.refill=1;
    match_update(&in,16,.016f);
    in={};in.start=1;match_update(&in,16,.016f);
    assert(stock_round && phase==PLAYING && player.x==starts[3][0]);
    MatchView v={};match_view(&v);assert(v.seconds==900);
    player.frags=2;bot.state.health=1;
    damage(bot,player,GUN_PISTOL,1,0,0);
    assert(phase==PLAYING && player.frags==3 && bot.deaths==1);
    match_view(&v);assert(v.bot_name && strstr(v.frag_messages[0].text,v.bot_name));
    player.state.health=1;damage(player,bot,GUN_PISTOL,1,0,0);
    in={};clock_ms=player.respawn_at+1;
    match_update(&in,16,.016f);assert(player.respawn_at);
    in.fire=1;match_update(&in,16,.016f);assert(player.respawn_at);
    in.fire=0;match_update(&in,16,.016f);assert(!player.respawn_at);
    clock_ms=bot_match_duration-1;
    match_update(&in,1,.001f);assert(phase==INTERMISSION);
    match_view(&v);assert(v.bot_deaths==1 && v.player_deaths==1);
    begin(PLAYING);match_view(&v);assert(!v.player_deaths && !v.bot_deaths);
    // A surviving bot keeps traversing the map while the player waits to respawn.
    float start_x=bot.x,start_y=bot.y;
    player.respawn_at=2000;in={};
    float farthest=0;bool used_waypoint=false;
    for(int i=0;i<1000;++i) {
        match_update(&in,16,.016f);
        farthest=fmaxf(farthest,distance(bot.x,bot.y,start_x,start_y));
        used_waypoint|=bot_route.waypoint>=0;
    }
    assert(farthest>20 && used_waypoint && !bot_saw_player && !bot.frags);
    // A two-cube ledge blocks normal walking but is within a stock jump.
    bot.x=158.2f;bot.y=127.70f;
    bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
    bot_airborne=bot_jump_request=false;
    float before=eye(bot);float high=before;
    for(int i=0;i<80;++i) {
        frame_start_ms=clock_ms;clock_ms+=5;
        move_bot(0,.08f);update_bot_height();
        high=fmaxf(high,eye(bot));
    }
    assert(high>before+1.5f && bot.y>128.5f);
    match_prepare();
}

extern "C" void test_stock_bot_primaries(void) {
    start_perception_test();stock_round=true;
    unsigned drawn=0;
    for(int i=0;i<60;++i) {
        spawn(bot,3,0);
        assert(bot.state.primary>=GUN_CARBINE && bot.state.primary<=GUN_ASSAULT);
        assert(bot.state.mag[bot.state.primary]==magsize(bot.state.primary));
        drawn|=1u<<(bot.state.primary-GUN_CARBINE);
    }
    assert(drawn==31);
    bot.state.primary=GUN_SNIPER;bot.state.gunselect=GUN_SNIPER;
    bot.state.mag[GUN_SNIPER]=5;bot.state.mag[GUN_PISTOL]=10;
    assert(preferred_bot_weapon(2)==GUN_KNIFE);
    assert(preferred_bot_weapon(9)==GUN_PISTOL);
    assert(preferred_bot_weapon(80)==GUN_SNIPER);
    bot.state.mag[GUN_GRENADE]=1;
    assert(preferred_bot_weapon(16)==GUN_GRENADE);
    for(int gun=GUN_CARBINE;gun<=GUN_ASSAULT;++gun) {
        start_perception_test();stock_round=true;
        place_target(0,4);feet_z=eye(player)-PLAYER_EYE_HEIGHT;
        player.yaw=player.pitch=0;bot_saw_player=true;
        bot.state.primary=bot.state.gunselect=gun;bot.state.mag[gun]=magsize(gun);
        bot_attack_at=0;bot.ready_at=0;bot_switch_after=clock_ms+1000;
        update_bot(0);
        assert(bot_sound==feedback[view_weapon(gun)].shot && bot.state.mag[gun]==magsize(gun)-1);
        assert(player.state.health<100);
    }
    start_perception_test();stock_round=true;
    place_target(0,8);feet_z=eye(player)-PLAYER_EYE_HEIGHT;
    bot.state.mag[GUN_GRENADE]=1;bot_saw_player=true;
    bot_attack_at=0;bot.ready_at=0;
    update_bot(0);assert(bot.state.gunselect==GUN_GRENADE);
    clock_ms=bot.ready_at;update_bot(0);
    assert(bot_primed_at && !bot.state.mag[GUN_GRENADE]);
    clock_ms+=attackdelay(GUN_GRENADE);update_bot(0);
    assert(!bot_primed_at && active_grenade_count==1 && !active_grenades[0].local);
    match_prepare();
}

extern "C" void test_lan_ffa_starts(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();match_lan_map(91);
    match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    assert(player.x==starts[3][0] && player.y==starts[3][1]);
    assert(fabsf(player.yaw-(starts[3][2]*.1f-90)*radians)<.0001f);
    // An enemy on the last candidate makes the preceding safe start preferable.
    place_remote(starts[7][0],starts[7][1],starts[7][3],0,0,0);
    match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    assert(player.x==starts[6][0] && player.y==starts[6][1]);
    match_lan_hide();
    spawn_cycle=start_count-2;
    assert(next_ffa_start(4,nullptr)==2);
    match_lan_health(0,0);
    MatchInput trigger={};trigger.fire=1;
    match_update(&trigger,100,.016f);trigger.fire=0;match_update(&trigger,100,.016f);
    assert(!match_lan_take_respawn());
    for(int i=0;i<160;++i) match_update(&trigger,16,.016f);
    assert(player.respawn_at && !match_lan_take_respawn());
    trigger.fire=1;match_update(&trigger,16,.016f);assert(!match_lan_take_respawn());
    trigger.fire=0;match_update(&trigger,16,.016f);
    assert(match_lan_take_respawn() && !match_lan_take_respawn() && player.respawn_at);
    match_lan_score(1,4);
    match_lan_time(0);
    MatchInput restart={};restart.start=1;
    match_update(&restart,16,.016f);
    assert(phase==INTERMISSION && player.frags==4);
    match_lan_map(92);
    assert(phase==PLAYING && player.frags==0 && hide_bot && spawn_cycle==-1);
    match_lan_stop();
}

extern "C" void test_lan_damage_feedback(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    player.yaw=0;
    place_remote(player.x,player.y+6,feet_z,0,0,0);
    match_lan_damage(1,1,18,82,0);
    assert(player.state.health==82 && damage_directions[2]==clock_ms+1000);
    assert(hurt_until==clock_ms+18*7 && lan_pain_sound==PLAYER_PAIN);
    match_lan_damage(0,0,32,68,0);
    assert(blood_count==3 && bot.pain_until==clock_ms+300 && player.state.health==82);
    char name[]="Windows";
    match_lan_frag(0,0,GUN_SHOTGUN,0,name);
    name[0]='X';
    match_lan_frag(1,0,GUN_SHOTGUN,1,"Windows");
    MatchView v={};match_view(&v);
    assert(!strcmp(v.frag_messages[0].text,"you were splattered by Windows"));
    assert(!strcmp(v.frag_messages[1].text,"you peppered Windows"));
    match_lan_frag(1,1,GUN_GRENADE,0,"Windows");match_view(&v);
    assert(!strcmp(v.frag_messages[0].text,"you suicided!"));
    clock_ms+=3000;match_view(&v);
    assert(!v.frag_messages[0].text);
    match_lan_stop();
}

extern "C" void test_lan_jump_report(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    MatchInput in={};in.jump=1;in.strafe=1;
    for(int i=0;i<3;++i) match_update(&in,16,.016f);
    MatchView v={};match_view(&v);
    assert(!v.on_floor && v.player_velocity.z>0 && hypotf(v.player_velocity.x,v.player_velocity.y)>0);
    assert(match_lan_take_jump() && !match_lan_take_jump());
    for(int i=0;i<120;++i) match_update(&in,16,.016f);
    match_view(&v);
    assert(v.on_floor && !match_lan_take_jump());
    in={};in.crouch=1;match_update(&in,16,.016f);match_view(&v);
    assert(v.crouching && !v.on_ladder);
    match_lan_stop();
}

extern "C" void test_lan_prediction(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    LanPose pose={{122,121,ground(122,121)},{1,0,0},0,0,1,0,1,0,0};
    match_lan_remote(&pose);
    MatchInput in={};
    match_update(&in,16,.016f);
    assert(bot.x>122.20f && bot.x<122.30f);
    float old=bot.x;
    pose.feet.x=123;pose.velocity={};pose.move=0;
    match_lan_remote(&pose);
    assert(bot.x==old);
    match_update(&in,25,.025f);assert(bot.x>old && bot.x<123);
    match_update(&in,50,.05f);assert(bot.x==123);
    pose.feet.x=140;match_lan_remote(&pose);assert(bot.x==140);
    pose.feet.x=122;pose.on_floor=0;pose.velocity.z=2;
    match_lan_remote(&pose);
    float floor=pose.feet.z;
    for(int i=0;i<10;++i) match_update(&in,5,.005f);
    assert(remote_pose.feet.z>floor+.9f && remote_pose.feet.z<floor+1.1f);
    for(int i=0;i<140;++i) match_update(&in,5,.005f);
    assert(remote_pose.feet.z==floor && remote_pose.on_floor && !remote_air_ms);
    match_lan_stop();
}

extern "C" void test_lan_stance(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    LanPose pose={{122,121,ground(122,121)},{},0,0,0,0,1,0,0};
    MatchInput in={};MatchView v={};
    match_lan_remote(&pose);match_view(&v);
    float standing_head=v.bot_head.z;
    assert(match_lan_sound_id(PLAYER_JUMP)==S_JUMP && match_lan_sound_id(WATER_ENTER)<0);
    match_lan_sound(S_JUMP);match_lan_sound(S_ITEMHELMET);match_lan_sound(S_ASSAULT);
    match_update(&in,16,.016f);match_view(&v);
    assert(v.actor_effect_count==2 && v.actor_effects[0]==PLAYER_JUMP && v.actor_effects[1]==PICKUP_HELMET);
    match_update(&in,16,.016f);match_view(&v);assert(!v.actor_effect_count);
    pose.crouching=1;match_lan_remote(&pose);
    for(int i=0;i<12;++i) {match_update(&in,16,.016f);match_view(&v);}
    assert(v.bot_height==.75f*PLAYER_EYE_HEIGHT && fabsf(v.bot_floor-pose.feet.z)<.001f);
    assert(v.current_pose.first>=135 && v.current_pose.first<154 && v.bot_head.z<standing_head-.5f);
    pose.move=1;match_lan_remote(&pose);match_update(&in,16,.016f);match_view(&v);
    assert(v.current_pose.first>=154 && v.current_pose.first<160);
    pose.crouching=0;pose.on_floor=0;pose.move=0;pose.feet.z+=3;match_lan_remote(&pose);
    for(int i=0;i<5;++i) match_update(&in,16,.016f);
    match_view(&v);assert(v.current_pose.first==68);
    match_lan_stop();
}

extern "C" void test_grenade_pickups(void) {
    match_prepare();begin(PLAYING);
    int index=0;
    while(index<item_count && locations[index].kind!=I_GRENADE) ++index;
    assert(index<item_count);
    const ItemLocation &p=locations[index];
    player.x=p.x;player.y=p.y;feet_z=ground(p.x,p.y)+p.height_tenths*.1f;
    player.state.mag[GUN_GRENADE]=0;
    collect(player);assert(player.state.mag[GUN_GRENADE]==1 && unavailable_until[index]==9000);
    clock_ms=8999;collect(player);assert(player.state.mag[GUN_GRENADE]==1);
    clock_ms=9000;collect(player);assert(player.state.mag[GUN_GRENADE]==2);
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    match_lan_item(p.entity,true,false);
    player.x=p.x;player.y=p.y;feet_z=ground(p.x,p.y)+p.height_tenths*.1f;
    assert(match_lan_touch_item()==p.entity && !player.state.mag[GUN_GRENADE]);
    match_lan_item(p.entity,false,true);
    match_lan_item(p.entity,false,true);
    assert(player.state.mag[GUN_GRENADE]==1);
    match_lan_stop();
}

extern "C" void test_akimbo(void) {
    match_prepare();
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    ammo[GUN_ASSAULT]=40;
    match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    int index=0;
    while(index<item_count && locations[index].kind!=I_AKIMBO) ++index;
    assert(index<item_count);
    int entity=locations[index].entity;
    match_lan_item(entity,true,false);
    assert(match_lan_item(entity,false,true));
    assert(!match_lan_item(entity,false,true));
    MatchInput in={};
    for(int i=0;i<30;++i) match_update(&in,16,.016f);
    assert(player.state.gunselect==GUN_AKIMBO && player.state.mag[GUN_AKIMBO]==20);
    in.fire=1;unsigned shots=0;
    for(int i=0;i<100;++i) {
        match_update(&in,8,.008f);
        if(match_attack(&in)==PISTOL_SHOT) {
            ++shots;
            assert(akimbo_left==(shots%2==0));
        }
    }
    assert(shots==10 && player.state.mag[GUN_AKIMBO]==10);
    in.fire=0;in.refill=1;
    assert(match_attack(&in)==AKIMBO_RELOAD && player.state.mag[GUN_AKIMBO]==20);
    in.refill=0;clock_ms=akimbo_expires-1;
    match_update(&in,1,.001f);
    assert(!player.state.akimbo && next_gun==GUN_ASSAULT && pickup_sound==AKIMBO_END);
    match_lan_stop();
}

extern "C" void test_remote_grenade(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};
    match_prepare();match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    MatchInput in={};MatchView view={};
    player.x=121.8125f;player.y=126.0625f;
    feet_z=ground(player.x,player.y);player.eye_height=feet_z+PLAYER_EYE_HEIGHT;
    place_remote(125,116,ground(125,116),0,0,0);
    match_lan_throw({123.06f,120.46f,.16f},{-.29f,1.28f,-.71f},1148);
    match_view(&view);assert(view.grenade_count==1);
    for(int i=0;i<53;++i) match_update(&in,16,.016f);
    match_view(&view);assert(view.grenade_count==1);
    match_update(&in,16,.016f);match_view(&view);
    assert(!view.grenade_count && view.explosion_count==1 && view.spark_count==50);
    assert(view.impact.sound==GRENADE_EXPLODE && player.state.health==100);
    // Stock Windows throw on ac_desert. The server's 10-damage hit and
    // quantized push vector put the explosion near (120.4,117.0).
    assert(hypotf(view.explosions[0].pos.x-120.4f,view.explosions[0].pos.y-117.f)<.2f);
    match_lan_stop();
}

extern "C" void test_grenade_controls(void) {
    int ammo[NUMGUNS]={},mag[NUMGUNS]={};mag[GUN_GRENADE]=1;
    match_prepare();match_lan_spawn(100,0,GUN_ASSAULT,GUN_ASSAULT,ammo,mag);
    MatchInput in={};in.grenade_select=1;match_update(&in,16,.016f);
    in={};for(int i=0;i<26;++i) match_update(&in,16,.016f);
    assert(match_lan_selected_weapon()==GUN_GRENADE);
    in.fire=1;assert(match_attack(&in)==GRENADE_PULL && player.state.mag[GUN_GRENADE]==0);
    const GrenadeEvent *events;
    assert(match_lan_grenade_events(&events)==1 && events[0].action==NADE_PRIME);
    unsigned id=events[0].id;
    in.fire=0;match_update(&in,300,.016f);assert(match_attack(&in)==SILENT);
    match_update(&in,350,.016f);assert(match_attack(&in)==GRENADE_THROW);
    assert(match_lan_grenade_events(&events)==1 && events[0].action==NADE_RELEASE);
    assert(events[0].id==id && events[0].cooked==650);
    for(int i=0;i<90;++i) {match_update(&in,16,.016f);match_attack(&in);}
    assert(match_lan_grenade_events(&events)==1 && events[0].action==NADE_BLAST && events[0].id==id);
    assert(!active_grenade_count && player.state.gunselect==GUN_ASSAULT && player.state.health==100);
    match_lan_stop();
    begin(PLAYING);player.state.mag[GUN_GRENADE]=1;player.state.gunselect=GUN_GRENADE;
    in.fire=1;assert(match_attack(&in)==GRENADE_PULL);
    for(int i=0;i<128;++i) {match_update(&in,16,.016f);match_attack(&in);}
    assert(player.respawn_at && player.frags==-1);
    MatchView v={};match_view(&v);assert(!strcmp(v.frag_messages[0].text,"you suicided!"));
}

#include "lan.h"
#include "enet_platform.h"
#include <enet/enet.h>
#include <nxdk/net.h>
#include <lwip/netif.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../../source/src/playerstate.h"

extern "C" struct netif *g_pnetif;

namespace {
ENetHost *host;
ENetPeer *peer;
ENetAddress address;
int own=-1,life,remote=-1,remote_life;
bool ready,alive,remote_alive;
bool intermission;
struct GrenadeId { unsigned game,wire; };
GrenadeId grenade_ids[8];
unsigned next_grenade_id;
unsigned last_pos,last_ping,next_spawn,connect_start;
char status[96];
char player_names[MAXCLIENTS][16];
int deaths[MAXCLIENTS],pings[MAXCLIENTS];
struct PrimaryChoice { int gun; const char *config,*label; };
constexpr PrimaryChoice primaries[]={
    {GUN_ASSAULT,"assault","Next spawn: MTP-57 rifle"},
    {GUN_SNIPER,"sniper","Next spawn: AD-81 sniper"},
    {GUN_SUBGUN,"subgun","Next spawn: A-ARD/10 SMG"},
    {GUN_CARBINE,"carbine","Next spawn: TMP-M&A CB carbine"},
    {GUN_SHOTGUN,"shotgun","Next spawn: V-19 CS shotgun"}
};
constexpr unsigned primary_count=sizeof(primaries)/sizeof(primaries[0]);
unsigned primary_choice;
int sent_weapon=-1,remote_weapon=GUN_ASSAULT;
bool supported_primary(int gun) {
    for(const PrimaryChoice &choice:primaries) if(choice.gun==gun) return true;
    return false;
}
unsigned choice_time;
const float degrees=57.2957795f;
int nearest(float value) { return (int)floorf(value + 0.5f); }

struct Reader {
    const unsigned char *data;
    size_t size,bit;
    bool valid=true;
    unsigned bits(unsigned n) {
        if (!valid || n>32 || bit+n>size*8) {valid=false; return 0;}
        unsigned value=0;
        for (unsigned i=0;i<n;i++,bit++)
            value|=unsigned((data[bit/8]>>(bit%8))&1)<<i;
        return value;
    }
    int integer() {
        unsigned b=bits(8);
        if (b==128) return (int16_t)bits(16);
        if (b==129) return (int32_t)bits(32);
        return (int8_t)b;
    }
    unsigned natural() {
        unsigned value=0;
        for(unsigned shift=0;shift<=21;shift+=7) {
            unsigned b=bits(8);
            value|=(shift==21?b:b&127)<<shift;
            if (!(b&128)) break;
        }
        return value;
    }
    void string(char *out,size_t cap) {
        size_t n=0;
        int ch;
        while(valid && (ch=integer())!=0) if(n+1<cap) out[n++]=(char)ch;
        if(cap) out[n]=0;
    }
    void skip(int n) { while(valid && n-->0) integer(); }
};

struct Message {
    unsigned char data[160]; size_t n=0;
    void byte(unsigned x) { data[n++]=(unsigned char)x; }
    void word(int x) {
        if(x>=-126 && x<=127) byte(x);
        else if(x>=-32768 && x<=32767) {byte(128);byte(x);byte((unsigned)x>>8);}
        else {byte(129); for(int i=0;i<4;i++) byte((unsigned)x>>(8*i));}
    }
    void natural(unsigned x) {
        while(x>=128) {byte(128|(x&127));x>>=7;}
        byte(x);
    }
    void string(const char *s) {while(*s) word(*s++);word(0);}
    void transmit(int channel=1) {
        ENetPacket *p=enet_packet_create(data,n,channel?ENET_PACKET_FLAG_RELIABLE:0);
        if(p && enet_peer_send(peer,channel,p)<0) enet_packet_destroy(p);
    }
};
void command(int cmd) {Message m; m.word(cmd);m.transmit();}
void stop(const char *why) {
    snprintf(status,sizeof(status),"LAN: %s",why);
    if(peer) enet_peer_reset(peer);
    peer=nullptr;ready=alive=false;own=remote=-1;
    match_lan_stop();
}
void positions(Reader &r) {
    while(r.valid && r.bit<r.size*8) {
        int type=r.integer(),cn,x,y,z,yaw,pitch,flags;
        MatchPoint velocity={};
        if(type==SV_POS) {
            cn=r.integer();x=r.natural();y=r.natural();z=r.natural();
            unsigned packed=r.bits(32); flags=packed&2047;pitch=(packed>>11)&255;yaw=(packed>>19)&1023;
            if(flags&512) velocity={r.integer()/DVELF,r.integer()/DVELF,r.integer()/DVELF};
        } else if(type>=SV_POSC && type<=SV_POSC4) {
            int width=11+type-SV_POSC;
            cn=r.bits(5); x=r.bits(width);y=r.bits(width);yaw=r.bits(YAWBITS);
            pitch=r.bits(PITCHBITS);flags=r.bits(FLAGBITS);
            if(flags&512) velocity={((int)r.bits(4)-8)/DVELF,((int)r.bits(4)-8)/DVELF,((int)r.bits(4)-8)/DVELF};
            bool full=r.bits(1);
            int width_z=(8-r.bit%8)%8;
            if(width_z<3) width_z+=8;
            z=r.bits(full?11:width_z);
            r.bit=(r.bit+7)&~size_t(7);
        } else return;
        if(!r.valid || cn==own || cn<0 || cn>=MAXCLIENTS || x<0 || x>=MAP*16 || y<0 || y>=MAP*16) continue;
        if(flags&1024) z=-z;
        if(cn!=remote || !remote_alive || ((flags>>6)&1)!=(remote_life&1)) continue;
        float angle=(yaw*360.f/1024-90)/degrees;
        float tilt=(pitch-128)/256.f;
        if(tilt>1.f/3) tilt=4*tilt-1;
        if(tilt< -1.f/3) tilt=4*tilt+1;
        int movement=flags&31;
        LanPose pose={{x/DMF,y/DMF,z/DMF},velocity,angle,tilt*90/degrees,
            movement/3%3-1,movement%3-1,(flags&128)!=0,movement/9==1,movement/9==2};
        match_lan_remote(&pose);
        snprintf(status,sizeof(status),"LAN  ac_desert    PC #%d",cn);
    }
}
void messages(Reader &r,unsigned now,int sender=-1) {
    while(peer && r.valid && r.bit<r.size*8) {
        int type=r.integer();
        Message m;
        switch(type) {
        case SV_SERVINFO: {
            own=r.integer();
            int version=r.integer(),auth=r.integer(),password=r.integer();
            if(!r.valid || version!=PROTOCOL_VERSION || auth || password) {stop("requires a compatible open LAN server");return;}
            m.word(SV_SERVINFO_RESPONSE);m.word(now);m.word(time(nullptr)/60);m.word(address.port);
            unsigned ip=lwip_ntohl(address.host);
            for(int i=0;i<4;i++) m.byte(ip>>(i*8));
            m.word(0);m.transmit();return;
        }
        case SV_SERVINFO_CONTD:
            if(r.integer()!=0 || !r.valid) {stop("unsupported authenticated server");return;}
            m.word(SV_CONNECT);m.word(1302);m.word(0);m.string("Xbox");m.string("");m.string("");
            m.word(0);m.word(primaries[primary_choice].gun);
            m.word(0);m.word(0);m.word(0);m.word(0);m.word(90);m.word(50);
            m.transmit();return;
        case SV_WELCOME:r.skip(1);break;
        case SV_MAPCHANGE: {
            char map[48];r.string(map,sizeof(map));
            int mode=r.integer();r.skip(3);
            if(!r.valid || strcmp(map,"ac_desert") || mode!=GMODE_DEATHMATCH) {stop("requires ac_desert deathmatch");return;}
            memset(grenade_ids,0,sizeof(grenade_ids));
            memset(deaths,0,sizeof(deaths));
            ready=true;alive=remote_alive=intermission=false;
            remote_weapon=GUN_ASSAULT;match_lan_map(now);
            // Size and map revision of the bundled official ac_desert.cgz.
            m.word(SV_MAPIDENT);m.word(12629);m.word(19);m.transmit();
            next_spawn=now+1200;
            snprintf(status,sizeof(status),"LAN  ac_desert    Joining");break;
        }
        case SV_SPAWNSTATE: {
            life=r.integer();int hp=r.integer(),armour=r.integer();int primary=r.integer(),gun=r.integer();r.integer();
            int ammo[NUMGUNS],mag[NUMGUNS];
            for(int &value:ammo) value=r.integer();
            for(int &value:mag) value=r.integer();
            if(!r.valid || !ready || !supported_primary(primary) ||
               (gun!=primary && gun!=GUN_PISTOL)) return;
            m.word(SV_SPAWN);m.word(life);m.word(gun);m.transmit();
            sent_weapon=gun;alive=true;
            match_lan_spawn(hp,armour,primary,gun,ammo,mag);
            match_lan_remote_weapon(remote_weapon);
            snprintf(status,sizeof(status),"LAN  ac_desert    Waiting for PC");break;
        }
        case SV_SPAWN:
            if(sender<0) return;
            remote=sender;remote_life=r.integer();r.skip(2);remote_weapon=r.integer();
            r.skip(2*NUMGUNS);
            if(r.valid) {
                remote_alive=true;
                match_lan_remote_spawn(remote_weapon);
            }
            break;
        case SV_RESUME:
            while(r.valid) {
                int cn=r.integer();if(cn<0) break;
                int state=r.integer(),seq=r.integer();r.integer();int gun=r.integer();
                r.integer();int frags=r.integer(),lost=r.integer();r.skip(3+2*NUMGUNS);
                if(!r.valid || cn>=MAXCLIENTS) return;
                deaths[cn]=lost;
                match_lan_score(cn==own,frags);
                if(cn!=own) {
                    remote=cn;remote_life=seq;remote_weapon=gun;
                    // entity.h orders alive, dead, spawning, lagged, editing, spectator.
                    remote_alive=state==0 || state==2 || state==3;
                    if(remote_alive) match_lan_remote_spawn(gun);
                    else match_lan_hide();
                }
            }
            break;
        case SV_CLIENT: {
            int cn=r.integer();unsigned len=r.natural();
            if(!r.valid || len>r.size-r.bit/8 || sender>=0 || cn<0 || cn>=MAXCLIENTS) return;
            Reader sub{r.data+r.bit/8,len,0};r.bit+=len*8;
            messages(sub,now,cn);break;
        }
        case SV_DAMAGE:case SV_GIBDAMAGE:{
            int victim=r.integer(),actor=r.integer(),gun=r.integer(),damage=r.integer();
            int armour=r.integer(),hp=r.integer();
            if(r.valid && gun>=0 && gun<NUMGUNS && damage>=0 && (victim==own || victim==remote))
                match_lan_damage(victim==own,actor==remote,damage,hp,armour);
            break;
        }
        case SV_GIBDIED:case SV_DIED:{
            int victim=r.integer(),actor=r.integer(),frags=r.integer(),gun=r.integer();
            if(!r.valid) return;
            if(victim>=0 && victim<MAXCLIENTS) ++deaths[victim];
            if((victim==own || victim==remote) && (actor==own || actor==remote) && gun>=0 && gun<NUMGUNS) {
                const char *name=remote>=0 && player_names[remote][0]?player_names[remote]:"PC";
                match_lan_frag(victim==own,victim==actor,gun,type==SV_GIBDIED,name);
            }
            if(actor==own || actor==remote) match_lan_score(actor==own,frags);
            if(victim==own) {alive=false;next_spawn=now+2200;match_lan_health(0,0);}
            if(victim==remote) {remote_alive=false;match_lan_remote_death(type==SV_GIBDIED);}
            break;
        }
        case SV_CDIS: {
            int cn=r.integer();
            if(cn>=0 && cn<MAXCLIENTS) {deaths[cn]=pings[cn]=0;player_names[cn][0]=0;}
            if(cn==remote) {remote=-1;match_lan_hide();snprintf(status,sizeof(status),"LAN  ac_desert    PC left");}
            break;
        }
        case SV_FORCEDEATH: {
            int cn=r.integer();
            if(cn==own) alive=false;
            if(cn==remote) {remote_alive=false;match_lan_hide();}
            break;
        }
        case SV_SPAWNDENY:if(r.integer()==SP_WRONGMAP){stop("map revision mismatch");return;}break;
        case SV_ITEMLIST:
            match_lan_reset_items();
            while(r.valid) {
                int entity=r.integer();
                if(!r.valid || entity<0) break;
                match_lan_item(entity,true,false);
            }
            break;
        case SV_ITEMSPAWN: {
            int entity=r.integer();
            if(r.valid) match_lan_item(entity,true,false);
            break;
        }
        case SV_ITEMACC: {
            int entity=r.integer(),picker=r.integer();
            if(r.valid) match_lan_item(entity,false,picker==own);
            break;
        }
        case SV_INITCLIENT:case SV_SWITCHNAME: {
            int cn=type==SV_INITCLIENT?r.integer():sender;
            char name[16];r.string(name,sizeof(name));
            if(type==SV_INITCLIENT) r.skip(8);
            if(r.valid && cn>=0 && cn<MAXCLIENTS) {
                unsigned n=0;
                for(const char *ch=name;*ch;++ch) if(*ch>=32 && *ch<=126) player_names[cn][n++]=*ch;
                player_names[cn][n]=0;
            }
            break;
        }
        case SV_TIMEUP:{
            int elapsed=r.integer(),limit=r.integer();
            if(r.valid && elapsed>=0 && limit>0) {
                intermission=elapsed>=limit;
                match_lan_time(intermission?0:limit-elapsed);
            }
            break;
        }
        case SV_SETTEAM:r.skip(2);break;
        case SV_PONG:{
            unsigned sent=r.integer(),latency=now-sent;
            if(r.valid && latency<=10000) {
                if(own>=0 && own<MAXCLIENTS) pings[own]=latency;
                m.word(SV_CLIENTPING);m.word(latency);m.transmit();
            }
            break;
        }
        case SV_SHOTFX:{
            int shooter=r.integer(),gun=r.integer();
            int x=r.integer(),y=r.integer(),z=r.integer();
            if(r.valid && shooter==remote && gun>=0 && gun<NUMGUNS)
                match_lan_shot(gun,x/DMF,y/DMF,z/DMF);
            break;
        }
        case SV_THROWNADE: {
            MatchPoint from={r.integer()/DNF,r.integer()/DNF,r.integer()/DNF};
            MatchPoint velocity={r.integer()/DNF,r.integer()/DNF,r.integer()/DNF};
            int cooked=r.integer();
            if(r.valid && sender==remote) match_lan_throw(from,velocity,cooked);
            break;
        }
        case SV_HITPUSH: {
            int gun=r.integer(),damage=r.integer();
            int x=r.integer(),y=r.integer(),z=r.integer();
            if(r.valid && alive && sender<0 && gun>=0 && gun<NUMGUNS)
                match_lan_hitpush(gun,damage,x/DNF,y/DNF,z/DNF);
            break;
        }
        case SV_WEAPCHANGE: {
            int gun=r.integer();
            if(r.valid && sender>=0 && sender!=own && gun>=0 && gun<NUMGUNS) {
                remote_weapon=gun;match_lan_remote_weapon(gun);
            }
            break;
        }
        case SV_RELOAD: {
            int cn=r.integer(),gun=r.integer();
            if(r.valid && cn==remote && gun>=0 && gun<NUMGUNS) match_lan_remote_reload(gun);
            break;
        }
        case SV_SERVMSG:case SV_SERVMSGVERB:r.string(nullptr,0);break;
        case SV_CLIENTPING:{
            int ping=r.integer();
            if(r.valid && sender>=0 && sender<MAXCLIENTS) pings[sender]=ping;
            break;
        }
        case SV_SOUND:case SV_VOICECOM:case SV_PAUSEMODE:case SV_SERVERMODE:r.skip(1);break;
        default:return; // Unknown message may have variable length; discard this packet's remainder.
        }
    }
}
}

void lan_init(void) {
    FILE *f=fopen("D:\\lan-server.txt","r");if(!f)return;
    char text[64];bool read=fgets(text,sizeof(text),f)!=nullptr;
    char primary[32];
    if (fgets(primary,sizeof(primary),f)) {
        primary[strcspn(primary," \t\r\n")]=0;
        for(unsigned i=0;i<primary_count;++i)
            if(!strcmp(primary,primaries[i].config)) primary_choice=i;
    }
    fclose(f);
    text[strcspn(read?text:""," \t\r\n")]=0;
    if(!read || !ip4addr_aton(text,(ip4_addr_t *)&address.host)) {stop("invalid lan-server.txt");return;}
    address.port=CUBE_DEFAULT_SERVER_PORT;
    if(nxNetInit(nullptr)<0) {stop("network setup failed");return;}
    host=enet_host_create(nullptr,1,3,0,0);
    if(!host) {stop("cannot open socket");return;}
    snprintf(status,sizeof(status),"LAN  Connecting to %s",text);
}
const char *lan_status(void) {
    if(choice_time)
        return primaries[primary_choice].label;
    return status[0]?status:nullptr;
}
int lan_connected(void) {return ready;}
int lan_scores(LanScore rows[2],const MatchView *v) {
    if(!ready || own<0 || own>=MAXCLIENTS) return 0;
    rows[0]={"Xbox",v->player_frags,deaths[own],pings[own]};
    if(remote<0 || remote>=MAXCLIENTS) return 1;
    rows[1]={player_names[remote][0]?player_names[remote]:"PC",v->bot_frags,deaths[remote],pings[remote]};
    if(rows[1].frags>rows[0].frags || (rows[1].frags==rows[0].frags && rows[1].deaths<rows[0].deaths)) {
        LanScore first=rows[0];rows[0]=rows[1];rows[1]=first;
    }
    return 2;
}
void lan_toggle_primary(unsigned now) {
    if (!ready || !peer) return;
    primary_choice=(primary_choice+1)%primary_count;
    Message m;
    m.word(SV_PRIMARYWEAP); m.word(primaries[primary_choice].gun); m.transmit();
    choice_time=now;
}
void lan_poll(unsigned now) {
    if(choice_time && now-choice_time>=2500) choice_time=0;
    if(!host)return;
    if(!peer) {
        if(connect_start || !netif_is_up(g_pnetif) || ip4_addr_isany_val(*netif_ip4_addr(g_pnetif))) return;
        connect_start=now?now:1;peer=enet_host_connect(host,&address,3,0);
        if(!peer) {stop("connection failed");return;}
    }
    ENetEvent ev;
    for(int i=0;i<32 && peer && enet_host_service(host,&ev,0)>0;i++) {
        if(ev.type==ENET_EVENT_TYPE_DISCONNECT){stop("disconnected");return;}
        if(ev.type==ENET_EVENT_TYPE_RECEIVE) {
            Reader r{ev.packet->data,ev.packet->dataLength,0};
            if(ev.channelID==0 && ready) positions(r);
            if(ev.channelID==1) messages(r,now);
            enet_packet_destroy(ev.packet);
        }
    }
    if(!peer)return;
    if(!ready && now-connect_start>18000) {stop("server timed out");return;}
    if(ready && !intermission && !alive && (int)(now-next_spawn)>=0) {command(SV_TRYSPAWN);next_spawn=now+1500;}
    if(ready && now-last_ping>=500){Message m;m.word(SV_PING);m.word(now);m.transmit();last_ping=now;}
}

static void send_grenades(unsigned now) {
    const GrenadeEvent *events;
    int count=match_lan_grenade_events(&events);
    for(int i=0;i<count;++i) {
        const GrenadeEvent &e=events[i];
        Message m;
        if(e.action==NADE_PRIME) {
            // The game clock can pause or restart. Retain the original wire ID
            // until the explosion, even when the shooter has already respawned.
            grenade_ids[next_grenade_id++%8]={e.id,now};
            m.word(SV_SHOOT);m.word(now);m.word(GUN_GRENADE);
            m.word(0);m.word(0);m.word(0);m.word(0);
        } else if(e.action==NADE_RELEASE) {
            m.word(SV_THROWNADE);
            m.word(nearest(e.position.x*DNF));m.word(nearest(e.position.y*DNF));m.word(nearest(e.position.z*DNF));
            m.word(nearest(e.velocity.x*DNF));m.word(nearest(e.velocity.y*DNF));m.word(nearest(e.velocity.z*DNF));m.word(e.cooked);
        } else {
            unsigned id=0;
            for(GrenadeId &g:grenade_ids) if(g.game==e.id && g.wire) {id=g.wire;g={};break;}
            if(!id) continue;
            m.word(SV_EXPLODE);m.word(now);m.word(GUN_GRENADE);m.word(id);m.word(e.hit_count);
            for(int j=0;j<e.hit_count;++j) {
                const GrenadeHit &h=e.hits[j];
                m.word(h.local?own:remote);m.word(h.local?life:remote_life);m.word((int)(h.distance*DMF));
                MatchPoint dir=h.local?MatchPoint{}:h.direction;
                m.word((int)(dir.x*DNF));m.word((int)(dir.y*DNF));m.word((int)(dir.z*DNF));
            }
        }
        m.transmit();
    }
}
void lan_send(const MatchView *v,const MatchInput *input,MatchSound effect,unsigned now) {
    if(!peer || !ready || intermission)return;
    send_grenades(now);
    if(!alive)return;
    if(now-last_pos>=40) {
        last_pos=now;Message m;m.word(SV_POS);m.word(own);
        m.natural(nearest(v->px*DMF));m.natural(nearest(v->py*DMF));
        int z=nearest(v->player_floor*DMF);m.natural(abs(z));
        int yaw=nearest((v->angle*degrees+90)*1024/360)&1023;
        float pitch=v->pitch*degrees/90;
        if(pitch>1.f/3) pitch=pitch*.25f+.25f;
        if(pitch< -1.f/3) pitch=pitch*.25f-.25f;
        int pi=nearest((pitch+.5f)*256)&255;
        int move=(input->move>.1f)-(input->move<-.1f),strafe=(input->strafe>.1f)-(input->strafe<-.1f);
        // The stock strafe flag is positive to the left. Xbox input is positive to the right.
        int movement=4+move*3-strafe+(v->on_ladder?9:v->crouching?18:0);
        int vx=nearest(v->player_velocity.x*DVELF),vy=nearest(v->player_velocity.y*DVELF);
        int vz=nearest(v->player_velocity.z*DVELF);
        bool has_velocity=vx || vy || vz;
        unsigned packed=(unsigned(yaw)<<19)|(pi<<11)|movement|(v->scoped?32:0)|
            ((life&1)<<6)|(v->on_floor?128:0)|(match_lan_take_jump()?256:0)|
            (has_velocity?512:0)|(z<0?1024:0);
        for(int i=0;i<4;i++)m.byte(packed>>(8*i));
        if(has_velocity) {m.word(vx);m.word(vy);m.word(vz);}
        m.transmit(0);
    }
    int item=match_lan_touch_item();
    if(item>=0) {Message m;m.word(SV_ITEMPICKUP);m.word(item);m.transmit();}
    int gun=match_lan_selected_weapon();
    if(gun<0) return;
    if(gun!=sent_weapon) {
        Message change;change.word(SV_WEAPCHANGE);change.word(gun);change.transmit();
        sent_weapon=gun;
    }
    if(effect==KNIFE_SWING || effect==RIFLE_SHOT || effect==PISTOL_SHOT || effect==SNIPER_SHOT || effect==SMG_SHOT || effect==CARBINE_SHOT || effect==SHOTGUN_SHOT) {
        Message m;m.word(SV_SHOOT);m.word(match_lan_shot_time(now));m.word(gun);
        MatchPoint dir=v->shot_direction;
        m.word(v->shot_end.x*DMF);m.word(v->shot_end.y*DMF);m.word(v->shot_end.z*DMF);
        bool hit=remote>=0 && v->shot_contact;
        m.word(hit);
        if(hit){m.word(remote);m.word(remote_life);m.word(gun==GUN_SHOTGUN?v->shotgun_info:(gun==GUN_KNIFE || (gun==GUN_SNIPER && v->shot_contact==2)));
            m.word(dir.x*DNF);m.word(dir.y*DNF);m.word(dir.z*DNF);}
        m.transmit();
    }
    if(effect==RIFLE_RELOAD || effect==PISTOL_RELOAD || effect==SNIPER_RELOAD || effect==SMG_RELOAD || effect==CARBINE_RELOAD || effect==SHOTGUN_RELOAD){Message m;m.word(SV_RELOAD);m.word(now);m.word(gun);m.transmit();}
}



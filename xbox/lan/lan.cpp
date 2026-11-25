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
bool ready,alive;
unsigned last_pos,last_ping,next_spawn,connect_start;
char status[96];
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
        if(type==SV_POS) {
            cn=r.integer();x=r.natural();y=r.natural();z=r.natural();
            unsigned packed=r.bits(32); flags=packed&2047;pitch=(packed>>11)&255;yaw=(packed>>19)&1023;
            if(flags&512) r.skip(3);
        } else if(type>=SV_POSC && type<=SV_POSC4) {
            int width=11+type-SV_POSC;
            cn=r.bits(5); x=r.bits(width);y=r.bits(width);yaw=r.bits(YAWBITS);
            pitch=r.bits(PITCHBITS);flags=r.bits(FLAGBITS);
            if(flags&512) r.bits(12);
            bool full=r.bits(1);
            int width_z=(8-r.bit%8)%8;
            if(width_z<3) width_z+=8;
            z=r.bits(full?11:width_z);
            r.bit=(r.bit+7)&~size_t(7);
        } else return;
        if(!r.valid || cn==own || cn<0 || cn>=MAXCLIENTS || x<0 || x>=MAP*16 || y<0 || y>=MAP*16) continue;
        if(flags&1024) z=-z;
        remote=cn;
        float angle=(yaw*360.f/1024-90)/degrees;
        float tilt=(pitch-128)/256.f;
        if(tilt>1.f/3) tilt=4*tilt-1;
        if(tilt< -1.f/3) tilt=4*tilt+1;
        match_lan_remote(x/DMF,y/DMF,z/DMF,angle,tilt*90/degrees,(flags&31)%9!=4);
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
            m.word(0);m.word(GUN_ASSAULT);
            m.word(0);m.word(0);m.word(0);m.word(0);m.word(100);m.word(40);
            m.transmit();return;
        case SV_WELCOME:r.skip(1);break;
        case SV_MAPCHANGE: {
            char map[48];r.string(map,sizeof(map));
            int mode=r.integer();r.skip(3);
            if(!r.valid || strcmp(map,"ac_desert") || mode!=GMODE_DEATHMATCH) {stop("requires ac_desert deathmatch");return;}
            ready=true;alive=false;remote=-1;match_lan_hide();
            // Size and map revision of the bundled official ac_desert.cgz.
            m.word(SV_MAPIDENT);m.word(12629);m.word(19);m.transmit();
            next_spawn=now+1200;
            snprintf(status,sizeof(status),"LAN  ac_desert    Joining");break;
        }
        case SV_SPAWNSTATE: {
            life=r.integer();int hp=r.integer(),armour=r.integer();r.integer();int gun=r.integer();r.integer();
            r.skip(2*NUMGUNS);
            if(!r.valid || !ready) return;
            m.word(SV_SPAWN);m.word(life);m.word(gun);m.transmit();
            alive=true;match_lan_spawn(hp,armour);
            snprintf(status,sizeof(status),"LAN  ac_desert    Waiting for PC");break;
        }
        case SV_SPAWN:
            if(sender<0) return;
            remote=sender;remote_life=r.integer();r.skip(3+2*NUMGUNS);break;
        case SV_RESUME:
            while(r.valid) {
                int cn=r.integer();if(cn<0) break;
                r.integer();int seq=r.integer();r.skip(8+2*NUMGUNS);
                if(cn!=own){remote=cn;remote_life=seq;}
            }
            break;
        case SV_CLIENT: {
            int cn=r.integer();unsigned len=r.natural();
            if(!r.valid || len>r.size-r.bit/8 || sender>=0 || cn<0 || cn>=MAXCLIENTS) return;
            Reader sub{r.data+r.bit/8,len,0};r.bit+=len*8;
            messages(sub,now,cn);break;
        }
        case SV_DAMAGE:case SV_GIBDAMAGE:{
            int victim=r.integer();r.skip(3);int armour=r.integer(),hp=r.integer();
            if(r.valid && victim==own) match_lan_health(hp,armour);
            break;
        }
        case SV_GIBDIED:case SV_DIED:{
            int victim=r.integer(),actor=r.integer(),frags=r.integer();r.integer();
            if(!r.valid) return;
            if(actor==own || actor==remote) match_lan_score(actor==own,frags);
            if(victim==own) {alive=false;next_spawn=now+2200;match_lan_health(0,0);}
            if(victim==remote) match_lan_hide();
            break;
        }
        case SV_CDIS: if(r.integer()==remote) {remote=-1;match_lan_hide();snprintf(status,sizeof(status),"LAN  ac_desert    PC left");} break;
        case SV_FORCEDEATH: if(r.integer()==own) alive=false;break;
        case SV_SPAWNDENY:if(r.integer()==SP_WRONGMAP){stop("map revision mismatch");return;}break;
        case SV_ITEMLIST:while(r.valid && r.integer()!=-1) {} break;
        case SV_INITCLIENT:r.integer();r.string(nullptr,0);r.skip(8);break;
        case SV_TIMEUP:{
            int elapsed=r.integer(),limit=r.integer();
            if(r.valid && elapsed>=0 && limit>=elapsed) match_lan_time(limit-elapsed);
            break;
        }
        case SV_SETTEAM:r.skip(2);break;
        case SV_PONG:case SV_PAUSEMODE:case SV_SERVERMODE:case SV_CLIENTPING:case SV_WEAPCHANGE:r.skip(1);break;
        default:return; // Unknown message may have variable length; discard this packet's remainder.
        }
    }
}
}

void lan_init(void) {
    FILE *f=fopen("D:\\lan-server.txt","r");if(!f)return;
    char text[64];bool read=fgets(text,sizeof(text),f)!=nullptr;fclose(f);
    text[strcspn(read?text:""," \t\r\n")]=0;
    if(!read || !ip4addr_aton(text,(ip4_addr_t *)&address.host)) {stop("invalid lan-server.txt");return;}
    address.port=CUBE_DEFAULT_SERVER_PORT;
    if(nxNetInit(nullptr)<0) {stop("network setup failed");return;}
    host=enet_host_create(nullptr,1,3,0,0);
    if(!host) {stop("cannot open socket");return;}
    snprintf(status,sizeof(status),"LAN  Connecting to %s",text);
}
const char *lan_status(void) {return status[0]?status:nullptr;}
int lan_connected(void) {return ready;}
void lan_poll(unsigned now) {
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
    if(ready && !alive && (int)(now-next_spawn)>=0) {command(SV_TRYSPAWN);next_spawn=now+1500;}
    if(ready && now-last_ping>=500){Message m;m.word(SV_PING);m.word(now);m.transmit();last_ping=now;}
}

void lan_send(const MatchView *v,const MatchInput *input,MatchSound effect,unsigned now) {
    if(!alive || !peer || !ready)return;
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
        unsigned packed=(yaw<<19)|(pi<<11)|(4+move*3+strafe+(input->crouch?18:0))|(life%2<<6)|128|(z<0?1024:0);
        for(int i=0;i<4;i++)m.byte(packed>>(8*i));
        m.transmit(0);
    }
    if(effect==RIFLE_SHOT) {
        Message m;m.word(SV_SHOOT);m.word(now);m.word(GUN_ASSAULT);
        float c=cosf(v->pitch),dx=cosf(v->angle)*c,dy=sinf(v->angle)*c,dz=sinf(v->pitch);
        m.word((int)((v->px+dx*100)*DMF));m.word((int)((v->py+dy*100)*DMF));m.word((int)((v->eye+dz*100)*DMF));
        bool hit=remote>=0 && !v->hide_bot && match_targeted(v);
        m.word(hit);
        if(hit){m.word(remote);m.word(remote_life);m.word(1);m.word(dx*DNF);m.word(dy*DNF);m.word(dz*DNF);}
        m.transmit();
    }
    if(effect==RIFLE_RELOAD){Message m;m.word(SV_RELOAD);m.word(now);m.word(GUN_ASSAULT);m.transmit();}
}



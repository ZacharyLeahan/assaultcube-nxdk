#ifndef AC_PLAYERSTATE_H
#define AC_PLAYERSTATE_H

#include <limits.h>
#include <stddef.h>
#include "protocol.h"

enum                            // static entity types
{
    NOTUSED = 0,                // entity slot not in use in map (usually seen at deleted entities)
    LIGHT,                      // lightsource, attr1 = radius, attr2 = intensity (or attr2..4 = r-g-b)
    PLAYERSTART,                // attr1 = angle, attr2 = team
    I_CLIPS, I_AMMO, I_GRENADE, // attr1 = elevation
    I_HEALTH, I_HELMET, I_ARMOUR, I_AKIMBO,
    MAPMODEL,                   // attr1 = angle, attr2 = idx, attr3 = elevation, attr4 = texture, attr5 = pitch, attr6 = roll
    CARROT,                     // attr1 = tag, attr2 = type
    LADDER,                     // attr1 = height
    CTF_FLAG,                   // attr1 = angle, attr2 = red/blue
    SOUND,                      // attr1 = idx, attr2 = radius, attr3 = size, attr4 = volume
    CLIP,                       // attr1 = elevation, attr2 = xradius, attr3 = yradius, attr4 = height, attr6 = slope, attr7 = shape
    PLCLIP,                     // attr1 = elevation, attr2 = xradius, attr3 = yradius, attr4 = height, attr6 = slope, attr7 = shape
    DUMMYENT,                   // temporary entity without any function - will not be saved to map files, used to mark positions and for scripting
    MAXENTTYPES
};

enum { GUN_KNIFE = 0, GUN_PISTOL, GUN_CARBINE, GUN_SHOTGUN, GUN_SUBGUN, GUN_SNIPER, GUN_ASSAULT, GUN_GRENADE, GUN_AKIMBO, NUMGUNS };
#define valid_weapon(g) ((g) >= GUN_KNIFE && (g) < NUMGUNS)
#define reloadable_gun(g) ((g) != GUN_KNIFE && (g) != GUN_GRENADE)

#define SGRAYS 21
#define SGDMGTOTAL 90

#define SGDMGBONUS 65
#define SGDMGDISTB 50

#define SGCCdmg 500
#define SGCCbase 0
#define SGCCrange 40

#define SGCMdmg 375
#define SGCMbase 25
#define SGCMrange 60

#define SGCOdmg 125
#define SGCObase 45
#define SGCOrange 75

#define SGMAXDMGABS 105
#define SGMAXDMGLOC 84
#define EXPDAMRAD 10

struct itemstat { int add, start, max, sound; };
extern itemstat ammostats[NUMGUNS];
extern itemstat powerupstats[I_ARMOUR-I_HEALTH+1];

struct guninfo { char modelname[23], title[42]; short sound, reload, reloadtime, attackdelay, damage, piercing, projspeed, part, spread, recoil, magsize, mdl_kick_rot, mdl_kick_back, recoilincrease, recoilbase, maxrecoil, recoilbackfade, pushfactor; bool isauto; };
extern guninfo guns[NUMGUNS];
extern const char *gunnames[];

static inline int reloadtime(int gun) { return guns[gun].reloadtime; }
static inline int attackdelay(int gun) { return guns[gun].attackdelay; }
static inline int magsize(int gun) { return guns[gun].magsize; }

class playerstate
{
public:
    int health, armour;
    int primary, nextprimary;
    int gunselect;
    bool akimbo;
    int ammo[NUMGUNS], mag[NUMGUNS], gunwait[NUMGUNS];
    int pstatshots[NUMGUNS], pstatdamage[NUMGUNS];

    playerstate() : armour(0), primary(GUN_ASSAULT), nextprimary(GUN_ASSAULT), akimbo(false) {}
    virtual ~playerstate() {}

    void resetstats() { for(int i = 0; i < NUMGUNS; ++i) pstatshots[i] = pstatdamage[i] = 0; }

    itemstat *itemstats(int type)
    {
        switch(type)
        {
            case I_CLIPS:   return &ammostats[GUN_PISTOL];
            case I_AMMO:    return &ammostats[primary];
            case I_GRENADE: return &ammostats[GUN_GRENADE];
            case I_AKIMBO:  return &ammostats[GUN_AKIMBO];
            case I_HEALTH:
            case I_HELMET:
            case I_ARMOUR:
                return &powerupstats[type - I_HEALTH];
            default:
                return NULL;
        }
    }

    bool canpickup(int type)
    {
        switch(type)
        {
            case I_CLIPS: return ammo[akimbo && ammo[GUN_AKIMBO]<ammostats[GUN_AKIMBO].max ? GUN_AKIMBO : GUN_PISTOL]<ammostats[akimbo && ammo[GUN_AKIMBO]<ammostats[GUN_AKIMBO].max ? GUN_AKIMBO : GUN_PISTOL].max;
            case I_AMMO: return ammo[primary]<ammostats[primary].max;
            case I_GRENADE: return mag[GUN_GRENADE]<ammostats[GUN_GRENADE].max;
            case I_HEALTH: return health<powerupstats[type-I_HEALTH].max;
            case I_HELMET:
            case I_ARMOUR: return armour<powerupstats[type-I_HEALTH].max;
            case I_AKIMBO: return !akimbo;
            default: return false;
        }
    }

    void additem(itemstat &is, int &v)
    {
        v += is.add;
        if(v > is.max) v = is.max;
    }

    void pickup(int type)
    {
        switch(type)
        {
            case I_CLIPS:
                additem(ammostats[GUN_PISTOL], ammo[GUN_PISTOL]);
                additem(ammostats[GUN_AKIMBO], ammo[GUN_AKIMBO]);
                break;
            case I_AMMO: additem(ammostats[primary], ammo[primary]); break;
            case I_GRENADE: additem(ammostats[GUN_GRENADE], mag[GUN_GRENADE]); break;
            case I_HEALTH: additem(powerupstats[type-I_HEALTH], health); break;
            case I_HELMET:
            case I_ARMOUR:
                additem(powerupstats[type-I_HEALTH], armour); break;
            case I_AKIMBO:
                akimbo = true;
                mag[GUN_AKIMBO] = guns[GUN_AKIMBO].magsize;
                additem(ammostats[GUN_AKIMBO], ammo[GUN_AKIMBO]);
                break;
        }
    }

    void respawn()
    {
        health = 100;
        armour = 0;
        gunselect = GUN_PISTOL;
        akimbo = false;
        for(int i = 0; i < NUMGUNS; ++i) ammo[i] = mag[i] = gunwait[i] = 0;
        ammo[GUN_KNIFE] = mag[GUN_KNIFE] = 1;
    }

    virtual void spawnstate(int gamemode)
    {
        if(m_pistol) primary = GUN_PISTOL;
        else if(m_osok) primary = GUN_SNIPER;
        else if(m_lss) primary = GUN_KNIFE;
        else primary = nextprimary;

        if(!m_nopistol)
        {
            ammo[GUN_PISTOL] = ammostats[GUN_PISTOL].start-magsize(GUN_PISTOL);//ammostats[GUN_PISTOL].max-magsize(GUN_PISTOL);
            mag[GUN_PISTOL] = magsize(GUN_PISTOL);
        }

        if(!m_noprimary)
        {
            ammo[primary] = ammostats[primary].start-magsize(primary);
            mag[primary] = magsize(primary);
        }

        gunselect = primary;

        if(m_osok) health = 1;
        if(m_lms) // Survivor && Team-Survivor : 2010nov19
        {
            health = 100;
            armour = 100;
            ammo[GUN_GRENADE] = 2;
        }
    }

    // just subtract damage here, can set death, etc. later in code calling this
    int dodamage(int damage, int gun)
    {
        guninfo gi = guns[gun];
        if(damage == INT_MAX)
        {
            damage = health;
            armour = health = 0;
            return damage;
        }

        // 4-level armour - tiered approach: 16%, 33%, 37%, 41%
        // Please update ./ac_website/htdocs/docs/introduction.html if this changes.
        int armoursection = 0;
        int ad = damage;
        if(armour > 25) armoursection = 1;
        if(armour > 50) armoursection = 2;
        if(armour > 75) armoursection = 3;
        switch(armoursection)
        {
            case 0: ad = (int) (16.0f/25.0f * armour); break;             // 16
            case 1: ad = (int) (17.0f/25.0f * armour) - 1; break;         // 33
            case 2: ad = (int) (4.0f/25.0f * armour) + 25; break;         // 37
            case 3: ad = (int) (4.0f/25.0f * armour) + 25; break;         // 41
            default: break;
        }

        //ra - reduced armor
        //rd - reduced damage
        int ra = (int) (ad * damage/100.0f);
        int rd = ra-(ra*(gi.piercing/100.0f)); //Who cares about rounding errors anyways?

        armour -= ra;
        damage -= rd;

        health -= damage;
        return damage;
    }
};

#endif

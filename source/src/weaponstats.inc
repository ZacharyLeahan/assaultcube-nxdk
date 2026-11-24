// Shared weapon and item definitions for game simulation.
// Please update ./ac_website/htdocs/docs/introduction.html if these figures change.
itemstat ammostats[NUMGUNS] =
{
    {  1,  1,   1,  S_ITEMAMMO  },   // knife dummy
    { 20, 60, 100,  S_ITEMAMMO  },   // pistol
    { 15, 30,  30,  S_ITEMAMMO  },   // carbine
    { 14, 28,  21,  S_ITEMAMMO  },   // shotgun
    { 60, 90,  90,  S_ITEMAMMO  },   // subgun
    { 10, 20,  15,  S_ITEMAMMO  },   // sniper
    { 40, 60,  60,  S_ITEMAMMO  },   // assault
    {  1,  0,   3,  S_ITEMAMMO  },   // grenade
    {100,  0, 100,  S_ITEMAKIMBO}    // akimbo
};

itemstat powerupstats[I_ARMOUR-I_HEALTH+1] =
{
    {33, 0, 100, S_ITEMHEALTH}, // 0 health
    {25, 0, 100, S_ITEMHELMET}, // 1 helmet
    {50, 0, 100, S_ITEMARMOUR}, // 2 armour
};

guninfo guns[NUMGUNS] =
{
    // Please update ./ac_website/htdocs/docs/introduction.html if these figures change.
    //mKR: mdl_kick_rot && mKB: mdl_kick_back
    //reI: recoilincrease && reB: recoilbase && reM: maxrecoil && reF: recoilbackfade
    //pFX: pushfactor
    //modelname                 sound                reloadtime        damage    projspeed  spread     magsize    mKB      reB          reF        isauto
    //             title                      reload       attackdelay      piercing     part     recoil       mKR     reI        reM        pFX
    { "knife",   "Knife",        S_KNIFE,   S_NULL,     0,      500,    50, 100,     0,   0,  1,    1,   1,    0,  0,   0,   0,    0,    0,   1,   false },
    { "pistol",  "Pistol",       S_PISTOL,  S_RPISTOL,  1400,   160,    18,   0,     0,   0, 53,   10,   10,   6,  5,   6,  35,   58,   125,  1,   false },
    { "carbine", "TMP-M&A CB",   S_CARBINE, S_RCARBINE, 1800,   720,    60,  40,     0,   0, 10,   60,   10,   4,  4,  10,  60,   60,   150,  1,   false },
    { "shotgun", "V-19 CS",      S_SHOTGUN, S_RSHOTGUN, 2400,   880,    1,    0,     0,   0,  1,   35,    7,   9,  9,  10, 140,  140,   125,  1,   false },   // CAUTION dmg only sane for server!
    { "subgun",  "A-ARD/10 SMG", S_SUBGUN,  S_RSUBGUN,  1650,   80,     16,   0,     0,   0, 45,   15,   30,   1,  2,   5,  25,   50,   188,  1,   true  },
    { "sniper",  "AD-81 SR",     S_SNIPER,  S_RSNIPER,  1950,   1500,   82,  25,     0,   0, 50,   50,    5,   4,  4,  10,  85,   85,   100,  1,   false },
    { "assault", "MTP-57 AR",    S_ASSAULT, S_RASSAULT, 2000,   120,    22,   0,     0,   0, 18,   30,   20,   0,  2,   3,  25,   50,   115,  1,   true  },
    { "grenade", "Grenades",     S_NULL,    S_NULL,     1000,   650,    200,  0,    20,   6,  1,    1,   1,    3,  1,   0,   0,    0,    0,   3,   false },
    { "pistol",  "Akimbo",       S_PISTOL,  S_RAKIMBO,  1400,   80,     18,   0,     0,   0, 50,   10,   20,   6,  5,   4,  15,   25,   115,  1,   true  },
};

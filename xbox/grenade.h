#pragma once

#include "match.h"
#include "navigation.h"

struct GrenadeFlight {
    MatchPoint position, velocity;
    float rotation, travel_since_bounce;
    bool on_floor;
};

// One five-millisecond AssaultCube physics step. Return true for an audible bounce.
bool grenade_step(GrenadeFlight &flight,const ActorBounds *actors,int count,unsigned millis=5,int resolution=1);

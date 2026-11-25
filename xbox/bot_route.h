#ifndef AC_XBOX_BOT_ROUTE_H
#define AC_XBOX_BOT_ROUTE_H
#include "match.h"

// Active only in a stock offline bot round. A negative item requests patrol.
struct BotRoute {
    int waypoint, item, previous[5];
    unsigned random_after, progress_at;
    float closest;
    bool fallback,jump;
    void reset();
    bool target(MatchPoint from,int pickup,unsigned now,unsigned (*random)(),MatchPoint &destination);
};
#endif

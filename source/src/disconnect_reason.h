#ifndef AC_DISCONNECT_REASON_H
#define AC_DISCONNECT_REASON_H

#include <stdio.h>
#include "protocol.h"

inline const char *format_disconnect_reason(int reason, char *res, size_t size)
{
    static const char *const prot[] = { "terminated by enet", "end of packet", "client num", "tag type", "duplicate connection", "overflow", "random", "voodoo", "sync", "auth failed" },
        *const refused[] = { "connection refused", "due to ban", "due to blacklisting", "wrong password", "server FULL", "server mastermode is \"private\"", "server overloaded" },
        *const removed[] = { "removed from server", "failed admin login", "inappropriate nickname", "annoyance limit exceeded", "SPAM", "DOS", "ludicrous speed", "fishslapped", "afk" },
        *const kick[] = { "kicked", "banned", "kicked by vote", "banned by vote", "kicked by server operator", "banned by server operator",
                    "auto kicked - teamkilling", "auto banned - teamkilling", "auto kicked - friendly fire", "auto banned - friendly fire", "auto kick", "auto ban" };
    const int prot_n = sizeof(prot)/sizeof(*prot);
    const int ref_n = sizeof(refused)/sizeof(*refused);
    const int rem_n = sizeof(removed)/sizeof(*removed);
    const int kick_n = sizeof(kick)/sizeof(*kick);
    if(reason >= DISC_PROTOCOL && reason < DISC_REFUSED)
    { // protocol related errors
        if(reason < DISC_PROTOCOL + prot_n) return prot[reason];
        else snprintf(res, size, "network protocol error '%d'", reason);
    }
    else if(reason >= DISC_REFUSED && reason < DISC_REMOVED)
    { // connection refused
        if(reason < DISC_REFUSED + ref_n) return refused[reason - DISC_REFUSED];
        else snprintf(res, size, "connection refused '%d'", reason);
    }
    else if(reason >= DISC_REMOVED && reason < DISC_KICK)
    { // player removed from server
        if(reason < DISC_REMOVED + rem_n) return removed[reason - DISC_REMOVED];
        else snprintf(res, size, "removed from server '%d'", reason);
    }
    else if(reason >= DISC_KICK && reason < DISC_UNKNOWN)
    { // kicks & bans
        reason -= DISC_KICK;
        if(reason < kick_n) return kick[reason];
        else snprintf(res, size, "%s '%d'", reason & 1 ? "banned" : "kicked", reason + DISC_KICK);
    }
    else
    { // unknown reasons
        snprintf(res, size, "unknown '%d'", reason);
    }
    return res;
}

#endif

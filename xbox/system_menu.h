#ifndef AC_XBOX_SYSTEM_MENU_H
#define AC_XBOX_SYSTEM_MENU_H

typedef enum { MENU_CLOSED = -1, MENU_RESUME, MENU_DASHBOARD, MENU_RESTART, MENU_SHUTDOWN } MenuChoice;
typedef struct { MenuChoice choice; int confirming; } SystemMenu;

#endif

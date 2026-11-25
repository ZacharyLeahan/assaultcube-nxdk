#ifndef AC_XBOX_AMBIENCE_H
#define AC_XBOX_AMBIENCE_H

void ambience_update(float x,float y,float z,float yaw);
void ambience_mix(int (*mix)[2],unsigned frames);

// Add camera-relative loops at the output rate, after world pitch processing.
void ambience_listener(int underwater,int ringing);
void ambience_listener_mix(int (*mix)[2],unsigned frames);

#endif

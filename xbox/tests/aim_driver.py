from pathlib import Path
import sys
# Add timing probes to a test-only copy of the installed SDL Xbox driver.
src=Path(sys.argv[1]).read_text()
def insert(anchor, code):
    global src
    if src.count(anchor)!=1:
        raise SystemExit("Unsupported nxdk SDL driver: "+anchor)
    src=src.replace(anchor,code)
insert('static void int_read_callback(UTR_T *utr) {', '''
#include <stdio.h>
static struct { unsigned tick, port; int axis; } aim_edges[256];
static volatile unsigned aim_count;
static int previous_axis[4];
static void aim_record(int port, int axis) {
    if ((unsigned)port>=4 || previous_axis[port]==axis) return;
    previous_axis[port]=axis;
    unsigned i=aim_count;
    if(i<256) {
        aim_edges[i].tick=(unsigned)KeQueryPerformanceCounter();
        aim_edges[i].port=port;
        aim_edges[i].axis=axis;
        aim_count=i+1;
    }
}
static void int_read_callback(UTR_T *utr) {''')
insert('        SDL_memcpy(joy->hwdata->raw_data, utr->buff, data_len);','''        if(data_len>=20) aim_record(joy->instance_id%4, (Sint16)(utr->buff[16] | (utr->buff[17]<<8)));
        SDL_memcpy(joy->hwdata->raw_data, utr->buff, data_len);''')
insert('    //Check if the rumble timer has expired.',r'''    static int written;
    if(!written && aim_count && (unsigned)((unsigned)KeQueryPerformanceCounter()-aim_edges[0].tick)>16u*(unsigned)KeQueryPerformanceFrequency()) {
        FILE *log=fopen("D:\\aim-usb.bin","wb");
        if(log){fwrite(aim_edges,sizeof(aim_edges[0]),aim_count,log);fclose(log);written=1;}
    }
    //Check if the rumble timer has expired.''')
Path(sys.argv[2]).write_text(src)

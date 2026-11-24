#include "gpu_render.h"
#define GL_GLEXT_PROTOTYPES
#include <pbgl.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct { unsigned offset, bytes, width, height, mip; } ImageInfo;
typedef struct { unsigned texture, first, count, primitive; } Batch;
typedef struct { int x, y, w, h; } Glyph;
#include "generated/gpu.h"
extern const unsigned char gpu_vertices[], gpu_textures[];
typedef struct { float x,y,z,u,v; unsigned char rgba[4]; } Vertex;
static const Vertex *geometry;
enum { STREAM_CAPACITY = 2048 };
static Vertex *stream;
static unsigned cursor, first;
static unsigned char tint[4] = {255,255,255,255};
static GLuint textures[sizeof(images)/sizeof(images[0])];
static float aspect, hud_width;

static voidpf zalloc_gpu(voidpf opaque, uInt count, uInt size) {
    (void)opaque;
    return calloc(count,size);
}
static void zfree_gpu(voidpf opaque, voidpf data) { (void)opaque; free(data); }

int gpu_init(int width, int height) {
    if (pbgl_init(1)) return 0;
    uint32_t *push = pb_begin();
    push = pb_push1(push,NV097_SET_COMPRESS_ZBUFFER_EN,0);
    pb_end(push);
    stream = pbgl_alloc(STREAM_CAPACITY*sizeof(Vertex));
    if (!stream) return 0;
    aspect = (float)width/height;
    hud_width = aspect*720;
    glViewport(0,0,width,height);
    pbgl_set_swap_interval(0);
    pb_show_debug_screen();
    void *buffer = pbgl_alloc(VERTEX_BYTES);
    if (!buffer) return 0;
    memcpy(buffer,gpu_vertices,VERTEX_BYTES);
    geometry = buffer;
    glGenTextures(sizeof(textures)/sizeof(textures[0]),textures);
    for (unsigned i=0;i<sizeof(textures)/sizeof(textures[0]);++i) {
        const ImageInfo *im = &images[i];
        unsigned size = im->width*im->height*4;
        unsigned char *rgba = malloc(size);
        if (!rgba) return 0;
        z_stream z = {0};
        z.zalloc=zalloc_gpu; z.zfree=zfree_gpu;
        z.next_in=(Bytef *)(gpu_textures+im->offset); z.avail_in=im->bytes;
        z.next_out=rgba; z.avail_out=size;
        int ok = inflateInit(&z)==Z_OK;
        if (ok) {
            ok=inflate(&z,Z_FINISH)==Z_STREAM_END && z.total_out==size;
            inflateEnd(&z);
        }
        if (!ok) { free(rgba); return 0; }
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,im->mip);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,im->mip?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,i<SKY_TEX?GL_REPEAT:GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,i<SKY_TEX?GL_REPEAT:GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,im->width,im->height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        free(rgba);
        if (glGetError()!=GL_NO_ERROR) return 0;
    }
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glClearColor(.39f,.53f,.67f,1);
    return 1;
}

static void bind_vertices(const Vertex *v) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3,GL_FLOAT,sizeof(Vertex),&v->x);
    glTexCoordPointer(2,GL_FLOAT,sizeof(Vertex),&v->u);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Vertex),v->rgba);
}
static void color(float r,float g,float b,float a) {
    tint[0]=r*255; tint[1]=g*255; tint[2]=b*255; tint[3]=a*255;
}
static void begin_quads(void) { first=cursor; }
static void vertex(float x,float y,float z,float u,float v) {
    if(cursor>=STREAM_CAPACITY) return;
    Vertex *out=&stream[cursor++];
    *out=(Vertex){.x=x,.y=y,.z=z,.u=u,.v=v};
    memcpy(out->rgba,tint,4);
}
static void end_quads(void) {
    if(cursor>first) glDrawArrays(GL_QUADS,first,cursor-first);
}
static void batch(unsigned n) {
    const Batch *b=&batches[n];
    glBindTexture(GL_TEXTURE_2D,textures[b->texture]);
    glDrawArrays(b->primitive==4?GL_QUADS:GL_TRIANGLES,b->first,b->count);
}
static void projection(float pitch) {
    float near=.25f, half_h=near*(120.f/180.f), shift=near*pitch/180.f;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-half_h*aspect,half_h*aspect,-half_h+shift,half_h+shift,near,350);
    glMatrixMode(GL_MODELVIEW);
}
static void quad(float x,float y,float w,float h,float u,float v,float du,float dv) {
    vertex(x,y,0,u,v);
    vertex(x+w,y,0,u+du,v);
    vertex(x+w,y+h,0,u+du,v+dv);
    vertex(x,y+h,0,u,v+dv);
}
static void text(float x,float y,float size,const char *str) {
    begin_quads();
    for (;*str;++str) {
        if (*str<'!' || *str>'~') {x+=size*.38f; continue;}
        const Glyph *g=&glyphs[*str-'!'];
        float w=g->w*size/64.f;
        quad(x,y,w,g->h*size/64.f,g->x/512.f,g->y/512.f,g->w/512.f,g->h/512.f);
        x+=w+size*.045f;
    }
    end_quads();
}
static void skybox(void) {
    static const signed char faces[6][12]={
        {1,-1,1,1,1,1,1,1,-1,1,-1,-1}, {1,1,1,-1,1,1,-1,1,-1,1,1,-1},
        {-1,1,1,-1,-1,1,-1,-1,-1,-1,1,-1}, {-1,-1,1,1,-1,1,1,-1,-1,-1,-1,-1},
        {-1,-1,1,-1,1,1,1,1,1,1,-1,1}, {1,-1,-1,1,1,-1,-1,1,-1,-1,-1,-1}
    };
    for(int i=0;i<6;++i) {
        glBindTexture(GL_TEXTURE_2D,textures[SKY_TEX+i]);
        begin_quads();
        for(int j=0;j<4;++j) {

            vertex(faces[i][3*j]*64.f,faces[i][3*j+1]*64.f,faces[i][3*j+2]*64.f,j==1||j==2,j>=2);
        }
        end_quads();
    }
}
static void level(const MatchView *v) {
    float c=cosf(v->angle),s=sinf(v->angle);
    const float camera[16]={-s,0,-c,0, c,0,-s,0, 0,1,0,0, 0,0,0,1};
    projection(v->pitch);
    glLoadMatrixf(camera);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    skybox();
    glTranslatef(-v->px,-v->py,-v->eye);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    bind_vertices(geometry);
    for (int i=0;i<WORLD_BATCHES;++i) batch(i);
    if (v->bot_alive) {
        glPushMatrix();
        glTranslatef(v->tx,v->ty,map_cube((int)v->tx,(int)v->ty)->floor);
        glRotatef(v->bot_yaw*57.29578f,0,0,1);
        batch(BODY_BATCH+v->bot_pose); batch(BOT_GUN_BATCH+v->bot_pose);
        glPopMatrix();
    }
    bind_vertices(stream);
    glBindTexture(GL_TEXTURE_2D,textures[ITEM_TEX]);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,.35f);
    begin_quads();
    for (int i=0;i<v->item_count;++i) {
        const ItemView *p=v->items+i;
        float x=p->x+.5f,y=p->y+.5f,z=map_cube(p->x,p->y)->floor;
        for (int j=0;j<4;++j) {
            int right=j==1||j==2,bottom=j>=2;
            float offset=right?.65f:-.65f;

            vertex(x-s*offset,y+c*offset,z+(bottom?0:1.3f),right,(p->icon+(bottom?.99f:.01f))/8);
        }
    }
    end_quads(); glDisable(GL_ALPHA_TEST);
}
static void weapon(const MatchView *v) {
    if(v->respawning) return;
    // Give the first-person mesh its own depth pass and camera, as in the PC client.
    glClear(GL_DEPTH_BUFFER_BIT);
    projection(0);
    const float camera[16]={1,0,0,0, 0,0,-1,0, 0,1,0,0, 4,-1,-10,1};
    glLoadMatrixf(camera);
    if(v->reload) glTranslatef(0,0,-6);
    bind_vertices(geometry);
    int first=(v->pistol?PISTOL_BATCH:ASSAULT_BATCH)+(v->flash?2:0);
    batch(first); batch(first+1);
    bind_vertices(stream);
}
static void hud(const MatchView *v,int aimed,unsigned fps10,int connected) {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,hud_width,720,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
    if (!v->respawning) {
        float x=hud_width*.5f,y=360;
        color(1,v->hitflash?.3f:.9f,aimed?.2f:.9f,1);
        begin_quads();
        quad(x-13,y-1,8,2,0,0,0,0); quad(x+5,y-1,8,2,0,0,0,0);
        quad(x-1,y-13,2,8,0,0,0,0); quad(x-1,y+5,2,8,0,0,0,0);
        end_quads();
    }
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    color(.03f,.04f,.05f,.78f); begin_quads();
    quad(28,24,528,v->phase==PLAYING?62:90,0,0,0,0); end_quads();
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,textures[FONT_TEX]);
    color(.95f,.94f,.87f,1);
    char line[100];
    if(v->phase==READY) {
        text(42,30,27,"ac_desert   /   Bot Deathmatch");
        text(42,59,23,"START to play    /    First to 3");
    } else {
        snprintf(line,sizeof(line),"YOU  %d      BOT  %d       %u:%02u",v->player_frags,v->bot_frags,v->seconds/60,v->seconds%60);
        text(42,28,28,line);
        if(v->phase==INTERMISSION)
            text(42,59,23,v->player_frags>v->bot_frags?"You win   /   START for rematch":v->player_frags<v->bot_frags?"Bot wins   /   START for rematch":"Draw   /   START for rematch");
        else if(v->respawning) text(42,58,22,"Fragged   /   Respawning");
        else {
            snprintf(line,sizeof(line),"HP %d    ARM %d    %s  %d/%d  %s",v->health,v->armour,v->pistol?"MK-77":"MTP-57",v->magazine,v->reserve,v->reload?"Reload":"");
            text(42,58,20,line);
        }
    }
    if(v->phase!=PLAYING) {
        snprintf(line,sizeof(line),"Controllers connected: %d",connected);
        text(42,85,19,line);
    } else if(!connected) text(42,92,22,"Connect a controller");
    snprintf(line,sizeof(line),"%u.%u FPS",fps10/10,fps10%10);
    text(hud_width-128,29,22,line);
    glDisable(GL_BLEND);
}
// An opt-in 12-second capture. Defer disk I/O until the sample is complete.
typedef struct {
    unsigned tick, vblank, prior_surface, next_surface;
    float yaw, eye;
    unsigned expired;
} FrameSample;
static FrameSample samples[720];
static unsigned sample_count;
static int tracing;
void gpu_trace_init(void) {
    FILE *marker=fopen("D:\\frame-trace.on","rb");
    if (marker) { fclose(marker); tracing=1; }
}
void gpu_present(const MatchView *v,int aimed,unsigned fps10,int connected) {
    cursor=0;
    bind_vertices(stream);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    color(1,1,1,1);
    level(v);
    weapon(v);
    hud(v,aimed,fps10,connected);
    // Show the completed surface at vblank. The default pbkit queue can expose
    // an in-progress frame on this console when rendering the full 720p scene.
    glFinish();
    unsigned address=(unsigned)pb_back_buffer() & 0x03ffffff;
    pb_wait_for_vbl();
    unsigned prior=VIDEOREG(PCRTC_START) & 0x03ffffff;
    VIDEOREG(PCRTC_START)=address;
    if (tracing && (v->phase==PLAYING || sample_count)) {
        FrameSample *sample=&samples[sample_count++];
        *sample=(FrameSample){(unsigned)KeQueryPerformanceCounter(),pb_get_vbl_counter(),
                              prior,address,v->angle,v->eye,v->respawning};
        if (sample_count==sizeof(samples)/sizeof(samples[0])) {
            unsigned hz=(unsigned)KeQueryPerformanceFrequency();
            FILE *file=fopen("D:\\frame-trace.bin","wb");
            if (file) {
                fwrite(&hz,sizeof(hz),1,file);
                fwrite(samples,sizeof(samples),1,file);
                fclose(file);
            }
            tracing=0;
        }
    }
    pbgl_swap_buffers();
}

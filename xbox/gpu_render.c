#include "gpu_render.h"
#include "navigation.h"
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
#include "generated/water.h"
extern const unsigned char gpu_vertices[], gpu_textures[];
extern const unsigned char cell_light[256][256][3];
typedef struct { float x,y,z,u,v; unsigned char rgba[4]; } Vertex;
static const Vertex *geometry;
typedef struct { float u,v; } LightUV;
static LightUV *world_light_uv;
typedef struct { float x,y,z; } Position;
static Position *pose_positions;
static unsigned pose_cursor;
enum { STREAM_CAPACITY = 2048 };
static Vertex *stream;
static unsigned cursor, first;
static unsigned char tint[4] = {255,255,255,255};
static GLuint textures[sizeof(images)/sizeof(images[0])];
static float aspect, hud_width;

// Reserve the slowest recent frame's CPU/GPU work plus scheduling headroom.
// Spend remaining slack before reading the pad so newer reports reach this refresh.
static unsigned tick_hz, last_flip, sample_tick, work_ticks[32], work_index, work_count;
static int holdoff;
void gpu_begin_input(unsigned refresh_hz, int respawning, int bot_shot_pending) {
    unsigned now=(unsigned)KeQueryPerformanceCounter();
    if (work_count==32 && !holdoff && !respawning) {
        unsigned peak=0;
        for (unsigned i=0;i<32;++i) if (work_ticks[i]>peak) peak=work_ticks[i];
        // A newly landed hit adds a full-screen blend before the cost is in the history.
        unsigned period=tick_hz/refresh_hz, guard=bot_shot_pending?tick_hz/160:tick_hz/400;
        unsigned offset=period>peak+guard ? period-peak-guard : 0;
        unsigned cap=tick_hz*7/1000;
        if (offset>cap) offset=cap;
        unsigned since=now-last_flip;
        if (since<offset) {
            unsigned ms=(offset-since)*1000/tick_hz;
            if (ms) {
                LARGE_INTEGER interval;
                interval.QuadPart=-(long long)ms*10000;
                KeDelayExecutionThread(KernelMode,FALSE,&interval);
            }
        }
    }
    sample_tick=(unsigned)KeQueryPerformanceCounter();
}

static voidpf zalloc_gpu(voidpf opaque, uInt count, uInt size) {
    (void)opaque;
    return calloc(count,size);
}
static void zfree_gpu(voidpf opaque, voidpf data) { (void)opaque; free(data); }

int gpu_init(int width, int height) {
    tick_hz=(unsigned)KeQueryPerformanceFrequency();
    if (pbgl_init(1)) return 0;
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
    const Batch *last_world=&batches[WORLD_BATCHES-1];
    unsigned world_count=last_world->first+last_world->count;
    world_light_uv=pbgl_alloc(world_count*sizeof(*world_light_uv));
    if (!world_light_uv) return 0;
    const Vertex *source=(const Vertex *)gpu_vertices;
    for (unsigned i=0;i<world_count;++i)
        world_light_uv[i]=(LightUV){(source[i].x+.5f)/256,(source[i].y+.5f)/256};
    unsigned rifle=batches[ASSAULT_BATCH].count+batches[ASSAULT_BATCH+1].count;
    unsigned pistol=batches[PISTOL_BATCH].count+batches[PISTOL_BATCH+1].count;
    unsigned count=batches[BODY_BATCH].count+batches[BOT_GUN_BATCH].count+(rifle>pistol?rifle:pistol);
    pose_positions=pbgl_alloc(count*sizeof(Position));
    if (!pose_positions) return 0;
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
    glTexEnvi(GL_TEXTURE_ENV,GL_RGB_SCALE,2);
    glEnableClientState(GL_COLOR_ARRAY);
    // Address the light grid with the same map XY used by the PC vertices.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,textures[WORLD_LIGHT_TEX]);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV,GL_RGB_SCALE,2);
    glActiveTexture(GL_TEXTURE0);
    return 1;
}

static void bind_vertices(const Vertex *v) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3,GL_FLOAT,sizeof(Vertex),&v->x);
    glTexCoordPointer(2,GL_FLOAT,sizeof(Vertex),&v->u);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Vertex),v->rgba);
}
static void color(float r,float g,float b,float a) {
    tint[0]=r*255; tint[1]=g*255; tint[2]=b*255; tint[3]=a*255;
}
static void model_color(float x,float y) {
    const unsigned char *rgb=cell_light[(unsigned)y & 255][(unsigned)x & 255];
    glColor4ub(rgb[0],rgb[1],rgb[2],255);
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
    // Keep NV2A draw indices local even when the packed scene exceeds 65536 vertices.
    bind_vertices(geometry+b->first);
    glBindTexture(GL_TEXTURE_2D,textures[b->texture]);
    glDrawArrays(b->primitive==4?GL_QUADS:GL_TRIANGLES,0,b->count);
}
// Pose pairs share topology and UVs. Read positions from cached XBE memory
// and write each mesh to its own GPU span until this frame has finished.
static void animated_batch(unsigned current,unsigned next,float blend) {
    const Batch *b=&batches[current];
    if (blend<=0) {
        bind_vertices(geometry);
        batch(current);
        return;
    }
    const Vertex *source=(const Vertex *)gpu_vertices;
    const Vertex *a=source+b->first, *z=source+batches[next].first;
    Position *out=pose_positions+pose_cursor;
    pose_cursor+=b->count;
    for (unsigned i=0;i<b->count;++i) {
        out[i]=(Position){a[i].x+(z[i].x-a[i].x)*blend,
                          a[i].y+(z[i].y-a[i].y)*blend,
                          a[i].z+(z[i].z-a[i].z)*blend};
    }
    bind_vertices(geometry+b->first);
    glVertexPointer(3,GL_FLOAT,sizeof(Position),out);
    glBindTexture(GL_TEXTURE_2D,textures[b->texture]);
    glDrawArrays(GL_TRIANGLES,0,b->count);
}
static void projection(float near,float half_h) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-half_h*aspect,half_h*aspect,-half_h,half_h,near,1200);
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

            vertex(faces[i][3*j]*512.f,faces[i][3*j+1]*512.f,faces[i][3*j+2]*512.f,j==1||j==2,j>=2);
        }
        end_quads();
    }
}
static void draw_water(unsigned ms) {
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
    float t=fmodf(ms/(300.f*6.2831853f),1.f);
    begin_quads();
    for (unsigned i=0;i<sizeof(water_patches)/sizeof(water_patches[0]);++i) {
        for (unsigned j=0;j<4;++j) {
            int x=water_patches[i][0]+4*(j==1||j==2),y=water_patches[i][1]+4*(j>=2);
            float phase=x*y*(.1f/6.2831853f)+t;
            float wave=phase-floorf(phase)-.5f;
            wave*=8-16*fabsf(wave);
            color(water_color[0]/255.f,water_color[1]/255.f,water_color[2]/255.f,
                  fminf(1,(water_color[3]+24*fabsf(wave))/255.f));
            vertex(x,y,WATER_SURFACE+.15f*wave,0,0);
        }
    }
    end_quads();
}
static void level(const MatchView *v) {
    float c=cosf(v->angle),s=sinf(v->angle);
    float cp=cosf(v->pitch), sp=sinf(v->pitch);
    const float camera[16]={-s,-c*sp,-c*cp,0, c,-s*sp,-s*cp,0, 0,cp,-sp,0, 0,0,0,1};
    // PC world FOV is horizontal: tan(90 degrees / 2) == 1.
    projection(.15f,.15f/aspect);
    glLoadMatrixf(camera);
    glTranslatef(-v->px,-v->py,-v->eye);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    bind_vertices(geometry);
    // The map uses +Y to the camera's right, reversing projected winding.
    glFrontFace(GL_CW);
    glEnable(GL_CULL_FACE);
    // Slot zero marks a view of the sky, including walls at the map boundary.
    // Its depth keeps geometry beyond the opening out of the finished frame.
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    batch(SKY_PORTAL_BATCH);
    glStencilOp(GL_KEEP,GL_KEEP,GL_INCR);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    for (int i=SKY_PORTAL_BATCH+1;i<WORLD_BATCHES;++i) {
        glClientActiveTexture(GL_TEXTURE1);
        glTexCoordPointer(2,GL_FLOAT,sizeof(LightUV),world_light_uv+batches[i].first);
        glClientActiveTexture(GL_TEXTURE0);
        batch(i);
    }
    glClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    for (int i=PROP_START;i<PROP_END;++i) batch(i);
    glDisableClientState(GL_COLOR_ARRAY);
    model_color(v->tx,v->ty);
    {
        glPushMatrix();
        glTranslatef(v->tx,v->ty,map_floor(map_cube((int)v->tx,(int)v->ty)));
        glRotatef(v->bot_yaw*57.29578f,0,0,1);
        unsigned current=bot_frame_batch[v->bot_pose],next=bot_frame_batch[v->next_bot_pose];
        animated_batch(BODY_BATCH+current,BODY_BATCH+next,v->bot_pose_blend);
        animated_batch(BOT_GUN_BATCH+current,BOT_GUN_BATCH+next,v->bot_pose_blend);
        glPopMatrix();
    }
    bind_vertices(geometry);
    for (int i=0;i<v->item_count;++i) {
        const ItemView *p=v->items+i;
        if ((p->x-v->px)*c+(p->y-v->py)*s < -3) continue;
        glPushMatrix();
        glTranslatef(p->x,p->y,p->base+1+.05f*sinf(v->elapsed_ms*.01f+p->x+p->y));
        glRotatef(v->elapsed_ms*.1f,0,0,1);
        model_color(p->x,p->y);
        batch(PICKUP_BATCH+p->icon);
        glPopMatrix();
    }
    glEnableClientState(GL_COLOR_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    bind_vertices(stream);
    // Transparent effects blend against the sky or the nearest opaque surface.
    glDepthMask(GL_FALSE);
    glStencilFunc(GL_EQUAL,0,255);
    glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    glDisable(GL_DEPTH_TEST);
    glPushMatrix(); glLoadMatrixf(camera);
    skybox();
    glPopMatrix();
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    if (v->bot_line.expires>v->elapsed_ms) {
        const ShotLine *line=&v->bot_line;
        glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        color(1,1,.7f,.5f);
        begin_quads();
        vertex(line->start.x,line->start.y,line->start.z,0,0);
        vertex(line->end.x,line->end.y,line->end.z,0,0);
        if (cursor-first==2) glDrawArrays(GL_LINES,first,2);
        color(1,1,1,1);
    }
    if (v->elapsed_ms<v->bot_flash_end_ms && v->bot_alive) {
        const float *a=bot_muzzle[bot_frame_batch[v->bot_pose]];
        const float *b=bot_muzzle[bot_frame_batch[v->next_bot_pose]];
        float p[3];
        for(int i=0;i<3;++i) p[i]=a[i]+v->bot_pose_blend*(b[i]-a[i]);
        float bx=cosf(v->bot_yaw),by=sinf(v->bot_yaw);
        float x=v->tx+bx*p[0]-by*p[1],y=v->ty+by*p[0]+bx*p[1];
        float z=map_floor(map_cube((int)v->tx,(int)v->ty))+p[2];
        float angle=(v->bot_flash_end_ms%360)*.017453293f,ca=cosf(angle),sa=sinf(angle);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR);
        glBindTexture(GL_TEXTURE_2D,textures[FLASH_TEX]);
        begin_quads();
        for(int j=0;j<4;++j) {
            int u=j==1||j==2,w=j>=2;
            float right=u?.525f:-.525f,up=w?-.525f:.525f;
            float r=right*ca-up*sa,h=right*sa+up*ca;
            vertex(x-s*r-c*sp*h,y+c*r-s*sp*h,z+cp*h,u,1-w);
        }
        end_quads();
    }
    glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR,GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D,textures[BULLET_TEX]);
    begin_quads();
    for (unsigned i=0;i<v->mark_count;++i) {
        const BulletMark *m=v->marks+i;
        if (m->expires<=v->elapsed_ms) continue;
        float fade=fminf(1,(m->expires-v->elapsed_ms)/1000.f);
        color(fade,fade,fade,fade);
        for (int j=0;j<4;++j) {
            int u=j==1||j==2, w=j>=2;
            float a=.6f*(u?HOLE_RIGHT:HOLE_LEFT), b=.6f*(w?HOLE_BOTTOM:HOLE_TOP);
            vertex(m->x+(m->axis==0?0:a),m->y+(m->axis==0?a:m->axis==2?b:0),
                   m->z+(m->axis==2?0:b),u,w);
        }
    }
    end_quads();
    glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
    glBindTexture(GL_TEXTURE_2D,textures[BLOOD_TEX]);
    begin_quads();
    for (unsigned i=0;i<v->blood_count;++i) {
        const BloodParticle *b=&v->blood[i];
        const SplashParticle *p=&b->motion;
        unsigned age=v->elapsed_ms-p->born;
        if (age>p->lifetime) continue;
        float fade=b->on_floor ? fminf(1,(p->lifetime-age)/1000.f) : 1;
        color(.5f*fade,fade,fade,1);
        float size=b->on_floor?.6f:.3f;
        unsigned tile=(i*7)%4,rotation=(i/4)%4;
        for (int j=0;j<4;++j) {
            float right=j==1||j==2?size:-size,up=j>=2?-size:size;
            unsigned k=(j+rotation)%4;
            float u=((tile&1)+(k==1||k==2))*.5f,w=((tile>>1)+(k<2))*.5f;
            if (b->on_floor) vertex(p->x+right,p->y+up,p->z,u,w);
            else vertex(p->x-s*right-c*sp*up,p->y+c*right-s*sp*up,p->z+cp*up,u,w);
        }
    }
    end_quads();
    glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glBindTexture(GL_TEXTURE_2D,textures[SPARK_TEX]);
    color(.4f,.4f,.4f,1);
    begin_quads();
    for (unsigned i=0;i<v->spark_count;++i) {
        const SplashParticle *p=v->sparks+i;
        unsigned age=v->elapsed_ms-p->born;
        if (age>p->lifetime) continue;
        float travel=age/20000.f;
        float x=p->x+p->dx*travel,y=p->y+p->dy*travel;
        float z=p->z+p->dz*travel-age*(float)age/120000.f;
        for (int j=0;j<4;++j) {
            int u=j==1||j==2,w=j>=2;
            float right=u?.06f:-.06f,up=w?-.06f:.06f;
            vertex(x-s*right-c*sp*up,y+c*right-s*sp*up,z+cp*up,u,1-w);
        }
    }
    end_quads();
    draw_water(v->elapsed_ms);
    glDisable(GL_BLEND);
    color(1,1,1,1);
    glDepthMask(GL_TRUE);
}
static void weapon_transform(const MatchView *v) {
    // Reserve the front of the depth buffer for the view weapon.
    // The view weapon uses the PC's separate 75-degree vertical field of view.
    projection(.30f,.30f*.76732699f);
    const float camera[16]={1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1};
    glLoadMatrixf(camera);
    glTranslatef(v->gun_side,v->gun_forward-v->gun_back,v->gun_up);
    glRotatef(v->gun_rotation,1,0,0);
    // PC MD3 units are 1/16 of a cube, and weapon::renderhudmodel uses scale 1.28.
    glScalef(.08f,.08f,.08f);
}
static void weapon(const MatchView *v) {
    if(v->respawning) return;
    weapon_transform(v);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    glDisableClientState(GL_COLOR_ARRAY);
    model_color(v->px,v->py);
    bind_vertices(geometry);
    int first=v->pistol?PISTOL_BATCH:ASSAULT_BATCH;
    int current=first+2*v->gun_pose, next=first+2*v->next_gun_pose;
    animated_batch(current,next,v->weapon_kick);
    animated_batch(current+1,next+1,v->weapon_kick);
    glEnableClientState(GL_COLOR_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}
static void muzzle_flash(const MatchView *v) {
    if (v->flash && !v->respawning) {
        weapon_transform(v);
        bind_vertices(stream);
        const float (*muzzle)[3]=v->pistol?pistol_muzzle:assault_muzzle;
        float p[3];
        for (int i=0;i<3;++i) p[i]=muzzle[v->gun_pose][i]+v->weapon_kick*
            (muzzle[v->next_gun_pose][i]-muzzle[v->gun_pose][i]);
        glBindTexture(GL_TEXTURE_2D,textures[FLASH_TEX]);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE); glDepthMask(GL_FALSE);
        begin_quads();
        for (int j=0;j<4;++j) {
            int u=j==1||j==2, w=j>=2;
            vertex(p[0]+(u?4:-4),p[1],p[2]+(w?-4:4),u,w);
        }
        end_quads(); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    }
}
static void equipment(float x,int col,int row,int value,int reserve) {
    glBindTexture(GL_TEXTURE_2D,textures[EQUIPMENT_TEX]);
    begin_quads(); quad(x,648,48,48,col*.25f,row*.25f,.25f,.25f); end_quads();
    glBindTexture(GL_TEXTURE_2D,textures[DIGITS_TEX]);
    char count[20];
    if (reserve>=0) snprintf(count,sizeof(count),"%d/%d",value,reserve);
    else snprintf(count,sizeof(count),"%d",value);
    x+=52;
    begin_quads();
    for (const char *ch=count;*ch;++ch) {
        float u=*ch=='/'?476:4+48*(*ch-'0'), w=*ch=='/'?24:40;
        quad(x,650,w*.71f,45,u/512.f,0,w/512.f,1); x+=w*.71f;
    }
    end_quads();
}
static void overview(const MatchView *v) {
    const float size=540, x=(hud_width-size)*.5f, y=(720-size)*.5f;
    color(1,1,1,1);
    glBindTexture(GL_TEXTURE_2D,textures[RADAR_TEX]);
    glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR);
    begin_quads();
    quad(x,y,size,size,0,0,1,1);
    end_quads();
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D,textures[RADAR_ICONS]);
    glPushMatrix();
    glTranslatef(x+(v->px-MAP_LEFT)*size/MAP_SPAN,y+(v->py-MAP_TOP)*size/MAP_SPAN,0);
    glRotatef(90+v->angle*57.29578f,0,0,1);
    begin_quads(); quad(-12,-12,24,24,v->respawning?.25f:v->flash?.5f:0,.5f,.25f,.25f); end_quads();
    glPopMatrix();
}
static void hud(const MatchView *v,unsigned fps10,int connected,int showmap) {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,hud_width,720,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (v->eye<WATER_SURFACE) {
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
        color(water_color[0]/255.f,water_color[1]/255.f,water_color[2]/255.f,.4f);
        begin_quads(); quad(0,0,hud_width,720,0,0,1,1); end_quads();
    }
    if (v->damage_alpha>0) {
        glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D,textures[DAMAGE_TEX]);
        color(v->damage_alpha,v->damage_alpha,v->damage_alpha,v->damage_alpha);
        begin_quads(); quad(0,0,hud_width,720,0,0,1,1); end_quads();
        glDisable(GL_BLEND);
    }
    glEnable(GL_BLEND);
    if (!v->respawning) {
        float half=v->wide_crosshair?8.4f:6.f;
        glBlendFunc(GL_ONE,GL_ONE);
        glBindTexture(GL_TEXTURE_2D,textures[CROSSHAIR_TEX]);
        color(1,v->health<=25?0:v->health<=50?.5f:1,v->health<=50?0:1,1);
        begin_quads();
        quad(hud_width*.5f-half,360-half,2*half,2*half,0,0,1,1);
        end_quads();
    }
    glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    if (v->phase==PLAYING) {
        color(.5f,0,0,.5f);
        begin_quads();
        for (int i=0;i<8;++i) {
            unsigned until=v->damage_directions[i];
            if (until<=v->elapsed_ms+200) continue;
            float size=40*(1-200.f/(until-v->elapsed_ms));
            float a=i*.78539816f, s=sinf(a), c=cosf(a);
            float x=hud_width*.5f+200*s,y=360-200*c;
            vertex(x,y,0,0,0); vertex(x+size*(c-s),y+size*(s+c),0,0,0);
            vertex(x-size*(c+s),y+size*(c-s),0,0,0); vertex(x,y,0,0,0);
        }
        end_quads();
    }
    if (v->phase==PLAYING && showmap) overview(v);
    else if (v->phase==PLAYING) {
        const float cx=hud_width-100.8f, cy=97.6f, radius=72, span=75;
        float u=(v->px-MAP_LEFT)/MAP_SPAN, t=(v->py-MAP_TOP)/MAP_SPAN;
        float c=cosf(v->angle), s=sinf(v->angle);
        glBindTexture(GL_TEXTURE_2D,textures[RADAR_TEX]);
        color(1,1,1,1);
        begin_quads();
        for (int i=0;i<32;++i) {
            vertex(cx,cy,0,u,t);
            for (int j=i;j<=i+1;++j) {
                float a=j*(6.2831853f/32), x=cosf(a), y=sinf(a);
                vertex(cx+radius*x,cy+radius*y,0,u-span*(s*x+c*y)/MAP_SPAN,
                       t+span*(c*x-s*y)/MAP_SPAN);
            }
            vertex(cx,cy,0,u,t);
        }
        end_quads();
        glBindTexture(GL_TEXTURE_2D,textures[RADAR_ICONS]);
        begin_quads(); quad(cx-6,cy-6,12,12,v->respawning?.25f:v->flash?.5f:0,.5f,.25f,.25f); end_quads();
        glBindTexture(GL_TEXTURE_2D,textures[RADAR_RING]);
        begin_quads(); quad(cx-78.4f,cy-78.4f,156.8f,156.8f,0,0,1,1); end_quads();
        glBindTexture(GL_TEXTURE_2D,textures[RADAR_COMPASS]);
        glPushMatrix(); glTranslatef(cx,cy,0); glRotatef(-90-v->angle*57.29578f,0,0,1);
        begin_quads(); quad(-75.2f,-75.2f,150.4f,150.4f,0,0,1,1); end_quads();
        glPopMatrix();
    }
    if(v->phase!=PLAYING) {
        color(.03f,.04f,.05f,.78f); begin_quads();
        quad(28,24,528,116,0,0,0,0); end_quads();
    } else if(!v->respawning) {
        color(1,1,1,1);
        equipment(30,2,3,v->health,-1);
        if (v->armour) equipment(200,(v->armour-1)/25,2,v->armour,-1);
        equipment(hud_width*.33f,v->pistol?1:2,v->pistol?0:1,v->magazine,v->reserve);
    }
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,textures[FONT_TEX]);
    color(.95f,.94f,.87f,1);
    char line[100];
    if(v->phase==READY) {
        text(42,30,27,"ac_desert   /   Bot Deathmatch");
        text(42,59,23,"START to play    /    First to 3");
    } else {
        snprintf(line,sizeof(line),"YOU %d    BOT %d",v->player_frags,v->bot_frags);
        text(42,28,22,line);
        snprintf(line,sizeof(line),"%u:%02u",v->seconds/60,v->seconds%60);
        text(hud_width-122,2,20,line);
        if(v->phase==INTERMISSION)
            text(42,59,23,v->player_frags>v->bot_frags?"You win   /   START for rematch":v->player_frags<v->bot_frags?"Bot wins   /   START for rematch":"Draw   /   START for rematch");
        else if(v->respawning) text(42,58,22,"Fragged   /   Respawning");
        else if(v->reload) text(hud_width*.33f,620,18,"Reloading");
    }
    if(v->phase!=PLAYING) {
        snprintf(line,sizeof(line),"Controllers connected: %d",connected);
        text(42,85,19,line);
        text(42,110,19,v->classic_assist ? "D-pad L/R   Aim assist: Classic" : "D-pad L/R   Aim assist: Tuned");
    } else if(!connected) text(42,92,22,"Connect a controller");
    snprintf(line,sizeof(line),"%u.%u FPS",fps10/10,fps10%10);
    text(hud_width-128,v->phase==READY?29:676,19,line);
    glDisable(GL_BLEND);
}
// An opt-in 12-second capture. Defer disk I/O until the sample is complete.
typedef struct {
    unsigned tick, vblank, prior_surface, next_surface;
    float yaw, eye;
    unsigned expired;
} FrameSample;
static FrameSample samples[720];
static unsigned input_times[720][3];
typedef struct {
    float px,py,eye,yaw,pitch,tx,ty,chest,strafe,turn;
    unsigned live,clear,aimed;
} TrackingFrame;
static TrackingFrame tracking_frames[720];
typedef struct { TrackingFrame tracking; unsigned vblank; } DuelFrame;
static DuelFrame *duel_frames;
static unsigned duel_count;
enum { DUEL_CAPACITY=5500 }; // 90 seconds at 60 Hz plus clock tolerance.

static TrackingFrame tracking_frame(const MatchView *v,const MatchInput *input,int aimed) {
    float chest=map_floor(map_cube((int)v->tx,(int)v->ty))+2.6f;
    int live=v->phase==PLAYING && v->bot_alive && !v->respawning;
    return (TrackingFrame){v->px,v->py,v->eye,v->angle,v->pitch,v->tx,v->ty,chest,
        input->strafe,input->turn,live,live && visible(v->px,v->py,v->eye,v->tx,v->ty,chest),aimed};
}
static unsigned sample_count;
static int tracing;
void gpu_trace_init(void) {
    FILE *marker=fopen("D:\\frame-trace.on","rb");
    if (marker) { fclose(marker); tracing=1; }
    marker=fopen("D:\\duel-trace.on","rb");
    if (marker) { fclose(marker); duel_frames=malloc(DUEL_CAPACITY*sizeof(*duel_frames)); }
}
unsigned gpu_present(const MatchView *v,const MatchInput *input,int aimed,unsigned fps10,int connected) {
    cursor=0;
    pose_cursor=0;
    bind_vertices(stream);
    glDepthMask(GL_TRUE);
    // The sky covers the background. Clear the full tiled Z24S8 surface for z compression.
    pb_erase_depth_stencil_buffer(0,0,pb_back_buffer_width(),pb_back_buffer_height());
    color(1,1,1,1);
    glEnable(GL_DEPTH_TEST);
    // The clear leaves stencil zero. Opaque pixels, including the gun, hide the sky.
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS,0,255);
    glStencilOp(GL_KEEP,GL_KEEP,GL_INCR);
    glDepthRange(0,.04);
    weapon(v);
    glDepthRange(.04,1);
    level(v);
    glDepthRange(0,.04);
    muzzle_flash(v);
    glDepthRange(0,1);
    hud(v,fps10,connected,input->showmap);
    // Show the completed surface at vblank. The default pbkit queue can expose
    // an in-progress frame on this console when rendering the full 720p scene.
    glFinish();
    work_ticks[work_index++ % 32]=(unsigned)KeQueryPerformanceCounter()-sample_tick;
    if (work_count<32) ++work_count;
    unsigned address=(unsigned)pb_back_buffer() & 0x03ffffff;
    unsigned signature=0;
    if (tracing && (v->phase==PLAYING || sample_count)) {
        // Sample the completed scene on both sides of the reticle, above the weapon.
        const uint32_t *row=(const uint32_t *)pb_back_buffer()+
            (pb_back_buffer_height()*9/20)*(pb_back_buffer_pitch()/4);
        for (unsigned i=1;i<=16;++i)
            signature=signature*16777619u ^ row[pb_back_buffer_width()*i/17];
    }
    unsigned vblank=pb_wait_for_vbl();
    unsigned prior=VIDEOREG(PCRTC_START) & 0x03ffffff;
    VIDEOREG(PCRTC_START)=address;
    static unsigned previous_vblank;
    if (vblank-previous_vblank!=1) holdoff=32;
    else if (holdoff) --holdoff;
    previous_vblank=vblank;
    last_flip=(unsigned)KeQueryPerformanceCounter();
    if (duel_frames && v->phase==PLAYING && duel_count<DUEL_CAPACITY) {
        duel_frames[duel_count++]=(DuelFrame){tracking_frame(v,input,aimed),vblank};
    } else if (duel_count && v->phase==INTERMISSION) {
        FILE *file=fopen(v->classic_assist?"D:\\classic.bin":"D:\\tuned.bin","wb");
        if (file) { fwrite(duel_frames,sizeof(*duel_frames),duel_count,file); fclose(file); }
        duel_count=0;
    }
    if (tracing && (v->phase==PLAYING || sample_count)) {
        input_times[sample_count][0]=sample_tick;
        input_times[sample_count][1]=work_ticks[(work_index-1)%32];
        input_times[sample_count][2]=signature;
        tracking_frames[sample_count]=tracking_frame(v,input,aimed);
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
            file=fopen("D:\\input-times.bin","wb");
            if (file) {
                fwrite(input_times,sizeof(input_times),1,file);
                fclose(file);
            }
            file=fopen("D:\\tracking.bin","wb");
            if (file) {
                float focal_pixels=.5f*pb_back_buffer_width();
                fwrite("ACT1",4,1,file);
                fwrite(&focal_pixels,sizeof(focal_pixels),1,file);
                fwrite(tracking_frames,sizeof(tracking_frames),1,file);
                fclose(file);
            }
            tracing=0;
        }
    }
    pbgl_swap_buffers();
    return vblank;
}

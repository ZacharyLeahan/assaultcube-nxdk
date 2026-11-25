#include "gpu_render.h"
#include "lan/lan.h"
#include "geometry.h"
#include "shot_light.h"
#include "navigation.h"
#define GL_GLEXT_PROTOTYPES
#include <pbgl.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/pbkit_pushbuffer.h>
#include <pbkit/nv_regs.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct { unsigned offset, bytes, width, height, mip, indexed, unpacked, grayscale; } ImageInfo;
typedef struct { unsigned texture, first, count, primitive; } Batch;
typedef struct { int x, y, w, h; } Glyph;
typedef struct { int x,y; unsigned first,count; } PropLightSpan;
#include "generated/gpu.h"
#include "generated/water.h"
typedef struct {
    const unsigned short *poses;
    unsigned surfaces,death_batch;
    const float (*muzzle)[3];
    float flash_scale;
    unsigned icon_x,icon_y;
    const char *choice;
} WeaponArt;
static const WeaponArt weapon_art[]={
    [VIEW_RIFLE]={assault_batches,ASSAULT_SURFACES,0,assault_muzzle,.75f,2,1,"Y   Primary: MTP-57 rifle"},
    [VIEW_SHOTGUN]={shotgun_batches,SHOTGUN_SURFACES,SHOTGUN_DEATH_BATCH,shotgun_muzzle,.75f,3,0,"Y   Primary: V-19 CS shotgun"},
    [VIEW_SMG]={subgun_batches,SUBGUN_SURFACES,SUBGUN_DEATH_BATCH,subgun_muzzle,.55f,0,1,"Y   Primary: A-ARD/10 SMG"},
    [VIEW_SNIPER]={sniper_batches,SNIPER_SURFACES,SNIPER_DEATH_BATCH,sniper_muzzle,.50f,1,1,"Y   Primary: AD-81 sniper   /   LT scope"},
    [VIEW_CARBINE]={carbine_batches,CARBINE_SURFACES,CARBINE_DEATH_BATCH,carbine_muzzle,.50f,2,0,"Y   Primary: TMP-M&A CB carbine"},
    [VIEW_PISTOL]={pistol_batches,PISTOL_SURFACES,0,pistol_muzzle,.25f,1,0,NULL},
    [VIEW_KNIFE]={knife_batches,KNIFE_SURFACES,KNIFE_DEATH_BATCH,NULL,0,0,0,NULL}
};
extern const unsigned char gpu_textures[];
static Vertex *geometry;
typedef struct { float u,v; } LightUV;
static LightUV *world_light_uv;
typedef MatchPoint Position;
extern const Position bot_poses[];
static Position *pose_positions;
// Each silhouette edge emits at most one triangle to the shadow tip.
#define MAX_COUNT(a,b) ((a)>(b)?(a):(b))
enum { WORLD_GUN_INDEX_BYTES=WORLD_WEAPON_MAX_INDICES*sizeof(unsigned short) };
typedef struct {
    unsigned batch,pose_base,vertices;
    const unsigned short *indices,*neighbors;
    const float (*muzzle)[3];
    float flash_radius;
} WorldWeapon;
#define WORLD_MODEL(id,name,muzzle,radius) {id##_BATCH,id##_POSE_BASE,id##_VERTICES,name##_indices,name##_neighbors,muzzle,radius}
static const WorldWeapon world_weapons[]={
    [VIEW_RIFLE]=WORLD_MODEL(BOT_GUN,bot_gun,bot_gun_muzzle,.525f),
    [VIEW_PISTOL]=WORLD_MODEL(WORLD_PISTOL,world_pistol,world_pistol_muzzle,.175f),
    [VIEW_KNIFE]=WORLD_MODEL(WORLD_KNIFE,world_knife,NULL,0),
    [VIEW_SHOTGUN]=WORLD_MODEL(WORLD_SHOTGUN,world_shotgun,world_shotgun_muzzle,.525f),
    [VIEW_SMG]=WORLD_MODEL(WORLD_SMG,world_smg,world_smg_muzzle,.385f),
    [VIEW_CARBINE]=WORLD_MODEL(WORLD_CARBINE,world_carbine,world_carbine_muzzle,.35f),
    [VIEW_SNIPER]=WORLD_MODEL(WORLD_SNIPER,world_sniper,world_sniper_muzzle,.7f)
};
#undef WORLD_MODEL
enum { SHADOW_CAPACITY=3*(2*(sizeof(body_indices)+WORLD_GUN_INDEX_BYTES))/sizeof(unsigned short) };
static Position *shadow_positions, shadow_tip;
static unsigned shadow_count;
static unsigned pose_cursor;
enum { STREAM_CAPACITY = 2048 + 4*MARK_CAPACITY };
static Vertex *stream;
static unsigned cursor, first;
static unsigned char tint[4] = {255,255,255,255};
static GLuint textures[sizeof(images)/sizeof(images[0])];
static float aspect, hud_width;
enum { RADAR_SEGMENTS = 32 };
static struct { float x, y; } radar_rim[RADAR_SEGMENTS+1];

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

static int gpu_failure(int line,int texture) {
    FILE *f=fopen("D:\\gpu-init-error.txt","w");
    if(f) {fprintf(f,"Renderer initialization failed at line %d, texture %d\n",line,texture);fclose(f);}
    return 0;
}
int gpu_init(int width, int height) {
    int texture_index=-1;
    tick_hz=(unsigned)KeQueryPerformanceFrequency();
    for (unsigned i=0;i<=RADAR_SEGMENTS;++i) {
        float a=i*(6.2831853f/RADAR_SEGMENTS);
        radar_rim[i].x=cosf(a);
        radar_rim[i].y=sinf(a);
    }
    if (pbgl_init(1)) return gpu_failure(__LINE__,texture_index);
    shot_light_init();
    stream = pbgl_alloc(STREAM_CAPACITY*sizeof(Vertex));
    if (!stream) return gpu_failure(__LINE__,texture_index);
    aspect = (float)width/height;
    hud_width = aspect*720;
    glViewport(0,0,width,height);
    pbgl_set_swap_interval(0);
    pb_show_debug_screen();
    void *buffer = pbgl_alloc(VERTEX_BYTES);
    if (!buffer) return gpu_failure(__LINE__,texture_index);
    memcpy(buffer,gpu_vertices,VERTEX_BYTES);
    geometry = buffer;
    const Batch *last_world=&batches[WORLD_BATCHES-1];
    unsigned world_count=last_world->first+last_world->count;
    world_light_uv=pbgl_alloc(world_count*sizeof(*world_light_uv));
    if (!world_light_uv) return gpu_failure(__LINE__,texture_index);
    const Vertex *source=(const Vertex *)gpu_vertices;
    for (unsigned i=0;i<world_count;++i)
        world_light_uv[i]=(LightUV){(source[i].x+.5f)/256,(source[i].y+.5f)/256};
    unsigned world_gun=WORLD_WEAPON_MAX_INDICES;
    unsigned extra=batches[BODY_BATCH].count+world_gun;
    for (unsigned i=0;i<sizeof(weapon_art)/sizeof(weapon_art[0]);++i) {
        const WeaponArt *art=weapon_art+i;
        unsigned count=0;
        for (unsigned j=0;j<art->surfaces;++j) count+=batches[art->poses[0]+j].count;
        extra=MAX_COUNT(extra,count);
        if (art->death_batch) extra=MAX_COUNT(extra,batches[BODY_BATCH].count+batches[art->death_batch].count);
    }
    unsigned count=batches[BODY_BATCH].count+world_gun+extra;
    pose_positions=pbgl_alloc(count*sizeof(Position));
    if (!pose_positions) return gpu_failure(__LINE__,texture_index);
    shadow_positions=pbgl_alloc(SHADOW_CAPACITY*sizeof(Position));
    if (!shadow_positions) return gpu_failure(__LINE__,texture_index);
    glGenTextures(sizeof(textures)/sizeof(textures[0]),textures);
    for (unsigned i=0;i<sizeof(textures)/sizeof(textures[0]);++i) {
        texture_index=(int)i;
        const ImageInfo *im = &images[i];
        unsigned size = im->unpacked;
        unsigned char *pixels = malloc(size);
        if (!pixels) return gpu_failure(__LINE__,texture_index);
        z_stream z = {0};
        z.zalloc=zalloc_gpu; z.zfree=zfree_gpu;
        z.next_in=(Bytef *)(gpu_textures+im->offset); z.avail_in=im->bytes;
        z.next_out=pixels; z.avail_out=size;
        int ok = inflateInit(&z)==Z_OK;
        if (ok) {
            ok=inflate(&z,Z_FINISH)==Z_STREAM_END && z.total_out==size;
            inflateEnd(&z);
        }
        if (!ok) { free(pixels); return gpu_failure(__LINE__,texture_index); }
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,im->mip && !im->indexed);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,im->mip?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,i<SKY_TEX?GL_REPEAT:GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,i<SKY_TEX?GL_REPEAT:GL_CLAMP_TO_EDGE);
        if (im->indexed) {
            const unsigned char *indices=pixels+256*3;
            unsigned w=im->width,h=im->height;
            for (unsigned level=0;;++level) {
                glColorTableEXT(GL_TEXTURE_2D,GL_RGB,256,GL_RGB,GL_UNSIGNED_BYTE,pixels);
                glTexImage2D(GL_TEXTURE_2D,level,GL_COLOR_INDEX8_EXT,w,h,0,GL_COLOR_INDEX,GL_UNSIGNED_BYTE,indices);
                if (w==1 && h==1) break;
                indices+=w*h;
                w=w>1?w/2:1; h=h>1?h/2:1;
            }
            // Refresh pbGL's sampler state after uploading the complete mip chain.
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        } else {
            GLenum format=im->grayscale?GL_LUMINANCE_ALPHA:GL_RGBA;
            glTexImage2D(GL_TEXTURE_2D,0,format,im->width,im->height,0,format,GL_UNSIGNED_BYTE,pixels);
        }
        free(pixels);
        if (glGetError()!=GL_NO_ERROR) return gpu_failure(__LINE__,texture_index);
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
    const unsigned char *rgb=lit_cell((unsigned)x & 255,(unsigned)y & 255);
    glColor4ub(rgb[0],rgb[1],rgb[2],255);
}
static void update_shot_lighting(const MatchView *v) {
    ShotLight lights[2]={{(int)v->px,(int)v->py,v->player_light},{(int)v->tx,(int)v->ty,v->bot_light}};
    LightPatch dirty[4];
    unsigned changes=shot_light_update(lights,dirty);
    if (!changes) return;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,textures[WORLD_LIGHT_TEX]);
    for (unsigned i=0;i<changes;++i) {
        int x=dirty[i].x,y=dirty[i].y;
        uint8_t rgba[LIGHT_WIDTH][LIGHT_WIDTH][4];
        for (int row=0;row<LIGHT_WIDTH;++row) memcpy(rgba[row],lit_cell(x,y+row),sizeof(rgba[row]));
        glTexSubImage2D(GL_TEXTURE_2D,0,x,y,LIGHT_WIDTH,LIGHT_WIDTH,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    }
    glActiveTexture(GL_TEXTURE0);
    for (unsigned i=0;i<sizeof(prop_lights)/sizeof(prop_lights[0]);++i) {
        const PropLightSpan *p=prop_lights+i;
        for (unsigned j=0;j<changes;++j) {
            if ((unsigned)(p->x-dirty[j].x)>=LIGHT_WIDTH || (unsigned)(p->y-dirty[j].y)>=LIGHT_WIDTH) continue;
            const uint8_t *rgb=lit_cell(p->x,p->y);
            for (unsigned k=0;k<p->count;++k) memcpy(geometry[p->first+k].rgba,rgb,4);
            break;
        }
    }
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

static MatchPoint muzzle_pose(BotPose pose,MatchWeapon weapon) {
    const float (*bot_muzzle)[3]=world_weapons[weapon].muzzle;
    const float *a=bot_muzzle[bot_frame_batch[pose.first]],*b=bot_muzzle[bot_frame_batch[pose.second]];
    return mix_point((MatchPoint){a[0],a[1],a[2]},(MatchPoint){b[0],b[1],b[2]},pose.blend);
}

static void shadow_edges(const Position *p,const unsigned short *idx,const unsigned short *neighbor,
                         unsigned faces) {
    unsigned char lit[sizeof(body_indices)/sizeof(unsigned short)/3+
                      WORLD_GUN_INDEX_BYTES/sizeof(unsigned short)/3];
    for (unsigned f=0;f<faces;++f) {
        Position a=p[idx[3*f]],b=p[idx[3*f+1]],c=p[idx[3*f+2]];
        float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
        float vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        lit[f]=(uy*vz-uz*vy)*shadow_tip.x+(uz*vx-ux*vz)*shadow_tip.y+
               (ux*vy-uy*vx)*shadow_tip.z<=0;
    }
    for (unsigned f=0;f<faces;++f) if (lit[f]) {
        for (unsigned e=0;e<3;++e) {
            unsigned other=neighbor[3*f+e];
            if (other!=65535 && lit[other]) continue;
            shadow_positions[shadow_count++]=p[idx[3*f+(e+1)%3]];
            shadow_positions[shadow_count++]=p[idx[3*f+e]];
            shadow_positions[shadow_count++]=shadow_tip;
        }
    }
}

static void actor_batch(unsigned batch_id,unsigned base,unsigned count,const unsigned short *indices,const unsigned short *neighbors,
                      BotPose cur,BotPose prev,float transition,unsigned texture) {
    // UV seams duplicate positions in the draw buffer. Mix each MD2 vertex
    // once before expanding to triangle order.
    static Position mixed[MAX_COUNT(BODY_VERTICES,WORLD_WEAPON_MAX_VERTICES)];
    const Position *poses=bot_poses+base;
    const Position *a=poses+bot_frame_batch[cur.first]*count,*b=poses+bot_frame_batch[cur.second]*count;
    for (unsigned i=0;i<count;++i) mixed[i]=mix_point(a[i],b[i],cur.blend);
    if (transition<1) {
        a=poses+bot_frame_batch[prev.first]*count; b=poses+bot_frame_batch[prev.second]*count;
        for (unsigned i=0;i<count;++i)
            mixed[i]=mix_point(mix_point(a[i],b[i],prev.blend),mixed[i],transition);
    }
    const Batch *mesh=&batches[batch_id];
    shadow_edges(mixed,indices,neighbors,mesh->count/3);
    Position *out=pose_positions+pose_cursor;
    pose_cursor+=mesh->count;
    for (unsigned i=0;i<mesh->count;++i) out[i]=mixed[indices[i]];
    bind_vertices(geometry+mesh->first);
    glVertexPointer(3,GL_FLOAT,sizeof(Position),out);
    glBindTexture(GL_TEXTURE_2D,textures[texture]);
    glDrawArrays(GL_TRIANGLES,0,mesh->count);
}
static void world_weapon(MatchWeapon weapon,BotPose current,BotPose previous,float transition) {
    const WorldWeapon *w=&world_weapons[weapon];
    actor_batch(w->batch,w->pose_base,w->vertices,w->indices,w->neighbors,current,previous,transition,batches[w->batch].texture);
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
static float glyph_advance(char ch,float size) {
    return ch<'!' || ch>'~'?size*.38f:glyphs[ch-'!'].w*size/64.f+size*.045f;
}
static void text(float x,float y,float size,const char *str) {
    begin_quads();
    for (;*str;++str) {
        if (*str<'!' || *str>'~') {x+=glyph_advance(*str,size); continue;}
        const Glyph *g=&glyphs[*str-'!'];
        float w=g->w*size/64.f;
        quad(x,y,w,g->h*size/64.f,g->x/512.f,g->y/512.f,g->w/512.f,g->h/512.f);
        x+=glyph_advance(*str,size);
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
static void bot_transform(const MatchView *v) {
    glTranslatef(v->tx,v->ty,v->bot_floor-v->bot_sink);
    glRotatef(v->bot_yaw*57.29578f,0,0,1);
    glRotatef(-v->bot_tilt*57.29578f,0,1,0);
}
static void stencil_reference(unsigned value) {
    // The bundled pbGL stores glStencilFunc's reference but omits the NV2A register.
    uint32_t *p=pb_begin();
    p=pb_push1(p,NV097_SET_STENCIL_FUNC_REF,value);
    pb_end(p);
}
static void draw_shadow_volumes(const MatchView *v,unsigned bot_count) {
    if (bot_count) {
        glPushMatrix(); bot_transform(v);
        glDrawArrays(GL_TRIANGLES,0,bot_count);
        glPopMatrix();
    }
    if (shadow_count>bot_count) {
        glPushMatrix();
        glTranslatef(v->px,v->py,v->player_floor);
        glRotatef(v->angle*57.29578f,0,0,1);
        glDrawArrays(GL_TRIANGLES,bot_count,shadow_count-bot_count);
        glPopMatrix();
    }
}
static void draw_actor_shadows(const MatchView *v,unsigned bot_count) {
    if (!shadow_count) return;
    glPushMatrix();
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3,GL_FLOAT,sizeof(Position),shadow_positions);
    glEnable(GL_STENCIL_TEST);
    // The caster's visible pixels keep bit seven so the open silhouette
    // volume shades receivers without darkening the bot that emitted it.
    glStencilFunc(GL_EQUAL,0,128);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glStencilOp(GL_KEEP,GL_KEEP,GL_INCR);
    draw_shadow_volumes(v,bot_count);
    glCullFace(GL_BACK);
    glStencilOp(GL_KEEP,GL_KEEP,GL_DECR);
    draw_shadow_volumes(v,bot_count);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    // Both untouched sky (0) and opaque surfaces (64) have zero low bits.
    glStencilFunc(GL_NOTEQUAL,0,63);
    glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
    glEnableClientState(GL_COLOR_ARRAY);
    bind_vertices(stream);
    color(0,0,0,.4f);
    begin_quads(); quad(-1,-1,2,2,0,0,1,1); end_quads();
    color(1,1,1,1);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}
static void level(const MatchView *v) {
    float c=cosf(v->camera_yaw),s=sinf(v->camera_yaw);
    float cp=cosf(v->camera_pitch), sp=sinf(v->camera_pitch);
    const float camera[16]={-s,-c*sp,-c*cp,0, c,-s*sp,-s*cp,0, 0,cp,-sp,0, 0,0,0,1};
    // PC world FOV is horizontal: tan(90 degrees / 2) == 1.
    projection(.15f,.15f*(v->scoped?SCOPE_TAN_HALF_FOV:1.f)/aspect);
    glLoadMatrixf(camera);
    glTranslatef(-v->camera.x,-v->camera.y,-v->camera.z);
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
    glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    // Draw opaque MD2 actors before the lightmapped world. Their depth
    // rejects hidden world fragments when a body fills the death camera.
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    glDisableClientState(GL_COLOR_ARRAY);
    shadow_count=0;
    if (!v->hide_bot) {
        model_color(v->tx,v->ty);
        glPushMatrix();
        bot_transform(v);
        // ac_desert uses the PC default 45-degree yaw and 20-unit player shadow.
        float cy=cosf(v->bot_yaw),sy=sinf(v->bot_yaw),ct=cosf(v->bot_tilt),st=sinf(v->bot_tilt);
        float forward=10*(cy+sy),down=-14.142136f;
        shadow_tip=(Position){ct*forward+st*down,10*(cy-sy),-st*forward+ct*down};
        stencil_reference(192);
        actor_batch(BODY_BATCH,BODY_POSE_BASE,BODY_VERTICES,body_indices,body_neighbors,
                    v->current_pose,v->previous_pose,v->pose_transition,batches[BODY_BATCH].texture);
        world_weapon(v->bot_weapon,v->current_pose,v->previous_pose,v->pose_transition);
        stencil_reference(64);
        glPopMatrix();
    }
    unsigned bot_shadow_count=shadow_count;
    if (v->respawning) {
        unsigned frame=(unsigned)v->death_frame,next=frame<5?frame+1:frame;
        float blend=v->death_frame-frame;
        glPushMatrix(); glTranslatef(v->px,v->py,v->player_floor);
        glRotatef(v->angle*57.29578f,0,0,1);
        model_color(v->px,v->py);
        float cy=cosf(v->angle),sy=sinf(v->angle);
        shadow_tip=(Position){10*(cy+sy),10*(cy-sy),-14.142136f};
        BotPose pose={184+(int)frame,184+(int)next,blend};
        stencil_reference(192);
        actor_batch(BODY_BATCH,BODY_POSE_BASE,BODY_VERTICES,body_indices,body_neighbors,
                    pose,pose,1,PLAYER_SKIN);
        unsigned base=weapon_art[v->weapon].death_batch;
        if (base) {
            animated_batch(base+frame,base+next,blend);
        }
        else world_weapon(v->weapon,pose,pose,1);
        stencil_reference(64);
        glPopMatrix();
    }
    glEnableClientState(GL_COLOR_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
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
    for(unsigned i=0;i<v->grenade_count;++i) {
        const GrenadeView *g=&v->grenades[i];
        glPushMatrix();
        glTranslatef(g->pos.x,g->pos.y,g->pos.z);
        glRotatef(g->rotation,0,0,1);glRotatef(g->rotation,1,0,0);
        model_color(g->pos.x,g->pos.y);batch(GRENADE_PROJECTILE_BATCH);
        glPopMatrix();
    }
    for (unsigned i=0;i<v->gib_count;++i) {
        const GibView *g=&v->gibs[i];
        glPushMatrix();
        glTranslatef(g->pos.x,g->pos.y,g->pos.z-g->sink);
        glRotatef(g->yaw,0,0,1); glRotatef(g->pitch,0,1,0);
        model_color(g->pos.x,g->pos.y);
        batch(GIB_BATCH+g->model);
        glPopMatrix();
    }
    bind_vertices(geometry);
    for (int i=0;i<v->item_count;++i) {
        const ItemView *p=v->items+i;
        if ((p->x-v->camera.x)*c+(p->y-v->camera.y)*s < -3) continue;
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
    stencil_reference(0);
    glStencilFunc(GL_EQUAL,0,255);
    glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    glDisable(GL_DEPTH_TEST);
    glPushMatrix(); glLoadMatrixf(camera);
    skybox();
    glPopMatrix();
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    draw_actor_shadows(v,bot_shadow_count);
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
        MatchPoint p=muzzle_pose(v->current_pose,v->bot_weapon);
        if (v->pose_transition<1) p=mix_point(muzzle_pose(v->previous_pose,v->bot_weapon),p,v->pose_transition);
        MatchPoint muzzle=bot_model_point(v,p);
        float x=muzzle.x,y=muzzle.y,z=muzzle.z;
        float angle=(v->bot_flash_end_ms%360)*.017453293f,ca=cosf(angle),sa=sinf(angle);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR);
        glBindTexture(GL_TEXTURE_2D,textures[FLASH_TEX]);
        begin_quads();
        float radius=world_weapons[v->bot_weapon].flash_radius;
        for(int j=0;j<4;++j) {
            int u=j==1||j==2,w=j>=2;
            float right=u?radius:-radius,up=w?-radius:radius;
            float r=right*ca-up*sa,h=right*sa+up*ca;
            vertex(x-s*r-c*sp*h,y+c*r-s*sp*h,z+cp*h,u,1-w);
        }
        end_quads();
    }
    glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR,GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D,textures[BULLET_TEX]);
    begin_quads();
    for (unsigned i=0;i<v->mark_count;++i) {
        const BulletMark *m=&v->marks[(v->first_mark+i)%MARK_CAPACITY];
        float dx=m->x-v->camera.x,dy=m->y-v->camera.y,dz=m->z-v->camera.z;
        float forward=c*dx+s*dy,depth=cp*forward+sp*dz;
        // Allow the full 0.6-unit square at the edge of the camera frustum.
        if (depth<-.5f || fabsf(c*dy-s*dx)>depth+1 ||
            fabsf(cp*dz-sp*forward)>depth/aspect+1) continue;
        if (m->expires<=v->elapsed_ms) continue;
        float fade=fminf(1,(m->expires-v->elapsed_ms)/1000.f);
        color(fade,fade,fade,fade);
        for (int j=0;j<4;++j) {
            int u=j==1||j==2, w=j>=2;
            float a=.6f*(u?HOLE_RIGHT:HOLE_LEFT), b=.6f*(w?HOLE_BOTTOM:HOLE_TOP);
            if (m->axis>=3) a*=.70710678f;
            vertex(m->x+(m->axis==0?0:a),m->y+(m->axis==0?a:m->axis==2?b:m->axis==3?-a:m->axis==4?a:0),
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
    if(v->explosion_count) {
        bind_vertices(geometry);
        glDisableClientState(GL_COLOR_ARRAY);
        for(unsigned i=0;i<v->explosion_count;++i) {
            const ExplosionView *e=&v->explosions[i];
            float age=1-(e->end_ms-v->elapsed_ms)/600.f;
            float radius=1+6*age,fade=(7-radius)/7;
            glPushMatrix();
            glTranslatef(e->pos.x,e->pos.y,e->pos.z);
            glRotatef(v->elapsed_ms/7.f,0,0,1);glScalef(radius,radius,radius);
            glColor4f(fade,.5f*fade,.5f*fade,1);batch(EXPLOSION_BATCH);
            glPopMatrix();
        }
        glEnableClientState(GL_COLOR_ARRAY);bind_vertices(stream);
    }
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
    const WeaponArt *art=&weapon_art[v->weapon];
    unsigned current=art->poses[v->gun_pose],next=art->poses[v->next_gun_pose];
    for (unsigned i=0;i<art->surfaces;++i) animated_batch(current+i,next+i,current==next?0:v->gun_pose_blend);
    glEnableClientState(GL_COLOR_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}
static void muzzle_flash(const MatchView *v) {
    if (v->flash && !v->respawning) {
        weapon_transform(v);
        bind_vertices(stream);
        const WeaponArt *art=&weapon_art[v->weapon];
        const float (*muzzle)[3]=art->muzzle;
        float p[3];
        for (int i=0;i<3;++i) p[i]=muzzle[v->gun_pose][i]+v->gun_pose_blend*
            (muzzle[v->next_gun_pose][i]-muzzle[v->gun_pose][i]);
        glTranslatef(p[0],p[1],p[2]);
        // The emitter follows the barrel while the particle faces the camera.
        glRotatef(-v->gun_rotation,1,0,0);
        float half=.7f*art->flash_scale/.08f;
        float ca=cosf(v->flash_angle),sa=sinf(v->flash_angle);
        glBindTexture(GL_TEXTURE_2D,textures[FLASH_TEX]);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR); glDepthMask(GL_FALSE);
        begin_quads();
        for (int j=0;j<4;++j) {
            int u=j==1||j==2,w=j>=2;
            float right=u?half:-half,up=w?-half:half;
            vertex(right*ca-up*sa,0,right*sa+up*ca,u,1-w);
        }
        end_quads(); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    }
}
static float hud_digits(float x,float y,int value,float scale) {
    char count[16];
    snprintf(count,sizeof(count),"%d",value);
    begin_quads();
    for (const char *ch=count;*ch;++ch) {
        float u=4+48*(*ch-'0');
        quad(x,y,40*scale,64*scale,u/512.f,0,40/512.f,1);
        x+=41*scale;
    }
    end_quads();
    return x;
}
static void equipment(float x,int col,int row,int value,int reserve) {
    glBindTexture(GL_TEXTURE_2D,textures[EQUIPMENT_TEX]);
    begin_quads(); quad(x,660,48,48,col*.25f,row*.25f,.25f,.25f); end_quads();
    glBindTexture(GL_TEXTURE_2D,textures[DIGITS_TEX]);
    if (value<0) return;
    x=hud_digits(x+56,823*.8f,value,.8f);
    if (reserve>=0) hud_digits(x,826*.8f,reserve,.4f);
}
static void radar_player(const MatchView *v,float x,float y,float yaw,float size) {
    float col=v->respawning?.25f:v->radar_active?.5f:0;
    float alpha=v->radar_active?fminf(1,.7f+.5f*sinf(v->elapsed_ms/30.f)):1;
    color(1,1,1,alpha);
    glBindTexture(GL_TEXTURE_2D,textures[RADAR_ICONS]);
    glPushMatrix(); glTranslatef(x,y,0); glRotatef(yaw*57.29578f,0,0,1);
    begin_quads(); quad(-size*.5f,-size*.5f,size,size,col,.5f,.25f,.25f); end_quads();
    glPopMatrix();
    color(1,1,1,1);
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
    float px=x+(v->px-MAP_LEFT)*size/MAP_SPAN,py=y+(v->py-MAP_TOP)*size/MAP_SPAN;
    radar_player(v,px,py,v->angle+1.57079633f,24);
    glBindTexture(GL_TEXTURE_2D,textures[FONT_TEX]);
    text(px+12,py+12,12,"You");
}
static void system_overlay(const SystemMenu *menu) {
    static const char *const choices[]={"Resume","Return to dashboard","Restart Xbox","Shut down Xbox"};
    float x=(hud_width-480)/2;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
    color(.03f,.04f,.05f,.94f);
    begin_quads(); quad(x,195,480,310,0,0,1,1); end_quads();
    glBindTexture(GL_TEXTURE_2D,textures[FONT_TEX]);
    color(1,1,1,1);
    text(x+28,217,30,"System menu");
    if (menu->confirming) {
        text(x+28,286,25,choices[menu->choice]);
        text(x+28,329,21,"End this game session?");
        text(x+28,452,19,"A  Confirm     B  Cancel");
    } else {
        for (int i=MENU_RESUME;i<=MENU_SHUTDOWN;++i) {
            color(1,i==menu->choice?.75f:1,i==menu->choice?.25f:1,1);
            if (i==menu->choice) text(x+24,270+38*i,22,">");
            text(x+53,270+38*i,22,choices[i]);
        }
        color(1,1,1,1);
        text(x+28,452,19,"D-pad  Move     A  Select     B  Back");
    }
    glDisable(GL_BLEND);
}

static void hud(const MatchView *v,unsigned fps10,int connected,int showmap,const SystemMenu *menu) {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,hud_width,720,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (menu->choice!=MENU_CLOSED) { system_overlay(menu); return; }
    if (v->camera.z<WATER_SURFACE) {
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
    if (v->scoped) {
        float edge=(hud_width-720)*.5f;
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D,textures[SOLID_TEX]);
        color(0,0,0,1);
        begin_quads();
        quad(0,0,edge,720,0,0,1,1);
        quad(edge+720,0,edge,720,0,0,1,1);
        end_quads();
        glBindTexture(GL_TEXTURE_2D,textures[SCOPE_TEX]);
        color(1,1,1,1);
        begin_quads(); quad(edge,0,720,720,0,0,1,1); end_quads();
        glBlendFunc(GL_ONE,GL_ONE);
        glBindTexture(GL_TEXTURE_2D,textures[SCOPE_DOT_TEX]);
        begin_quads(); quad(hud_width*.5f-10,350,20,20,0,0,1,1); end_quads();
    }
    if (!v->scoped && !v->respawning && !v->hide_crosshair) {
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
        float c=cosf(v->camera_yaw), s=sinf(v->camera_yaw);
        glBindTexture(GL_TEXTURE_2D,textures[RADAR_TEX]);
        color(1,1,1,1);
        unsigned fan=cursor;
        vertex(cx,cy,0,u,t);
        for (unsigned i=0;i<=RADAR_SEGMENTS;++i) {
            float x=radar_rim[i].x, y=radar_rim[i].y;
            vertex(cx+radius*x,cy+radius*y,0,u-span*(s*x+c*y)/MAP_SPAN,
                   t+span*(c*x-s*y)/MAP_SPAN);
        }
        if (cursor-fan>=3) glDrawArrays(GL_TRIANGLE_FAN,fan,cursor-fan);
        radar_player(v,cx,cy,v->angle-v->camera_yaw,12);
        glBindTexture(GL_TEXTURE_2D,textures[RADAR_RING]);
        begin_quads(); quad(cx-78.4f,cy-78.4f,156.8f,156.8f,0,0,1,1); end_quads();
        glBindTexture(GL_TEXTURE_2D,textures[RADAR_COMPASS]);
        glPushMatrix(); glTranslatef(cx,cy,0); glRotatef(-90-v->camera_yaw*57.29578f,0,0,1);
        begin_quads(); quad(-75.2f,-75.2f,150.4f,150.4f,0,0,1,1); end_quads();
        glPopMatrix();
    }
    if(v->phase!=PLAYING) {
        color(.03f,.04f,.05f,.78f); begin_quads();
        quad(28,24,528,173,0,0,0,0); end_quads();
    } else if(!v->respawning) {
        color(1,1,1,1);
        equipment(8,2,3,v->health,-1);
        if (v->armour) equipment(196,(v->armour-1)/25,2,v->armour,-1);
        const WeaponArt *art=&weapon_art[v->weapon];
        equipment(384,art->icon_x,art->icon_y,v->weapon==VIEW_KNIFE?-1:v->magazine,v->reserve);
    }
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,textures[FONT_TEX]);
    color(.95f,.94f,.87f,1);
    char line[100];
    if(v->phase==READY) {
        text(42,30,27,"ac_desert   /   Bot Deathmatch");
        text(42,59,23,"START to play    /    First to 3");
    } else {
        if (lan_connected()) snprintf(line,sizeof(line),"%s",lan_status());
        else snprintf(line,sizeof(line),"YOU %d    BOT %d",v->player_frags,v->bot_frags);
        text(42,28,22,line);
        snprintf(line,sizeof(line),"%u:%02u",v->seconds/60,v->seconds%60);
        text(hud_width-122,2,20,line);
        if(v->phase==INTERMISSION)
            text(42,59,23,v->player_frags>v->bot_frags?"You win   /   START for rematch":v->player_frags<v->bot_frags?"Bot wins   /   START for rematch":"Draw   /   START for rematch");
        else if(v->respawning) text(42,58,22,"Fragged   /   Respawning");
        else if(v->reload) text(hud_width*.33f,620,18,"Reloading");
    }
    for (unsigned i=0;i<3 && v->frag_messages[i].text;++i) {
        const char *message=v->frag_messages[i].text;
        float size=64*720.f/(1800*.9f),width=0;
        for (const char *ch=message;*ch;++ch) width+=glyph_advance(*ch,size);
        float t=v->frag_messages[i].age/3000.f;
        color(1,192/255.f,64/255.f,1);
        text((hud_width-width)*.5f,540+i*size+180*t*t*t*t,size,message);
    }
    color(.95f,.94f,.87f,1);
    if(v->phase!=PLAYING) {
        snprintf(line,sizeof(line),"Controllers connected: %d",connected);
        text(42,85,19,line);
        text(42,164,19,lan_status()?lan_status():"D-pad Up   System menu");
        text(42,136,19,weapon_art[v->selected_primary].choice);
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
static unsigned short impact_counts[720];
typedef struct { int8_t target; uint8_t rifle_mag,rifle_ammo,pistol_mag,pistol_ammo; } AmmoFrame;
static AmmoFrame ammo_frames[720];
typedef struct {
    float px,py,eye,yaw,pitch,tx,ty,chest,strafe,turn;
    unsigned live,clear,aimed;
} TrackingFrame;
typedef struct { float yaw,pitch; int health,weapon_sound; } AimFrame;
static AimFrame aim_frames[720];
static TrackingFrame tracking_frames[720];
typedef struct { TrackingFrame tracking; unsigned vblank; } DuelFrame;
static DuelFrame *duel_frames;
static unsigned duel_count;
enum { DUEL_CAPACITY=5500 }; // 90 seconds at 60 Hz plus clock tolerance.

static TrackingFrame tracking_frame(const MatchView *v,const MatchInput *input,int aimed) {
    float chest=v->bot_floor+v->bot_height-1;
    int live=v->phase==PLAYING && v->bot_alive && !v->respawning;
    return (TrackingFrame){v->px,v->py,v->eye,v->angle,v->pitch,v->tx,v->ty,chest,
        input->strafe,input->turn,live,live && visible(v->px,v->py,v->eye,v->tx,v->ty,chest),aimed};
}
static unsigned sample_count;
enum { TRACE_NOW,TRACE_PISTOL,TRACE_AMMO };
static int tracing,trace_trigger;
void gpu_trace_init(void) {
    FILE *marker=fopen("D:\\frame-trace.on","rb");
    if (marker) {
        char trigger[8]={0};
        fread(trigger,1,sizeof(trigger)-1,marker);
        fclose(marker);
        if (!strncmp(trigger,"pistol",6)) trace_trigger=TRACE_PISTOL;
        if (!strncmp(trigger,"ammo",4)) trace_trigger=TRACE_AMMO;
        tracing=1;
    }
    marker=fopen("D:\\duel-trace.on","rb");
    if (marker) { fclose(marker); duel_frames=malloc(DUEL_CAPACITY*sizeof(*duel_frames)); }
}
unsigned gpu_present(const MatchView *v,const MatchInput *input,int aimed,unsigned fps10,int connected,const SystemMenu *menu) {
    cursor=0;
    pose_cursor=0;
    update_shot_lighting(v);
    bind_vertices(stream);
    glDepthMask(GL_TRUE);
    // The sky covers the background. Clear the full tiled Z24S8 surface for z compression.
    pb_erase_depth_stencil_buffer(0,0,pb_back_buffer_width(),pb_back_buffer_height());
    color(1,1,1,1);
    glEnable(GL_DEPTH_TEST);
    // The clear leaves stencil zero. Opaque pixels, including the gun, hide the sky.
    glEnable(GL_STENCIL_TEST);
    stencil_reference(64);
    glStencilFunc(GL_ALWAYS,64,255);
    glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
    glDepthRange(0,.04);
    if (!v->scoped) weapon(v);
    glDepthRange(.04,1);
    level(v);
    glDepthRange(0,.04);
    if (!v->scoped) muzzle_flash(v);
    glDepthRange(0,1);
    hud(v,fps10,connected,input->showmap,menu);
    // Show the completed surface at vblank. The default pbkit queue can expose
    // an in-progress frame on this console when rendering the full 720p scene.
    glFinish();
    work_ticks[work_index++ % 32]=(unsigned)KeQueryPerformanceCounter()-sample_tick;
    if (work_count<32) ++work_count;
    unsigned address=(unsigned)pb_back_buffer() & 0x03ffffff;
    unsigned signature=0;
    if (v->phase==PLAYING && ((trace_trigger==TRACE_PISTOL && v->bot_weapon==VIEW_PISTOL) ||
                              (trace_trigger==TRACE_AMMO && v->bot_supply>=0))) trace_trigger=TRACE_NOW;
    if (tracing && trace_trigger==TRACE_NOW && (v->phase==PLAYING || sample_count)) {
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
    if (duel_frames && menu->choice==MENU_CLOSED && v->phase==PLAYING && duel_count<DUEL_CAPACITY) {
        duel_frames[duel_count++]=(DuelFrame){tracking_frame(v,input,aimed),vblank};
    } else if (duel_count && v->phase==INTERMISSION) {
        FILE *file=fopen(v->classic_assist?"D:\\classic.bin":"D:\\tuned.bin","wb");
        if (file) { fwrite(duel_frames,sizeof(*duel_frames),duel_count,file); fclose(file); }
        duel_count=0;
    }
    if (tracing && trace_trigger==TRACE_NOW && (v->phase==PLAYING || sample_count)) {
        input_times[sample_count][0]=sample_tick;
        input_times[sample_count][1]=work_ticks[(work_index-1)%32];
        input_times[sample_count][2]=signature;
        aim_frames[sample_count]=(AimFrame){v->bot_yaw,v->bot_tilt*4,v->health,v->bot_sound};
        ammo_frames[sample_count]=(AmmoFrame){v->bot_supply,v->bot_rifle_mag,v->bot_rifle_ammo,
                                             v->bot_pistol_mag,v->bot_pistol_ammo};
        impact_counts[sample_count]=v->mark_count;
        tracking_frames[sample_count]=tracking_frame(v,input,aimed);
        FrameSample *sample=&samples[sample_count++];
        *sample=(FrameSample){(unsigned)KeQueryPerformanceCounter(),pb_get_vbl_counter(),
                              prior,address,v->camera_yaw,v->camera.z,v->respawning};
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
            file=fopen("D:\\bot-aim.bin","wb");
            if (file) { fwrite(aim_frames,sizeof(aim_frames),1,file); fclose(file); }
            file=fopen("D:\\impacts.bin","wb");
            if (file) { fwrite(impact_counts,sizeof(impact_counts),1,file); fclose(file); }
            file=fopen("D:\\bot-ammo.bin","wb");
            if (file) { fwrite(ammo_frames,sizeof(ammo_frames),1,file); fclose(file); }
            tracing=0;
        }
    }
    pbgl_swap_buffers();
    return vblank;
}

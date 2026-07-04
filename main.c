#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// 6D world: xyz are spatial, wvu are extra dims
#define XS 10
#define YS 10
#define ZS 10
#define WS 4
#define VS 4
#define US 4
#define FOCAL 6.0f
#define EXTRA_VIEW 2.5f
#define BLOCK_TYPES 8
#define MAX_DRAWS (XS*YS*ZS*WS*VS*US)

static unsigned char world[XS][YS][ZS][WS][VS][US];

static Color blockBase[BLOCK_TYPES] = {
    BLANK,
    {120,80,40,255},   // dirt
    {60,120,40,255},   // grass
    {150,150,150,255}, // stone
    {40,80,200,220},   // water
    {200,180,60,255},  // sand
    {80,60,40,255},    // wood
    {60,180,60,255},   // leaves
};

static int getBlock(int x,int y,int z,int w,int v,int u) {
    if(x<0||y<0||z<0||w<0||v<0||u<0) return 0;
    if(x>=XS||y>=YS||z>=ZS||w>=WS||v>=VS||u>=US) return 0;
    return world[x][y][z][w][v][u];
}
static void setBlock(int x,int y,int z,int w,int v,int u,int t) {
    if(x<0||y<0||z<0||w<0||v<0||u<0) return;
    if(x>=XS||y>=YS||z>=ZS||w>=WS||v>=VS||u>=US) return;
    world[x][y][z][w][v][u]=(unsigned char)t;
}

// DDA in current XYZ slice (at integer w,v,u)
static bool raycast(Vector3 origin, Vector3 dir, int iw, int iv, int iu,
                    int *hx, int *hy, int *hz, Vector3 *normal) {
    int x=(int)floorf(origin.x), y=(int)floorf(origin.y), z=(int)floorf(origin.z);
    float dx=fabsf(dir.x)<1e-5f?1e30f:1.0f/fabsf(dir.x);
    float dy=fabsf(dir.y)<1e-5f?1e30f:1.0f/fabsf(dir.y);
    float dz=fabsf(dir.z)<1e-5f?1e30f:1.0f/fabsf(dir.z);
    int sx=dir.x<0?-1:1, sy=dir.y<0?-1:1, sz=dir.z<0?-1:1;
    float tx=(dir.x<0?(origin.x-x):(x+1-origin.x))*dx;
    float ty=(dir.y<0?(origin.y-y):(y+1-origin.y))*dy;
    float tz=(dir.z<0?(origin.z-z):(z+1-origin.z))*dz;
    Vector3 norm={0,0,0};
    for(int i=0;i<64;i++) {
        if(getBlock(x,y,z,iw,iv,iu)){*hx=x;*hy=y;*hz=z;*normal=norm;return true;}
        if(tx<ty&&tx<tz){x+=sx;norm=(Vector3){-(float)sx,0,0};tx+=dx;}
        else if(ty<tz){y+=sy;norm=(Vector3){0,-(float)sy,0};ty+=dy;}
        else{z+=sz;norm=(Vector3){0,0,-(float)sz};tz+=dz;}
    }
    return false;
}

static void genWorld(void) {
    for(int w=0;w<WS;w++) for(int v=0;v<VS;v++) for(int u=0;u<US;u++) {
        int gh = 3 + (w+v+u)%3;
        for(int x=0;x<XS;x++) for(int z=0;z<ZS;z++) {
            for(int y=0;y<gh-1;y++) setBlock(x,y,z,w,v,u,1);
            setBlock(x,gh-1,z,w,v,u,2);
        }
        int tx=3+(w+v)%3, tz=3+(v+u)%3;
        for(int y=gh;y<gh+2+u;y++) setBlock(tx,y,tz,w,v,u,3);
    }
}

typedef struct { Vector3 pos; float size; Color col; float dist2; } DrawCmd;
static DrawCmd draws[MAX_DRAWS];

static int cmpDrawFar(const void *a, const void *b) {
    float da=((DrawCmd*)a)->dist2, db=((DrawCmd*)b)->dist2;
    return (db>da)-(db<da); // descending = far first
}

int main(void) {
    InitWindow(1280,720,"voxel6d - 6D perspective (Penteract style)");
    SetTargetFPS(60);
    DisableCursor();

    genWorld();

    float cx=5,cy=6,cz=5;         // XYZ camera pos
    float cw=1.5f,cv=1.5f,cu=1.5f; // extra-dim camera pos (continuous)
    float yaw=0,pitch=0;
    int selBlock=1;

    Camera3D rcam = {
        .position={0,0,-0.001f}, .target={0,0,0}, .up={0,1,0},
        .fovy=70, .projection=CAMERA_PERSPECTIVE
    };

    float moveSpeed=5.0f, lookSpeed=0.0018f, extraSpeed=1.5f;

    while(!WindowShouldClose()) {
        float dt=GetFrameTime();

        Vector2 md=GetMouseDelta();
        yaw-=md.x*lookSpeed;
        pitch-=md.y*lookSpeed;
        if(pitch>1.4f)pitch=1.4f;
        if(pitch<-1.4f)pitch=-1.4f;

        float cosY=cosf(yaw), sinY=sinf(yaw);
        Vector3 fwd={cosf(pitch)*cosY, sinf(pitch), cosf(pitch)*-sinY};

        if(IsKeyDown(KEY_W)){cx+=cosY*moveSpeed*dt; cz-=sinY*moveSpeed*dt;}
        if(IsKeyDown(KEY_S)){cx-=cosY*moveSpeed*dt; cz+=sinY*moveSpeed*dt;}
        if(IsKeyDown(KEY_A)){cx-=sinY*moveSpeed*dt; cz-=cosY*moveSpeed*dt;}
        if(IsKeyDown(KEY_D)){cx+=sinY*moveSpeed*dt; cz+=cosY*moveSpeed*dt;}
        if(IsKeyDown(KEY_SPACE)) cy+=moveSpeed*dt;
        if(IsKeyDown(KEY_LEFT_SHIFT)) cy-=moveSpeed*dt;
        // Smooth extra-dim movement
        if(IsKeyDown(KEY_Q)) cw+=extraSpeed*dt;
        if(IsKeyDown(KEY_E)) cw-=extraSpeed*dt;
        if(IsKeyDown(KEY_R)) cv+=extraSpeed*dt;
        if(IsKeyDown(KEY_F)) cv-=extraSpeed*dt;
        if(IsKeyDown(KEY_T)) cu+=extraSpeed*dt;
        if(IsKeyDown(KEY_G)) cu-=extraSpeed*dt;

        for(int k=KEY_ONE;k<=KEY_SEVEN;k++)
            if(IsKeyPressed(k)) selBlock=k-KEY_ONE+1;

        // Current integer extra-dim slice for interaction
        int iw=(int)floorf(cw); int iv=(int)floorf(cv); int iu=(int)floorf(cu);
        if(iw<0)iw=0; if(iw>=WS)iw=WS-1;
        if(iv<0)iv=0; if(iv>=VS)iv=VS-1;
        if(iu<0)iu=0; if(iu>=US)iu=US-1;

        int hx,hy,hz; Vector3 hnorm;
        bool hit=raycast((Vector3){cx,cy,cz},fwd,iw,iv,iu,&hx,&hy,&hz,&hnorm);

        if(hit&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            setBlock(hx,hy,hz,iw,iv,iu,0);
        if(hit&&IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            int px=hx+(int)hnorm.x, py=hy+(int)hnorm.y, pz=hz+(int)hnorm.z;
            if(!getBlock(px,py,pz,iw,iv,iu))
                setBlock(px,py,pz,iw,iv,iu,selBlock);
        }

        // Build draw list with 6D perspective projection
        int ndraw=0;
        for(int bx=0;bx<XS;bx++)
        for(int by=0;by<YS;by++)
        for(int bz=0;bz<ZS;bz++)
        for(int bw=0;bw<WS;bw++)
        for(int bv=0;bv<VS;bv++)
        for(int bu=0;bu<US;bu++) {
            int b=getBlock(bx,by,bz,bw,bv,bu);
            if(!b) continue;

            float dw=(bw+0.5f)-cw, dv=(bv+0.5f)-cv, du=(bu+0.5f)-cu;
            if(fabsf(dw)>EXTRA_VIEW||fabsf(dv)>EXTRA_VIEW||fabsf(du)>EXTRA_VIEW) continue;

            // Sequential perspective divides: 6D→5D→4D→3D
            float sw=FOCAL/(FOCAL+dw); if(sw<0.05f) continue;
            float sv=FOCAL/(FOCAL+dv*sw); if(sv<0.05f) continue;
            float su=FOCAL/(FOCAL+du*sw*sv); if(su<0.05f) continue;
            float s=sw*sv*su; // total perspective scale

            // Project XYZ: blocks closer to cam in extra dims appear at normal size;
            // farther blocks appear smaller and pulled toward camera
            float px=cx+((bx+0.5f)-cx)*s;
            float py=cy+((by+0.5f)-cy)*s;
            float pz=cz+((bz+0.5f)-cz)*s;

            float rx=px-cx, ry=py-cy, rz=pz-cz;
            float dist2=rx*rx+ry*ry+rz*rz;

            // Color: tint blue/warm based on extra-dim offset to show depth
            Color c=blockBase[b];
            float wBias=0.85f+0.05f*bw, vBias=0.85f+0.05f*bv, uBias=0.85f+0.05f*bu;
            c.r=(unsigned char)fminf(255,c.r*wBias);
            c.g=(unsigned char)fminf(255,c.g*vBias);
            c.b=(unsigned char)fminf(255,c.b*uBias);
            // Fade blocks in distant extra dims
            float extDist=(fabsf(dw)+fabsf(dv)+fabsf(du))/(EXTRA_VIEW*3.0f);
            c.a=(unsigned char)(fmaxf(0.25f,1.0f-extDist*0.7f)*c.a);

            draws[ndraw++]=(DrawCmd){{px,py,pz},s,c,dist2};
        }

        // Sort far to near (painter's algorithm)
        qsort(draws,ndraw,sizeof(DrawCmd),cmpDrawFar);

        rcam.position=(Vector3){cx,cy,cz};
        rcam.target=(Vector3){cx+fwd.x,cy+fwd.y,cz+fwd.z};

        BeginDrawing();
        ClearBackground((Color){8,8,18,255});
        BeginMode3D(rcam);

        for(int i=0;i<ndraw;i++) {
            DrawCmd *d=&draws[i];
            DrawCube(d->pos, d->size, d->size, d->size, d->col);
            if(d->size>0.4f)
                DrawCubeWires(d->pos, d->size, d->size, d->size, (Color){0,0,0,35});
        }

        // Target highlight (in current slice, s≈1 so position is accurate)
        if(hit)
            DrawCubeWires((Vector3){hx+0.5f,hy+0.5f,hz+0.5f},1.02f,1.02f,1.02f,WHITE);

        EndMode3D();

        int sw2=GetScreenWidth()/2, sh2=GetScreenHeight()/2;
        DrawLine(sw2-10,sh2,sw2+10,sh2,WHITE);
        DrawLine(sw2,sh2-10,sw2,sh2+10,WHITE);

        DrawText("voxel6d",10,10,20,WHITE);
        DrawText("WASD=XZ  Space/Shift=Y  Q/E=W  R/F=V  T/G=U  1-7=block  LMB=break  RMB=place",10,34,13,LIGHTGRAY);

        char buf[160];
        snprintf(buf,sizeof(buf),"xyz: %.1f %.1f %.1f   extra: W=%.2f V=%.2f U=%.2f   blocks: %d",
            cx,cy,cz,cw,cv,cu,ndraw);
        DrawText(buf,10,50,13,YELLOW);

        snprintf(buf,sizeof(buf),"block: %d  slice: W=%d V=%d U=%d",selBlock,iw,iv,iu);
        DrawText(buf,10,66,13,SKYBLUE);
        DrawFPS(10,82);

        EndDrawing();
    }
    CloseWindow();
    return 0;
}

// voxel6d - 6D first-person voxel game
// Extra dims: W (Q/E), V (R/F), U (T/G)
// Look into extra dims: Z/X = look_w, C/V = look_v (makes blocks warp)
// LMB=break, RMB=place, 1-7=block type
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XS 8
#define YS 8
#define ZS 8
#define WS 4
#define VS 4
#define US 4
#define FOCAL 5.0f
#define EXTRA_VIEW 2.5f

static unsigned char world[XS][YS][ZS][WS][VS][US];

static Color blockColor[] = {
    BLANK,
    {120,80,40,255},{60,120,40,255},{150,150,150,255},
    {40,80,200,220},{200,180,60,255},{80,60,40,255},{60,180,60,255}
};

// ---- 6D view matrix ----
// Rows = camera basis vectors in world space:
// row 0=right, 1=up, 2=forward, 3=over(W), 4=yonder(V), 5=ultra(U)
static float VM[6][6];

static void vmIdentity(void) {
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) VM[i][j]=(i==j)?1.f:0.f;
}

// Rotate rows a and b of VM by angle
static void vmRot(int a, int b, float angle) {
    float c=cosf(angle), s=sinf(angle);
    for(int k=0;k<6;k++){
        float ra=VM[a][k], rb=VM[b][k];
        VM[a][k]=c*ra-s*rb; VM[b][k]=s*ra+c*rb;
    }
}

static void buildView(float yaw, float pitch, float lw, float lv) {
    vmIdentity();
    vmRot(0,2,yaw);       // XZ = horizontal yaw
    vmRot(1,2,-pitch);    // YZ = vertical pitch
    vmRot(2,3,lw);        // ZW = look into W
    vmRot(2,4,lv);        // ZV = look into V
}

// ---- projection ----
// Project 6D world point to 3D "camera space" suitable for raylib:
// output = (cam_right*s, cam_up*s, cam_forward)
// Raylib perspective camera then divides X,Y by Z, giving correct screen coords.
static bool proj6(
    float wx,float wy,float wz,float ww,float wv,float wu,
    float cx,float cy,float cz,float cw,float cv,float cu,
    Vector3 *out)
{
    float r[6]={wx-cx,wy-cy,wz-cz,ww-cw,wv-cv,wu-cu};
    float c[6]={0};
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) c[i]+=VM[i][j]*r[j];

    if(c[2]<0.05f) return false;

    // Sequential perspective divides on the extra cam dims
    float sw=FOCAL/(FOCAL+c[3]); if(sw<0.05f||sw>20.f) return false;
    float sv=FOCAL/(FOCAL+c[4]*sw); if(sv<0.05f||sv>20.f) return false;
    float su=FOCAL/(FOCAL+c[5]*sw*sv); if(su<0.05f||su>20.f) return false;
    float s=sw*sv*su;

    *out=(Vector3){c[0]*s, c[1]*s, c[2]};
    return true;
}

// ---- world helpers ----
static int getBlock(int x,int y,int z,int w,int v,int u){
    if(x<0||y<0||z<0||w<0||v<0||u<0) return 0;
    if(x>=XS||y>=YS||z>=ZS||w>=WS||v>=VS||u>=US) return 0;
    return world[x][y][z][w][v][u];
}
static void setBlock(int x,int y,int z,int w,int v,int u,int t){
    if(x<0||y<0||z<0||w<0||v<0||u<0) return;
    if(x>=XS||y>=YS||z>=ZS||w>=WS||v>=VS||u>=US) return;
    world[x][y][z][w][v][u]=(unsigned char)t;
}

static void genWorld(void){
    for(int w=0;w<WS;w++) for(int v=0;v<VS;v++) for(int u=0;u<US;u++){
        int gh=3+(w+v+u)%3;
        for(int x=0;x<XS;x++) for(int z=0;z<ZS;z++){
            for(int y=0;y<gh-1;y++) setBlock(x,y,z,w,v,u,1);
            setBlock(x,gh-1,z,w,v,u,2);
        }
        setBlock(3+(w+v)%3, gh, 3+(v+u)%3, w,v,u,3);
        setBlock(3+(w+v)%3, gh+1, 3+(v+u)%3, w,v,u,3);
    }
}

// Simple DDA raycast in current XYZ slice for interaction
static bool raycast(Vector3 ro, Vector3 rd, int iw,int iv,int iu,
                    int *hx,int *hy,int *hz, Vector3 *hn){
    int x=(int)floorf(ro.x),y=(int)floorf(ro.y),z=(int)floorf(ro.z);
    float dx=fabsf(rd.x)<1e-5f?1e30f:1.f/fabsf(rd.x);
    float dy=fabsf(rd.y)<1e-5f?1e30f:1.f/fabsf(rd.y);
    float dz=fabsf(rd.z)<1e-5f?1e30f:1.f/fabsf(rd.z);
    int sx=rd.x<0?-1:1,sy=rd.y<0?-1:1,sz=rd.z<0?-1:1;
    float tx=(rd.x<0?(ro.x-x):(x+1-ro.x))*dx;
    float ty=(rd.y<0?(ro.y-y):(y+1-ro.y))*dy;
    float tz=(rd.z<0?(ro.z-z):(z+1-ro.z))*dz;
    Vector3 n={0,0,0};
    for(int i=0;i<64;i++){
        if(getBlock(x,y,z,iw,iv,iu)){*hx=x;*hy=y;*hz=z;*hn=n;return true;}
        if(tx<ty&&tx<tz){x+=sx;n=(Vector3){-(float)sx,0,0};tx+=dx;}
        else if(ty<tz){y+=sy;n=(Vector3){0,-(float)sy,0};ty+=dy;}
        else{z+=sz;n=(Vector3){0,0,-(float)sz};tz+=dz;}
    }
    return false;
}

// ---- rendering ----
// Face corners: bit0=+X, bit1=+Y, bit2=+Z
static const int FCONN[6][4] = {
    {0,2,6,4},{1,5,7,3}, // -X, +X
    {0,4,5,1},{2,3,7,6}, // -Y, +Y
    {0,1,3,2},{4,6,7,5}, // -Z, +Z
};
// Face shading: dot with fake light dir (0.6,0.9,0.5)
static const float FSHADE[6] = {0.65f,0.80f,0.50f,1.00f,0.70f,0.75f};

typedef struct {
    Vector3 v[4]; // 4 projected 3D corners
    Color col;
    float depth;
} Face;

#define MAX_FACES (XS*YS*ZS*WS*VS*US*6)
static Face faces[MAX_FACES];
static int nfaces;

static int cmpFace(const void *a,const void *b){
    float da=((Face*)a)->depth, db=((Face*)b)->depth;
    return (da<db)-(da>db); // descending: far first
}

int main(void){
    InitWindow(1280,720,"voxel6d - 6D perspective");
    SetTargetFPS(60);
    DisableCursor();
    genWorld();

    float cx=4,cy=5,cz=4;
    float cw=1.5f,cv=1.5f,cu=1.5f;
    float yaw=0,pitch=0,look_w=0,look_v=0;
    int selBlock=1;
    float moveSpeed=5.f, lookSp=0.0018f, extraSp=1.5f, lwSp=0.8f;

    // Fixed camera at origin looking +Z (our proj6 output is already in camera space)
    Camera3D rcam={
        .position={0,0,0},.target={0,0,1},.up={0,1,0},
        .fovy=70,.projection=CAMERA_PERSPECTIVE
    };

    while(!WindowShouldClose()){
        float dt=GetFrameTime();

        Vector2 md=GetMouseDelta();
        bool mmb=IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);
        if(!mmb){
            yaw  -= md.x*lookSp;
            pitch -= md.y*lookSp;
            if(pitch>1.4f)pitch=1.4f;
            if(pitch<-1.4f)pitch=-1.4f;
        } else {
            // MMB drag: look into W/V (like Penteract Placer)
            look_w += md.x*lookSp;
            look_v -= md.y*lookSp;
            look_w=fmaxf(-1.4f,fminf(1.4f,look_w));
            look_v=fmaxf(-1.4f,fminf(1.4f,look_v));
        }

        buildView(yaw,pitch,look_w,look_v);

        // Movement using current camera axes (rows of VM)
        float fwdX=VM[2][0],fwdY=VM[2][1],fwdZ=VM[2][2]; // forward in XYZ
        float rtX =VM[0][0],rtY =VM[0][1],rtZ =VM[0][2]; // right in XYZ
        if(IsKeyDown(KEY_W)){cx+=fwdX*moveSpeed*dt;cy+=fwdY*moveSpeed*dt;cz+=fwdZ*moveSpeed*dt;}
        if(IsKeyDown(KEY_S)){cx-=fwdX*moveSpeed*dt;cy-=fwdY*moveSpeed*dt;cz-=fwdZ*moveSpeed*dt;}
        if(IsKeyDown(KEY_A)){cx-=rtX*moveSpeed*dt;cy-=rtY*moveSpeed*dt;cz-=rtZ*moveSpeed*dt;}
        if(IsKeyDown(KEY_D)){cx+=rtX*moveSpeed*dt;cy+=rtY*moveSpeed*dt;cz+=rtZ*moveSpeed*dt;}
        if(IsKeyDown(KEY_SPACE))  cy+=moveSpeed*dt;
        if(IsKeyDown(KEY_LEFT_SHIFT)) cy-=moveSpeed*dt;
        if(IsKeyDown(KEY_Q)) cw+=extraSp*dt;
        if(IsKeyDown(KEY_E)) cw-=extraSp*dt;
        if(IsKeyDown(KEY_R)) cv+=extraSp*dt;
        if(IsKeyDown(KEY_F)) cv-=extraSp*dt;
        if(IsKeyDown(KEY_T)) cu+=extraSp*dt;
        if(IsKeyDown(KEY_G)) cu-=extraSp*dt;

        for(int k=KEY_ONE;k<=KEY_SEVEN;k++) if(IsKeyPressed(k)) selBlock=k-KEY_ONE+1;

        // Clamp extra dims to world bounds
        if(cw<0)cw=0; if(cw>=WS)cw=WS-0.01f;
        if(cv<0)cv=0; if(cv>=VS)cv=VS-0.01f;
        if(cu<0)cu=0; if(cu>=US)cu=US-0.01f;

        int iw=(int)cw,iv=(int)cv,iu=(int)cu;

        // Raycast in current XYZ slice
        // The forward in world XYZ for raycast:
        Vector3 fwd3={(float)fwdX,(float)fwdY,(float)fwdZ};
        fwd3=Vector3Normalize(fwd3);
        int hx,hy,hz; Vector3 hn;
        bool hit=raycast((Vector3){cx,cy,cz},fwd3,iw,iv,iu,&hx,&hy,&hz,&hn);

        if(hit&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            setBlock(hx,hy,hz,iw,iv,iu,0);
        if(hit&&IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)){
            int px=hx+(int)hn.x,py=hy+(int)hn.y,pz=hz+(int)hn.z;
            if(!getBlock(px,py,pz,iw,iv,iu)) setBlock(px,py,pz,iw,iv,iu,selBlock);
        }

        // ---- Build face list ----
        nfaces=0;
        for(int bx=0;bx<XS;bx++)
        for(int by=0;by<YS;by++)
        for(int bz=0;bz<ZS;bz++)
        for(int bw=0;bw<WS;bw++)
        for(int bv=0;bv<VS;bv++)
        for(int bu=0;bu<US;bu++){
            int b=getBlock(bx,by,bz,bw,bv,bu);
            if(!b) continue;

            // Extra-dim distance cull
            float dw=(bw+0.5f)-cw,dv=(bv+0.5f)-cv,du=(bu+0.5f)-cu;
            if(fabsf(dw)>EXTRA_VIEW||fabsf(dv)>EXTRA_VIEW||fabsf(du)>EXTRA_VIEW) continue;

            // Project the 8 corners
            Vector3 pc[8];
            bool ok=true;
            for(int i=0;i<8;i++){
                float wx=(float)bx+(i&1);
                float wy=(float)by+((i>>1)&1);
                float wz=(float)bz+((i>>2)&1);
                if(!proj6(wx,wy,wz,bw+0.5f,bv+0.5f,bu+0.5f,cx,cy,cz,cw,cv,cu,&pc[i])){
                    ok=false; break;
                }
            }
            if(!ok) continue;

            // Average depth of block center
            Vector3 ctr; proj6(bx+0.5f,by+0.5f,bz+0.5f,bw+0.5f,bv+0.5f,bu+0.5f,cx,cy,cz,cw,cv,cu,&ctr);

            Color bc=blockColor[b];
            // Tint per extra-dim slice so slices are visually distinguishable
            bc.r=(unsigned char)(bc.r*(0.7f+0.1f*bw));
            bc.g=(unsigned char)(bc.g*(0.7f+0.1f*bv));
            bc.b=(unsigned char)(bc.b*(0.7f+0.1f*bu));
            // Fade blocks far in extra dims
            float extD=(fabsf(dw)+fabsf(dv)+fabsf(du))/(EXTRA_VIEW*3.0f);
            bc.a=(unsigned char)(fmaxf(0.3f,1.f-extD*0.65f)*bc.a);

            for(int f=0;f<6;f++){
                if(nfaces>=MAX_FACES) break;
                Face *fc=&faces[nfaces++];
                float sh=FSHADE[f];
                fc->col=(Color){(unsigned char)(bc.r*sh),(unsigned char)(bc.g*sh),(unsigned char)(bc.b*sh),bc.a};
                fc->depth=ctr.z; // sort by block center depth
                for(int k=0;k<4;k++) fc->v[k]=pc[FCONN[f][k]];
            }
        }

        // Sort faces far-to-near
        qsort(faces,nfaces,sizeof(Face),cmpFace);

        // ---- Draw ----
        BeginDrawing();
        ClearBackground((Color){8,8,18,255});
        BeginMode3D(rcam);

        rlDisableBackfaceCulling();
        for(int i=0;i<nfaces;i++){
            Face *f=&faces[i];
            // Draw quad as 2 triangles
            DrawTriangle3D(f->v[0],f->v[1],f->v[2],f->col);
            DrawTriangle3D(f->v[0],f->v[2],f->v[3],f->col);
            // Wireframe edges
            Color wc={0,0,0,30};
            DrawLine3D(f->v[0],f->v[1],wc); DrawLine3D(f->v[1],f->v[2],wc);
            DrawLine3D(f->v[2],f->v[3],wc); DrawLine3D(f->v[3],f->v[0],wc);
        }
        rlEnableBackfaceCulling();

        // Highlight targeted block
        if(hit){
            // Project block center to draw a white outline
            Vector3 hc;
            if(proj6(hx+0.5f,hy+0.5f,hz+0.5f,iw+0.5f,iv+0.5f,iu+0.5f,cx,cy,cz,cw,cv,cu,&hc)){
                DrawSphere(hc,0.05f,WHITE);
            }
        }

        EndMode3D();

        int W=GetScreenWidth(),H=GetScreenHeight();
        DrawLine(W/2-10,H/2,W/2+10,H/2,WHITE);
        DrawLine(W/2,H/2-10,W/2,H/2+10,WHITE);

        DrawText("voxel6d - 6D perspective",10,10,20,WHITE);
        DrawText("WASD=move  Space/Shift=Y  Q/E=W  R/F=V  T/G=U",10,34,13,LIGHTGRAY);
        DrawText("Mouse=look  MMB+mouse=look into W/V  1-7=block  LMB=break  RMB=place",10,50,13,LIGHTGRAY);

        char buf[160];
        snprintf(buf,sizeof(buf),"xyz:%.1f %.1f %.1f  extra:W=%.2f V=%.2f U=%.2f  lookW=%.2f lookV=%.2f",
            cx,cy,cz,cw,cv,cu,look_w,look_v);
        DrawText(buf,10,66,13,YELLOW);
        DrawFPS(10,82);

        EndDrawing();
    }
    CloseWindow();
    return 0;
}

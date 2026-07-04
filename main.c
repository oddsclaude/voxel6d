// voxel6d — 6D first-person voxel game, GPU raycasting like Penteract Placer
// Controls: WASD=move, Space/Shift=Y, Q/E=W, R/F=V, T/G=U
//           Z/X=lookW, C/V=lookV (warp), MMB+drag=look extra dims
//           LMB=break, RMB=place, 1-7=block
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XS 12
#define YS 12
#define ZS 12
#define WS 6
#define VS 6
#define US 1
#define FOCAL_DEG 70.0f

static unsigned char world[XS][YS][ZS][WS][VS][US];

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

static int hash3(int x,int z,int wv){ unsigned int h=(unsigned)(x*73856093^z*19349663^wv*83492791); h^=h>>13;h*=1274126177u;h^=h>>16;return (int)(h&0x7fffffff); }

static void genWorld(void){
    for(int w=0;w<WS;w++) for(int v=0;v<VS;v++) for(int u=0;u<US;u++)
    for(int x=0;x<XS;x++) for(int z=0;z<ZS;z++){
        int h=2+hash3(x,z,w*7+v*3+u)%5;
        for(int y=0;y<h-1;y++) setBlock(x,y,z,w,v,u,(y<h-3)?3:1);
        setBlock(x,h-1,z,w,v,u,2);
    }
}

// ---- 6D view matrix ----
static float VM[6][6];
static void vmId(void){for(int i=0;i<6;i++)for(int j=0;j<6;j++)VM[i][j]=(i==j)?1.f:0.f;}
static void vmRot(int a,int b,float t){
    float c=cosf(t),s=sinf(t);
    for(int k=0;k<6;k++){float ra=VM[a][k],rb=VM[b][k];VM[a][k]=c*ra-s*rb;VM[b][k]=s*ra+c*rb;}
}
static void buildView(float yaw,float pitch,float lw,float lv){
    vmId();vmRot(0,2,yaw);vmRot(1,2,-pitch);vmRot(2,3,lw);vmRot(2,4,lv);
}

// ---- World texture (256x256 grayscale: R = block type 0-7) ----
#define TEXW 256
static unsigned char texData[TEXW*TEXW];
static Texture2D worldTex;

static int bidx(int x,int y,int z,int w,int v,int u){return x+XS*(y+YS*(z+ZS*(w+WS*v)));}
static void syncTex(void){
    memset(texData,0,sizeof(texData));
    for(int u=0;u<US;u++) for(int v=0;v<VS;v++) for(int w=0;w<WS;w++)
    for(int z=0;z<ZS;z++) for(int y=0;y<YS;y++) for(int x=0;x<XS;x++)
        texData[bidx(x,y,z,w,v,u)]=world[x][y][z][w][v][u];
    UpdateTexture(worldTex,texData);
}

// ---- 6D DDA for interaction ----
static bool dda6(float pos[6],float dir[6],int hvox[6],int *lastAxis,int *lastStep){
    float tMax[6],tDelta[6]; int step[6],vox[6];
    for(int i=0;i<6;i++){
        vox[i]=(int)floorf(pos[i]);
        float d=dir[i];
        if(fabsf(d)<1e-8f){tMax[i]=1e30f;tDelta[i]=1e30f;step[i]=0;continue;}
        step[i]=d>0?1:-1; tDelta[i]=fabsf(1.f/d);
        tMax[i]=d>0?(vox[i]+1-pos[i])/d:(pos[i]-vox[i])/(-d);
    }
    *lastAxis=2;*lastStep=1;
    for(int iter=0;iter<100;iter++){
        int b=getBlock(vox[0],vox[1],vox[2],vox[3],vox[4],vox[5]);
        if(b){for(int i=0;i<6;i++)hvox[i]=vox[i];return true;}
        float minT=tMax[0];*lastAxis=0;
        for(int i=1;i<6;i++) if(tMax[i]<minT){minT=tMax[i];*lastAxis=i;}
        *lastStep=step[*lastAxis];
        vox[*lastAxis]+=step[*lastAxis]; tMax[*lastAxis]+=tDelta[*lastAxis];
        if(vox[0]<0||vox[1]<0||vox[2]<0) break;
        if(vox[0]>=XS||vox[1]>=YS||vox[2]>=ZS) break;
        if(vox[3]<0||vox[4]<0||vox[5]<0) break;
        if(vox[3]>=WS||vox[4]>=VS||vox[5]>=US) break;
    }
    return false;
}

// ---- Shaders ----
static const char *VS_SRC =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "out vec2 fragUV;\n"
    "uniform mat4 mvp;\n"
    "void main(){\n"
    "  fragUV=vertexTexCoord;\n"
    "  gl_Position=mvp*vec4(vertexPosition,1.0);\n"
    "}\n";

static const char *FS_SRC =
    "#version 330\n"
    "in vec2 fragUV;\n"
    "out vec4 col;\n"
    "uniform vec2 res;\n"
    "uniform float fovTan;\n"
    "uniform vec3 camPos;\n"
    "uniform vec3 camExtra;\n"
    "uniform float vm[36];\n"
    "uniform sampler2D worldTex;\n"
    "\n"
    "int getBlock(int x,int y,int z,int w,int v,int u){\n"
    "  if(x<0||y<0||z<0||w<0||v<0||u<0) return 0;\n"
    "  if(x>=12||y>=12||z>=12||w>=6||v>=6||u>=1) return 0;\n"
    "  int idx=x+12*(y+12*(z+12*(w+6*v)));\n"
    "  return int(texelFetch(worldTex,ivec2(idx%256,idx/256),0).r*255.0+0.5);\n"
    "}\n"
    "\n"
    "void main(){\n"
    "  vec2 ndc=(gl_FragCoord.xy/res)*2.0-1.0;\n"
    "  ndc.y=-ndc.y;\n"
    "  ndc.x*=res.x/res.y;\n"
    "  // camera-space ray: right,up,fwd,over,yonder,ultra\n"
    "  float rd[6];\n"
    "  rd[0]=ndc.x*fovTan; rd[1]=ndc.y*fovTan; rd[2]=1.0;\n"
    "  rd[3]=0.0; rd[4]=0.0; rd[5]=0.0;\n"
    "  // rotate by VM^T to get world-space ray\n"
    "  float wd[6];\n"
    "  for(int j=0;j<6;j++){wd[j]=0.0;for(int i=0;i<6;i++)wd[j]+=vm[i*6+j]*rd[i];}\n"
    "  float p[6];\n"
    "  p[0]=camPos.x;p[1]=camPos.y;p[2]=camPos.z;\n"
    "  p[3]=camExtra.x;p[4]=camExtra.y;p[5]=camExtra.z;\n"
    "  // 6D DDA\n"
    "  float tMax[6],tDelta[6];\n"
    "  int stp[6],vox[6];\n"
    "  for(int i=0;i<6;i++){\n"
    "    vox[i]=int(floor(p[i]));\n"
    "    float d=wd[i];\n"
    "    if(abs(d)<1e-8){tMax[i]=1e30;tDelta[i]=1e30;stp[i]=0;continue;}\n"
    "    stp[i]=d>0?1:-1; tDelta[i]=abs(1.0/d);\n"
    "    tMax[i]=d>0?(float(vox[i]+1)-p[i])/d:(p[i]-float(vox[i]))/(-d);\n"
    "  }\n"
    "  int ax=2,hitBlock=0;\n"
    "  bool hitneg=false;\n"
    "  for(int it=0;it<90;it++){\n"
    "    int b=getBlock(vox[0],vox[1],vox[2],vox[3],vox[4],vox[5]);\n"
    "    if(b!=0){hitBlock=b;break;}\n"
    "    float mn=tMax[0];ax=0;\n"
    "    for(int i=1;i<6;i++) if(tMax[i]<mn){mn=tMax[i];ax=i;}\n"
    "    vox[ax]+=stp[ax]; tMax[ax]+=tDelta[ax];\n"
    "    hitneg=stp[ax]<0;\n"
    "    if(vox[0]<-1||vox[1]<-1||vox[2]<-1||vox[0]>12||vox[1]>13||vox[2]>12) break;\n"
    "    if(vox[3]<0||vox[4]<0||vox[5]<0||vox[3]>=6||vox[4]>=6||vox[5]>=1) break;\n"
    "  }\n"
    "  if(hitBlock==0){\n"
    "    float t=gl_FragCoord.y/res.y;\n"
    "    col=vec4(mix(vec3(0.45,0.65,1.0),vec3(0.1,0.1,0.25),t),1.0);\n"
    "    return;\n"
    "  }\n"
    "  vec3 cols[8]=vec3[8](\n"
    "    vec3(0),vec3(0.47,0.31,0.16),vec3(0.25,0.48,0.18),\n"
    "    vec3(0.58,0.58,0.58),vec3(0.18,0.35,0.80),\n"
    "    vec3(0.76,0.69,0.22),vec3(0.32,0.22,0.14),vec3(0.22,0.62,0.22));\n"
    "  float sh[6]=float[6](0.72,0.88,0.54,1.0,0.76,0.82);\n"
    "  float s=sh[ax]*(hitneg?0.82:1.0);\n"
    "  col=vec4(cols[hitBlock]*s,1.0);\n"
    "}\n";

int main(void){
    InitWindow(1280,720,"voxel6d");
    SetTargetFPS(60);
    DisableCursor();
    genWorld();

    // Create world texture
    Image img={.data=texData,.width=TEXW,.height=TEXW,.mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
    worldTex=LoadTextureFromImage(img);
    syncTex();

    // Load shader
    Shader shader=LoadShaderFromMemory(VS_SRC,FS_SRC);
    int locRes    =GetShaderLocation(shader,"res");
    int locFovTan =GetShaderLocation(shader,"fovTan");
    int locCamPos =GetShaderLocation(shader,"camPos");
    int locCamExt =GetShaderLocation(shader,"camExtra");
    int locVm     =GetShaderLocation(shader,"vm[0]");
    int locWTex   =GetShaderLocation(shader,"worldTex");

    // Camera
    float cx=6,cy=7,cz=6;
    float cw=2.5f,cv=2.5f,cu=0.5f;
    float yaw=0,pitch=0,look_w=0,look_v=0;
    int selBlock=1;
    float mspd=5.f,lspd=0.0018f,espd=1.5f,lwspd=1.2f;

    while(!WindowShouldClose()){
        float dt=GetFrameTime();
        Vector2 md=GetMouseDelta();
        bool mmb=IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);
        if(!mmb){ yaw-=md.x*lspd; pitch-=md.y*lspd; pitch=fmaxf(-1.4f,fminf(1.4f,pitch)); }
        else { look_w+=md.x*lspd; look_v-=md.y*lspd; }

        if(IsKeyDown(KEY_Z)) look_w+=lwspd*dt;
        if(IsKeyDown(KEY_X)) look_w-=lwspd*dt;
        if(IsKeyDown(KEY_C)) look_v+=lwspd*dt;
        if(IsKeyDown(KEY_V)) look_v-=lwspd*dt;
        look_w=fmaxf(-1.4f,fminf(1.4f,look_w));
        look_v=fmaxf(-1.4f,fminf(1.4f,look_v));

        buildView(yaw,pitch,look_w,look_v);

        float fwX=VM[2][0],fwY=VM[2][1],fwZ=VM[2][2];
        float rtX=VM[0][0],rtY=VM[0][1],rtZ=VM[0][2];
        if(IsKeyDown(KEY_W)){cx+=fwX*mspd*dt;cy+=fwY*mspd*dt;cz+=fwZ*mspd*dt;}
        if(IsKeyDown(KEY_S)){cx-=fwX*mspd*dt;cy-=fwY*mspd*dt;cz-=fwZ*mspd*dt;}
        if(IsKeyDown(KEY_A)){cx-=rtX*mspd*dt;cy-=rtY*mspd*dt;cz-=rtZ*mspd*dt;}
        if(IsKeyDown(KEY_D)){cx+=rtX*mspd*dt;cy+=rtY*mspd*dt;cz+=rtZ*mspd*dt;}
        if(IsKeyDown(KEY_SPACE))    cy+=mspd*dt;
        if(IsKeyDown(KEY_LEFT_SHIFT))cy-=mspd*dt;
        if(IsKeyDown(KEY_Q)) cw+=espd*dt;
        if(IsKeyDown(KEY_E)) cw-=espd*dt;
        if(IsKeyDown(KEY_R)) cv+=espd*dt;
        if(IsKeyDown(KEY_F)) cv-=espd*dt;
        if(IsKeyDown(KEY_T)) cu+=espd*dt;
        if(IsKeyDown(KEY_G)) cu-=espd*dt;
        cw=fmaxf(0,fminf(WS-0.01f,cw));
        cv=fmaxf(0,fminf(VS-0.01f,cv));
        cu=fmaxf(0,fminf(US-0.01f,cu));

        for(int k=KEY_ONE;k<=KEY_SEVEN;k++) if(IsKeyPressed(k)) selBlock=k-KEY_ONE+1;

        // 6D DDA for interaction (center ray)
        float pos6[6]={cx,cy,cz,cw,cv,cu};
        // World-space ray direction: VM^T * (0,0,1,0,0,0) = column 2 of VM
        float dir6[6];
        for(int j=0;j<6;j++) dir6[j]=VM[j][2]; // column 2 = forward direction
        int hvox[6],lastAxis,lastStep;
        bool hit=dda6(pos6,dir6,hvox,&lastAxis,&lastStep);
        bool worldDirty=false;

        if(hit&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
            setBlock(hvox[0],hvox[1],hvox[2],hvox[3],hvox[4],hvox[5],0);
            worldDirty=true;
        }
        if(hit&&IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)){
            int pv[6]; for(int i=0;i<6;i++) pv[i]=hvox[i];
            pv[lastAxis]-=lastStep; // place on the face we hit from
            if(!getBlock(pv[0],pv[1],pv[2],pv[3],pv[4],pv[5])){
                setBlock(pv[0],pv[1],pv[2],pv[3],pv[4],pv[5],selBlock);
                worldDirty=true;
            }
        }
        if(worldDirty) syncTex();

        // ---- Set shader uniforms ----
        float res[2]={(float)GetScreenWidth(),(float)GetScreenHeight()};
        float fovTan=tanf(FOCAL_DEG*DEG2RAD*0.5f);
        float cp3[3]={cx,cy,cz}, ce3[3]={cw,cv,cu};
        float vmFlat[36];
        for(int i=0;i<6;i++) for(int j=0;j<6;j++) vmFlat[i*6+j]=VM[i][j];

        SetShaderValue(shader,locRes,res,SHADER_UNIFORM_VEC2);
        SetShaderValue(shader,locFovTan,&fovTan,SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,locCamPos,cp3,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locCamExt,ce3,SHADER_UNIFORM_VEC3);
        SetShaderValueV(shader,locVm,vmFlat,SHADER_UNIFORM_FLOAT,36);
        SetShaderValueTexture(shader,locWTex,worldTex);

        // ---- Render ----
        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(shader);
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),WHITE);
        EndShaderMode();

        // Crosshair
        int W=GetScreenWidth(),H=GetScreenHeight();
        DrawLine(W/2-10,H/2,W/2+10,H/2,WHITE);
        DrawLine(W/2,H/2-10,W/2,H/2+10,WHITE);

        // HUD
        DrawText("voxel6d - 6D raycast",10,10,20,WHITE);
        DrawText("WASD=move  Spc/Shift=Y  Q/E=W  R/F=V  T/G=U",10,34,13,LIGHTGRAY);
        DrawText("Z/X=lookW(WARP)  C/V=lookV  MMB+drag  LMB=break  RMB=place  1-7=block",10,50,13,YELLOW);
        char buf[160];
        snprintf(buf,sizeof(buf),"pos:%.1f,%.1f,%.1f  W=%.2f V=%.2f U=%.2f  lookW=%.2f lookV=%.2f",
            cx,cy,cz,cw,cv,cu,look_w,look_v);
        DrawText(buf,10,66,13,YELLOW);
        DrawFPS(10,82);
        EndDrawing();
    }
    UnloadShader(shader);
    UnloadTexture(worldTex);
    CloseWindow();
    return 0;
}

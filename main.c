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

static int hash3(int x,int z,int wv){
    unsigned int h=(unsigned)(x*73856093^z*19349663^wv*83492791);
    h^=h>>13;h*=1274126177u;h^=h>>16;return(int)(h&0x7fffffff);
}

static void genWorld(void){
    for(int w=0;w<WS;w++) for(int v=0;v<VS;v++) for(int u=0;u<US;u++)
    for(int x=0;x<XS;x++) for(int z=0;z<ZS;z++){
        int h=2+hash3(x,z,w*7+v*3+u)%5;
        for(int y=0;y<h-1;y++) setBlock(x,y,z,w,v,u,(y<h-3)?3:1);
        setBlock(x,h-1,z,w,v,u,2);
    }
}

// 6D view matrix: rows = camera basis vectors in world space
static float VM[6][6];
static void vmId(void){for(int i=0;i<6;i++)for(int j=0;j<6;j++)VM[i][j]=(i==j)?1.f:0.f;}
static void vmRot(int a,int b,float t){
    float c=cosf(t),s=sinf(t);
    for(int k=0;k<6;k++){float ra=VM[a][k],rb=VM[b][k];VM[a][k]=c*ra-s*rb;VM[b][k]=s*ra+c*rb;}
}
static void buildView(float yaw,float pitch,float lw,float lv){
    vmId();vmRot(0,2,yaw);vmRot(1,2,-pitch);vmRot(2,3,lw);vmRot(2,4,lv);
}

// World texture: RGBA8, 256x256, R channel = block type (0-7)
#define TEXW 256
static unsigned char texRGBA[TEXW*TEXW*4];
static Texture2D worldTex;

static void syncTex(void){
    memset(texRGBA,0,sizeof(texRGBA));
    for(int u=0;u<US;u++) for(int v=0;v<VS;v++) for(int w=0;w<WS;w++)
    for(int z=0;z<ZS;z++) for(int y=0;y<YS;y++) for(int x=0;x<XS;x++){
        int idx=(x+XS*(y+YS*(z+ZS*(w+WS*v))))*4;
        texRGBA[idx]=(unsigned char)world[x][y][z][w][v][u];
        texRGBA[idx+3]=255;
    }
    UpdateTexture(worldTex,texRGBA);
}

// 6D DDA for interaction (C side, center-screen ray)
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
    for(int it=0;it<80;it++){
        int b=getBlock(vox[0],vox[1],vox[2],vox[3],vox[4],vox[5]);
        if(b){for(int i=0;i<6;i++)hvox[i]=vox[i];return true;}
        float mn=tMax[0];*lastAxis=0;
        for(int i=1;i<6;i++) if(tMax[i]<mn){mn=tMax[i];*lastAxis=i;}
        *lastStep=step[*lastAxis];
        vox[*lastAxis]+=step[*lastAxis];tMax[*lastAxis]+=tDelta[*lastAxis];
        if(vox[0]<0||vox[1]<-1||vox[2]<0) break;
        if(vox[0]>=XS||vox[1]>=YS||vox[2]>=ZS) break;
        if(vox[3]<0||vox[4]<0||vox[5]<0) break;
        if(vox[3]>=WS||vox[4]>=VS||vox[5]>=US) break;
    }
    return false;
}

// Explicit VS: avoids AMD linker issues from default VS/FS variable mismatch
static const char *VS_SRC =
"#version 330\n"
"in vec3 vertexPosition;\n"
"in vec2 vertexTexCoord;\n"
"uniform mat4 mvp;\n"
"void main() { gl_Position = mvp*vec4(vertexPosition,1.0); }\n";

// Fragment shader — 6D DDA raytracer
// View matrix passed as 6 vec3 pairs: vmXYZ[i] = xyz components of row i,
// vmWVU[i] = wvu components of row i.
// World space ray = VM^T * camera_ray
static const char *FS_SRC =
"#version 330\n"
"out vec4 finalColor;\n"
"uniform vec2 res;\n"
"uniform float fovTan;\n"
"uniform vec3 camPos;\n"
"uniform vec3 camExt;\n"        // camera wvu
"uniform vec3 vmXYZ[6];\n"     // xyz part of each VM row
"uniform vec3 vmWVU[6];\n"     // wvu part of each VM row
"uniform sampler2D worldTex;\n"
"\n"
"int blockAt(ivec3 xyz, ivec3 wvu) {\n"
"  if (any(lessThan(xyz,ivec3(0)))||any(lessThan(wvu,ivec3(0)))) return 0;\n"
"  if (any(greaterThanEqual(xyz,ivec3(12,12,12)))) return 0;\n"
"  if (any(greaterThanEqual(wvu,ivec3(6,6,1)))) return 0;\n"
"  int idx = xyz.x + 12*(xyz.y + 12*(xyz.z + 12*(wvu.x + 6*wvu.y)));\n"
"  return int(texelFetch(worldTex, ivec2(idx%256,idx/256), 0).r*255.0+0.5);\n"
"}\n"
"\n"
"void main() {\n"
"  vec2 ndc = (gl_FragCoord.xy/res)*2.0-1.0;\n"
"  ndc.y = -ndc.y;\n"
"  ndc.x *= res.x/res.y;\n"
"\n"
"  // Camera-space ray: only right/up/fwd components, over/yonder/ultra=0\n"
"  vec3 rdCam = vec3(ndc.x*fovTan, ndc.y*fovTan, 1.0);\n"
"\n"
"  // World-space ray = VM^T * rdCam\n"
"  // vmXYZ[i] = i-th row of VM, xyz part. mat3(col0,col1,col2)*v = VM^T*rdCam for xyz.\n"
"  vec3 wdXYZ = mat3(vmXYZ[0],vmXYZ[1],vmXYZ[2]) * rdCam;\n"
"  vec3 wdWVU = mat3(vmWVU[0],vmWVU[1],vmWVU[2]) * rdCam;\n"
"\n"
"  // DDA setup — avoid ivec3(vec3) cast bugs on AMD\n"
"  ivec3 vXYZ = ivec3(int(floor(camPos.x)),int(floor(camPos.y)),int(floor(camPos.z)));\n"
"  ivec3 vWVU = ivec3(int(floor(camExt.x)),int(floor(camExt.y)),int(floor(camExt.z)));\n"
"  ivec3 stXYZ = ivec3(wdXYZ.x>0.?1:(wdXYZ.x<0.?-1:0),\n"
"                      wdXYZ.y>0.?1:(wdXYZ.y<0.?-1:0),\n"
"                      wdXYZ.z>0.?1:(wdXYZ.z<0.?-1:0));\n"
"  ivec3 stWVU = ivec3(wdWVU.x>0.?1:(wdWVU.x<0.?-1:0),\n"
"                      wdWVU.y>0.?1:(wdWVU.y<0.?-1:0),\n"
"                      wdWVU.z>0.?1:(wdWVU.z<0.?-1:0));\n"
"  vec3 tdXYZ = vec3(\n"
"    abs(wdXYZ.x)<1e-8?1e30:abs(1.0/wdXYZ.x),\n"
"    abs(wdXYZ.y)<1e-8?1e30:abs(1.0/wdXYZ.y),\n"
"    abs(wdXYZ.z)<1e-8?1e30:abs(1.0/wdXYZ.z));\n"
"  vec3 tdWVU = vec3(\n"
"    abs(wdWVU.x)<1e-8?1e30:abs(1.0/wdWVU.x),\n"
"    abs(wdWVU.y)<1e-8?1e30:abs(1.0/wdWVU.y),\n"
"    abs(wdWVU.z)<1e-8?1e30:abs(1.0/wdWVU.z));\n"
"  vec3 tmXYZ = vec3(\n"
"    wdXYZ.x>1e-8?(float(vXYZ.x+1)-camPos.x)/wdXYZ.x:wdXYZ.x<-1e-8?(camPos.x-float(vXYZ.x))/(-wdXYZ.x):1e30,\n"
"    wdXYZ.y>1e-8?(float(vXYZ.y+1)-camPos.y)/wdXYZ.y:wdXYZ.y<-1e-8?(camPos.y-float(vXYZ.y))/(-wdXYZ.y):1e30,\n"
"    wdXYZ.z>1e-8?(float(vXYZ.z+1)-camPos.z)/wdXYZ.z:wdXYZ.z<-1e-8?(camPos.z-float(vXYZ.z))/(-wdXYZ.z):1e30);\n"
"  vec3 tmWVU = vec3(\n"
"    wdWVU.x>1e-8?(float(vWVU.x+1)-camExt.x)/wdWVU.x:wdWVU.x<-1e-8?(camExt.x-float(vWVU.x))/(-wdWVU.x):1e30,\n"
"    wdWVU.y>1e-8?(float(vWVU.y+1)-camExt.y)/wdWVU.y:wdWVU.y<-1e-8?(camExt.y-float(vWVU.y))/(-wdWVU.y):1e30,\n"
"    wdWVU.z>1e-8?(float(vWVU.z+1)-camExt.z)/wdWVU.z:wdWVU.z<-1e-8?(camExt.z-float(vWVU.z))/(-wdWVU.z):1e30);\n"
"\n"
"  int hitBlock=0, hitAxis=2; bool hitneg=false;\n"
"\n"
"  for (int i=0; i<70; i++) {\n"
"    hitBlock = blockAt(vXYZ, vWVU);\n"
"    if (hitBlock != 0) break;\n"
"\n"
"    float m0=min(tmXYZ.x,min(tmXYZ.y,tmXYZ.z));\n"
"    float m1=min(tmWVU.x,min(tmWVU.y,tmWVU.z));\n"
"\n"
"    if (m0 <= m1) {\n"
"      if (tmXYZ.x<=tmXYZ.y && tmXYZ.x<=tmXYZ.z) {\n"
"        vXYZ.x+=stXYZ.x; tmXYZ.x+=tdXYZ.x; hitneg=stXYZ.x<0; hitAxis=0;\n"
"      } else if (tmXYZ.y<=tmXYZ.z) {\n"
"        vXYZ.y+=stXYZ.y; tmXYZ.y+=tdXYZ.y; hitneg=stXYZ.y<0; hitAxis=1;\n"
"      } else {\n"
"        vXYZ.z+=stXYZ.z; tmXYZ.z+=tdXYZ.z; hitneg=stXYZ.z<0; hitAxis=2;\n"
"      }\n"
"    } else {\n"
"      if (tmWVU.x<=tmWVU.y && tmWVU.x<=tmWVU.z) {\n"
"        vWVU.x+=stWVU.x; tmWVU.x+=tdWVU.x; hitneg=stWVU.x<0; hitAxis=3;\n"
"      } else if (tmWVU.y<=tmWVU.z) {\n"
"        vWVU.y+=stWVU.y; tmWVU.y+=tdWVU.y; hitneg=stWVU.y<0; hitAxis=4;\n"
"      } else {\n"
"        vWVU.z+=stWVU.z; tmWVU.z+=tdWVU.z; hitneg=stWVU.z<0; hitAxis=5;\n"
"      }\n"
"    }\n"
"    if (any(lessThan(vXYZ,ivec3(-1)))||any(greaterThan(vXYZ,ivec3(13)))) break;\n"
"    if (any(lessThan(vWVU,ivec3(0)))||any(greaterThanEqual(vWVU,ivec3(6,6,1)))) break;\n"
"  }\n"
"\n"
"  if (hitBlock==0) {\n"
"    float t = gl_FragCoord.y/res.y;\n"
"    finalColor = vec4(mix(vec3(0.45,0.65,1.0),vec3(0.08,0.08,0.22),t),1.0);\n"
"    return;\n"
"  }\n"
"\n"
"  vec3 bc;\n"
"  if(hitBlock==1) bc=vec3(0.47,0.31,0.16);\n"
"  else if(hitBlock==2) bc=vec3(0.25,0.48,0.18);\n"
"  else if(hitBlock==3) bc=vec3(0.58,0.58,0.58);\n"
"  else if(hitBlock==4) bc=vec3(0.18,0.35,0.80);\n"
"  else if(hitBlock==5) bc=vec3(0.76,0.69,0.22);\n"
"  else if(hitBlock==6) bc=vec3(0.32,0.22,0.14);\n"
"  else bc=vec3(0.22,0.62,0.22);\n"
"  float sh;\n"
"  if(hitAxis==0) sh=0.72; else if(hitAxis==1) sh=0.88;\n"
"  else if(hitAxis==2) sh=0.54; else if(hitAxis==3) sh=1.0;\n"
"  else if(hitAxis==4) sh=0.76; else sh=0.82;\n"
"  finalColor = vec4(bc*sh*(hitneg?0.82:1.0),1.0);\n"
"}\n";

int main(void){
    InitWindow(1280,720,"voxel6d");
    SetTargetFPS(60);
    DisableCursor();
    genWorld();

    // World texture (RGBA8 for max compatibility)
    Image img={.data=texRGBA,.width=TEXW,.height=TEXW,.mipmaps=1,
               .format=PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    worldTex=LoadTextureFromImage(img);
    syncTex();

    // Explicit VS avoids AMD linker errors from VS/FS interface mismatch
    Shader shader=LoadShaderFromMemory(VS_SRC,FS_SRC);
    int locRes    =GetShaderLocation(shader,"res");
    int locFovTan =GetShaderLocation(shader,"fovTan");
    int locCamPos =GetShaderLocation(shader,"camPos");
    int locCamExt =GetShaderLocation(shader,"camExt");
    int locXYZ    =GetShaderLocation(shader,"vmXYZ[0]");
    int locWVU    =GetShaderLocation(shader,"vmWVU[0]");
    int locWTex   =GetShaderLocation(shader,"worldTex");

    // Camera
    float cx=6,cy=7,cz=6, cw=2.5f,cv=2.5f,cu=0.5f;
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
        if(IsKeyDown(KEY_Q)) cw+=espd*dt; if(IsKeyDown(KEY_E)) cw-=espd*dt;
        if(IsKeyDown(KEY_R)) cv+=espd*dt; if(IsKeyDown(KEY_F)) cv-=espd*dt;
        if(IsKeyDown(KEY_T)) cu+=espd*dt; if(IsKeyDown(KEY_G)) cu-=espd*dt;
        cw=fmaxf(0,fminf(WS-0.01f,cw));
        cv=fmaxf(0,fminf(VS-0.01f,cv));
        cu=fmaxf(0,fminf(US-0.01f,cu));

        for(int k=KEY_ONE;k<=KEY_SEVEN;k++) if(IsKeyPressed(k)) selBlock=k-KEY_ONE+1;

        // Interaction: 6D DDA along forward direction
        float pos6[6]={cx,cy,cz,cw,cv,cu};
        float dir6[6]; for(int j=0;j<6;j++) dir6[j]=VM[j][2]; // VM col 2 = world-space fwd
        int hvox[6],lastAxis,lastStep;
        bool hit=dda6(pos6,dir6,hvox,&lastAxis,&lastStep);

        if(hit&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
            setBlock(hvox[0],hvox[1],hvox[2],hvox[3],hvox[4],hvox[5],0); syncTex();
        }
        if(hit&&IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)){
            int pv[6]; for(int i=0;i<6;i++) pv[i]=hvox[i];
            pv[lastAxis]-=lastStep;
            if(!getBlock(pv[0],pv[1],pv[2],pv[3],pv[4],pv[5])){
                setBlock(pv[0],pv[1],pv[2],pv[3],pv[4],pv[5],selBlock); syncTex();
            }
        }

        // Build uniform data
        float res2[2]={(float)GetScreenWidth(),(float)GetScreenHeight()};
        float fovTan=tanf(FOCAL_DEG*DEG2RAD*0.5f);
        float cp3[3]={cx,cy,cz}, ce3[3]={cw,cv,cu};
        // Split VM rows into xyz and wvu parts
        float vmXYZ[18], vmWVU[18]; // 6 rows × 3 floats
        for(int i=0;i<6;i++){
            vmXYZ[i*3+0]=VM[i][0]; vmXYZ[i*3+1]=VM[i][1]; vmXYZ[i*3+2]=VM[i][2];
            vmWVU[i*3+0]=VM[i][3]; vmWVU[i*3+1]=VM[i][4]; vmWVU[i*3+2]=VM[i][5];
        }

        SetShaderValue(shader,locRes,res2,SHADER_UNIFORM_VEC2);
        SetShaderValue(shader,locFovTan,&fovTan,SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,locCamPos,cp3,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locCamExt,ce3,SHADER_UNIFORM_VEC3);
        SetShaderValueV(shader,locXYZ,vmXYZ,SHADER_UNIFORM_VEC3,6);
        SetShaderValueV(shader,locWVU,vmWVU,SHADER_UNIFORM_VEC3,6);
        SetShaderValueTexture(shader,locWTex,worldTex);

        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(shader);
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),WHITE);
        EndShaderMode();

        int W=GetScreenWidth(),H=GetScreenHeight();
        DrawLine(W/2-10,H/2,W/2+10,H/2,WHITE);
        DrawLine(W/2,H/2-10,W/2,H/2+10,WHITE);
        DrawText("voxel6d - 6D raycast",10,10,20,WHITE);
        DrawText("WASD/Space/Shift=move  Q/E=W  R/F=V  T/G=U  LMB=break  RMB=place  1-7=block",10,34,13,LIGHTGRAY);
        DrawText("Z/X=lookW(WARP!)  C/V=lookV  MMB+drag",10,50,13,YELLOW);
        char buf[160];
        snprintf(buf,sizeof(buf),"pos:%.1f %.1f %.1f  W=%.2f V=%.2f U=%.2f  lookW=%.2f lookV=%.2f  block:%d",
            cx,cy,cz,cw,cv,cu,look_w,look_v,selBlock);
        DrawText(buf,10,66,13,YELLOW);
        DrawFPS(10,82);
        EndDrawing();
    }
    UnloadShader(shader);
    UnloadTexture(worldTex);
    CloseWindow();
    return 0;
}

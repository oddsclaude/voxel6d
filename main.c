// voxel6d — 6D first-person voxel game, GPU 6D DDA raytracer
// 64^6 fully procedural world (no edit storage - would be 64GB)
// Controls: WASD=move, Space/Shift=Y, Q/E=W, R/F=V
//           Z/X=lookW(WARP!), C/V=lookV, MMB+drag=look extra dims
//           Tab=unlock mouse, Click=lock mouse
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WLD 64   // world size in every axis
#define FOCAL_DEG 70.0f

// 6D view matrix
static float VM[6][6];
static void vmId(void){for(int i=0;i<6;i++)for(int j=0;j<6;j++)VM[i][j]=(i==j)?1.f:0.f;}
static void vmRot(int a,int b,float t){
    float c=cosf(t),s=sinf(t);
    for(int k=0;k<6;k++){float ra=VM[a][k],rb=VM[b][k];VM[a][k]=c*ra-s*rb;VM[b][k]=s*ra+c*rb;}
}
static void buildView(float yaw,float pitch,float lw,float lv){
    vmId();vmRot(0,2,yaw);vmRot(1,2,-pitch);vmRot(2,3,lw);vmRot(2,4,lv);
}

// C-side DDA for crosshair hit detection
// terrain: block exists if y < 8 + hash(x,z,w,v)%8
static int terrainH(int x,int z,int w,int v){
    unsigned int h=(unsigned)(x*73856093^z*19349663^(w*7+v*3)*83492791);
    h^=h>>13;h*=1274126177u;h^=h>>16;return 8+(int)(h%8u);
}
static int blockAt6(int x,int y,int z,int w,int v){
    if(x<0||y<0||z<0||w<0||v<0) return 0;
    if(x>=WLD||y>=WLD||z>=WLD||w>=WLD||v>=WLD) return 0;
    int h=terrainH(x,z,w,v);
    if(y>=h) return 0;
    if(y<h-1) return (y<h-3)?3:1;
    return 2;
}
static bool dda6(float pos[6],float dir[6],int hvox[6]){
    float tM[6],tD[6]; int st[6],v[6];
    for(int i=0;i<6;i++){
        v[i]=(int)floorf(pos[i]);
        float d=dir[i];
        if(fabsf(d)<1e-8f){tM[i]=1e30f;tD[i]=1e30f;st[i]=0;continue;}
        st[i]=d>0?1:-1; tD[i]=fabsf(1.f/d);
        tM[i]=d>0?(v[i]+1-pos[i])/d:(pos[i]-v[i])/(-d);
    }
    for(int it=0;it<200;it++){
        if(blockAt6(v[0],v[1],v[2],v[3],v[4])){ for(int i=0;i<6;i++)hvox[i]=v[i]; return true; }
        float mn=tM[0]; int ax=0;
        for(int i=1;i<6;i++) if(tM[i]<mn){mn=tM[i];ax=i;}
        v[ax]+=st[ax]; tM[ax]+=tD[ax];
        if(v[0]<-1||v[0]>WLD||v[2]<-1||v[2]>WLD) break;
        if(v[3]<0||v[3]>=WLD||v[4]<0||v[4]>=WLD) break;
    }
    return false;
}

static const char *VS_SRC =
"#version 330\n"
"in vec3 vertexPosition;\n"
"in vec2 vertexTexCoord;\n"
"uniform mat4 mvp;\n"
"void main(){gl_Position=mvp*vec4(vertexPosition,1.0);}\n";

static const char *FS_SRC =
"#version 330\n"
"out vec4 finalColor;\n"
"uniform vec2 res;\n"
"uniform float fovTan;\n"
"uniform vec3 camPos;\n"
"uniform vec3 camExt;\n"
"uniform vec3 r0xyz;\n"
"uniform vec3 r1xyz;\n"
"uniform vec3 r2xyz;\n"
"uniform vec3 r0wvu;\n"
"uniform vec3 r1wvu;\n"
"uniform vec3 r2wvu;\n"
"\n"
"int terrainH(int x,int z,int w,int v){\n"
"  uint h=uint(x)*73856093u^uint(z)*19349663u^uint(w*7+v*3)*83492791u;\n"
"  h^=h>>13u;h*=1274126177u;h^=h>>16u;\n"
"  return 8+int(h%8u);\n"
"}\n"
"\n"
"int blockAt(ivec3 xyz,ivec2 wv){\n"
"  if(any(lessThan(xyz,ivec3(0)))||any(lessThan(wv,ivec2(0)))) return 0;\n"
"  if(any(greaterThanEqual(xyz,ivec3(64)))|| any(greaterThanEqual(wv,ivec2(64)))) return 0;\n"
"  int h=terrainH(xyz.x,xyz.z,wv.x,wv.y);\n"
"  if(xyz.y>=h) return 0;\n"
"  if(xyz.y<h-1) return (xyz.y<h-3)?3:1;\n"
"  return 2;\n"
"}\n"
"\n"
"void main(){\n"
"  vec2 ndc=(gl_FragCoord.xy/res)*2.0-1.0;\n"
"  ndc.x*=res.x/res.y;\n"
"\n"
"  vec3 rdc=vec3(ndc.x*fovTan,ndc.y*fovTan,1.0);\n"
"  vec3 wdXYZ=mat3(r0xyz,r1xyz,r2xyz)*rdc;\n"
"  vec3 wdWVU=mat3(r0wvu,r1wvu,r2wvu)*rdc;\n"
"\n"
"  ivec3 vX=ivec3(int(floor(camPos.x)),int(floor(camPos.y)),int(floor(camPos.z)));\n"
"  ivec2 vW=ivec2(int(floor(camExt.x)),int(floor(camExt.y)));\n"
"  ivec3 sX=ivec3(wdXYZ.x>0.?1:(wdXYZ.x<0.?-1:0),wdXYZ.y>0.?1:(wdXYZ.y<0.?-1:0),wdXYZ.z>0.?1:(wdXYZ.z<0.?-1:0));\n"
"  ivec2 sW=ivec2(wdWVU.x>0.?1:(wdWVU.x<0.?-1:0),wdWVU.y>0.?1:(wdWVU.y<0.?-1:0));\n"
"  vec3 tdX=vec3(abs(wdXYZ.x)<1e-8?1e30:abs(1./wdXYZ.x),abs(wdXYZ.y)<1e-8?1e30:abs(1./wdXYZ.y),abs(wdXYZ.z)<1e-8?1e30:abs(1./wdXYZ.z));\n"
"  vec2 tdW=vec2(abs(wdWVU.x)<1e-8?1e30:abs(1./wdWVU.x),abs(wdWVU.y)<1e-8?1e30:abs(1./wdWVU.y));\n"
"  vec3 tmX=vec3(\n"
"    wdXYZ.x>1e-8?(float(vX.x+1)-camPos.x)/wdXYZ.x:wdXYZ.x<-1e-8?(camPos.x-float(vX.x))/(-wdXYZ.x):1e30,\n"
"    wdXYZ.y>1e-8?(float(vX.y+1)-camPos.y)/wdXYZ.y:wdXYZ.y<-1e-8?(camPos.y-float(vX.y))/(-wdXYZ.y):1e30,\n"
"    wdXYZ.z>1e-8?(float(vX.z+1)-camPos.z)/wdXYZ.z:wdXYZ.z<-1e-8?(camPos.z-float(vX.z))/(-wdXYZ.z):1e30);\n"
"  vec2 tmW=vec2(\n"
"    wdWVU.x>1e-8?(float(vW.x+1)-camExt.x)/wdWVU.x:wdWVU.x<-1e-8?(camExt.x-float(vW.x))/(-wdWVU.x):1e30,\n"
"    wdWVU.y>1e-8?(float(vW.y+1)-camExt.y)/wdWVU.y:wdWVU.y<-1e-8?(camExt.y-float(vW.y))/(-wdWVU.y):1e30);\n"
"\n"
"  int hit=0,ax=2; bool neg=false;\n"
"  for(int i=0;i<300;i++){\n"
"    hit=blockAt(vX,vW); if(hit!=0) break;\n"
"    float m0=min(tmX.x,min(tmX.y,tmX.z)),m1=min(tmW.x,tmW.y);\n"
"    if(m0<=m1){\n"
"      if(tmX.x<=tmX.y&&tmX.x<=tmX.z){vX.x+=sX.x;tmX.x+=tdX.x;neg=sX.x<0;ax=0;}\n"
"      else if(tmX.y<=tmX.z){vX.y+=sX.y;tmX.y+=tdX.y;neg=sX.y<0;ax=1;}\n"
"      else{vX.z+=sX.z;tmX.z+=tdX.z;neg=sX.z<0;ax=2;}\n"
"    } else {\n"
"      if(tmW.x<=tmW.y){vW.x+=sW.x;tmW.x+=tdW.x;neg=sW.x<0;ax=3;}\n"
"      else{vW.y+=sW.y;tmW.y+=tdW.y;neg=sW.y<0;ax=4;}\n"
"    }\n"
"    if(vX.x<-1||vX.x>64||vX.z<-1||vX.z>64) break;\n"
"    if(vW.x<0||vW.x>=64||vW.y<0||vW.y>=64) break;\n"
"  }\n"
"\n"
"  if(hit==0){float t=gl_FragCoord.y/res.y;finalColor=vec4(mix(vec3(0.45,0.65,1.),vec3(0.08,0.08,0.22),t),1.);return;}\n"
"\n"
"  vec3 bc;\n"
"  if(hit==1) bc=vec3(0.47,0.31,0.16);\n"
"  else if(hit==2) bc=vec3(0.25,0.48,0.18);\n"
"  else if(hit==3) bc=vec3(0.58,0.58,0.58);\n"
"  else if(hit==4) bc=vec3(0.18,0.35,0.80);\n"
"  else if(hit==5) bc=vec3(0.76,0.69,0.22);\n"
"  else if(hit==6) bc=vec3(0.32,0.22,0.14);\n"
"  else bc=vec3(0.22,0.62,0.22);\n"
"  float sh;\n"
"  if(ax==0) sh=0.72; else if(ax==1) sh=0.88;\n"
"  else if(ax==2) sh=0.54; else if(ax==3) sh=1.0;\n"
"  else if(ax==4) sh=0.76; else sh=0.82;\n"
"  finalColor=vec4(bc*sh*(neg?0.82:1.0),1.0);\n"
"}\n";

int main(void){
    InitWindow(1280,720,"voxel6d");
    SetTargetFPS(60);
    SetExitKey(0);

    Shader shader=LoadShaderFromMemory(VS_SRC,FS_SRC);
    int locRes   =GetShaderLocation(shader,"res");
    int locFov   =GetShaderLocation(shader,"fovTan");
    int locCP    =GetShaderLocation(shader,"camPos");
    int locCE    =GetShaderLocation(shader,"camExt");
    int locR0xyz =GetShaderLocation(shader,"r0xyz");
    int locR1xyz =GetShaderLocation(shader,"r1xyz");
    int locR2xyz =GetShaderLocation(shader,"r2xyz");
    int locR0wvu =GetShaderLocation(shader,"r0wvu");
    int locR1wvu =GetShaderLocation(shader,"r1wvu");
    int locR2wvu =GetShaderLocation(shader,"r2wvu");

    float cx=32,cy=22,cz=32, cw=32,cv=32,cu=0;
    float yaw=0,pitch=0.3f,look_w=0,look_v=0;
    float mspd=8.f,lspd=0.0018f,espd=4.f,lwspd=1.2f;
    bool locked=false;

    while(!WindowShouldClose()&&!IsKeyPressed(KEY_ESCAPE)){
        float dt=GetFrameTime();

        if(!locked&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){DisableCursor();locked=true;}
        if(locked&&IsKeyPressed(KEY_TAB)){EnableCursor();locked=false;}

        Vector2 md=locked?GetMouseDelta():(Vector2){0,0};
        bool mmb=locked&&IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);

        if(!mmb){
            yaw  +=md.x*lspd;
            pitch+=md.y*lspd;
            pitch=fmaxf(-1.4f,fminf(1.4f,pitch));
        } else {
            look_w+=md.x*lspd;
            look_v-=md.y*lspd;
        }
        if(locked&&IsKeyDown(KEY_Z)) look_w+=lwspd*dt;
        if(locked&&IsKeyDown(KEY_X)) look_w-=lwspd*dt;
        if(locked&&IsKeyDown(KEY_C)) look_v+=lwspd*dt;
        if(locked&&IsKeyDown(KEY_V)) look_v-=lwspd*dt;
        look_w=fmaxf(-1.4f,fminf(1.4f,look_w));
        look_v=fmaxf(-1.4f,fminf(1.4f,look_v));

        buildView(yaw,pitch,look_w,look_v);

        float fwX=VM[2][0],fwY=VM[2][1],fwZ=VM[2][2];
        float rtX=VM[0][0],rtY=VM[0][1],rtZ=VM[0][2];
        if(IsKeyDown(KEY_W)){cx+=fwX*mspd*dt;cy+=fwY*mspd*dt;cz+=fwZ*mspd*dt;}
        if(IsKeyDown(KEY_S)){cx-=fwX*mspd*dt;cy-=fwY*mspd*dt;cz-=fwZ*mspd*dt;}
        if(IsKeyDown(KEY_A)){cx-=rtX*mspd*dt;cy-=rtY*mspd*dt;cz-=rtZ*mspd*dt;}
        if(IsKeyDown(KEY_D)){cx+=rtX*mspd*dt;cy+=rtY*mspd*dt;cz+=rtZ*mspd*dt;}
        if(IsKeyDown(KEY_SPACE)) cy+=mspd*dt;
        if(IsKeyDown(KEY_LEFT_SHIFT)) cy-=mspd*dt;
        if(IsKeyDown(KEY_Q)) cw+=espd*dt; if(IsKeyDown(KEY_E)) cw-=espd*dt;
        if(IsKeyDown(KEY_R)) cv+=espd*dt; if(IsKeyDown(KEY_F)) cv-=espd*dt;
        cw=fmaxf(0,fminf(WLD-0.01f,cw)); cv=fmaxf(0,fminf(WLD-0.01f,cv));

        float res2[2]={(float)GetScreenWidth(),(float)GetScreenHeight()};
        float fovTan=tanf(FOCAL_DEG*DEG2RAD*0.5f);
        float cp3[3]={cx,cy,cz},ce3[3]={cw,cv,cu};
        float r0xyz[3]={VM[0][0],VM[0][1],VM[0][2]};
        float r1xyz[3]={VM[1][0],VM[1][1],VM[1][2]};
        float r2xyz[3]={VM[2][0],VM[2][1],VM[2][2]};
        float r0wvu[3]={VM[0][3],VM[0][4],VM[0][5]};
        float r1wvu[3]={VM[1][3],VM[1][4],VM[1][5]};
        float r2wvu[3]={VM[2][3],VM[2][4],VM[2][5]};

        SetShaderValue(shader,locRes,res2,SHADER_UNIFORM_VEC2);
        SetShaderValue(shader,locFov,&fovTan,SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,locCP,cp3,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locCE,ce3,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locR0xyz,r0xyz,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locR1xyz,r1xyz,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locR2xyz,r2xyz,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locR0wvu,r0wvu,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locR1wvu,r1wvu,SHADER_UNIFORM_VEC3);
        SetShaderValue(shader,locR2wvu,r2wvu,SHADER_UNIFORM_VEC3);

        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(shader);
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),WHITE);
        EndShaderMode();

        int W=GetScreenWidth(),H=GetScreenHeight();
        DrawLine(W/2-10,H/2,W/2+10,H/2,WHITE);
        DrawLine(W/2,H/2-10,W/2,H/2+10,WHITE);
        if(!locked) DrawText("CLICK TO LOCK MOUSE",W/2-120,H/2-40,20,YELLOW);
        DrawText("voxel6d 6D raycast  |  64^6 world",10,10,20,WHITE);
        DrawText("WASD/Space/Shift=move  Q/E=W  R/F=V  Z/X=lookW(WARP)  C/V=lookV  MMB+drag  Tab=unlock",10,34,13,LIGHTGRAY);
        char buf[160];
        snprintf(buf,sizeof(buf),"xyz:%.1f,%.1f,%.1f  W=%.1f V=%.1f  lookW=%.2f lookV=%.2f",cx,cy,cz,cw,cv,look_w,look_v);
        DrawText(buf,10,50,13,YELLOW);
        DrawFPS(10,66);
        EndDrawing();
    }
    UnloadShader(shader); CloseWindow();
    return 0;
}

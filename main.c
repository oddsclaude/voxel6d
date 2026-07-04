// voxel6d — 6D first-person voxel game, GPU 6D DDA raytracer
// Controls: WASD=move, Space/Shift=Y, Q/E=W, R/F=V
//           Z/X=lookW(WARP!), MMB+drag=look extra dims
//           LMB=break, RMB=place, 1-7=block, Tab=unlock mouse
// Click window to lock mouse (Wayland-safe)
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XS 10
#define YS 10
#define ZS 10
#define WS 5
#define VS 5
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

static int hash6(int x,int z,int wv){
    unsigned int h=(unsigned)(x*73856093^z*19349663^wv*83492791);
    h^=h>>13;h*=1274126177u;h^=h>>16;return(int)(h&0x7fffffff);
}

// Base terrain height at (x,z,w,v)
static int terrainH(int x,int z,int w,int v){return 2+hash6(x,z,w*7+v*3)%5;}

static void genWorld(void){
    for(int w=0;w<WS;w++) for(int v=0;v<VS;v++) for(int u=0;u<US;u++)
    for(int x=0;x<XS;x++) for(int z=0;z<ZS;z++){
        int h=terrainH(x,z,w,v);
        for(int y=0;y<h-1;y++) setBlock(x,y,z,w,v,u,(y<h-3)?3:1);
        setBlock(x,h-1,z,w,v,u,2);
    }
}

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

// World edits: packed as flat texture for shader
// 1 texel per block, R = block type override (0 = use procedural, 1..7 = override)
// But encoding "empty" vs "procedural" is tricky. Let's use 0=procedural, 255=air, 1..7=block
#define TEXW 256
static unsigned char texR[TEXW*TEXW*4]; // RGBA, R=type
static Texture2D editTex;

static void syncEditTex(void){
    memset(texR,0,sizeof(texR)); // 0 = use procedural terrain
    for(int u=0;u<US;u++) for(int v=0;v<VS;v++) for(int w=0;w<WS;w++)
    for(int z=0;z<ZS;z++) for(int y=0;y<YS;y++) for(int x=0;x<XS;x++){
        int idx=(x+XS*(y+YS*(z+ZS*(w+WS*v))))*4;
        int b=world[x][y][z][w][v][u];
        int h=terrainH(x,z,w,v);
        // 0 = matches procedural (no override needed)
        // store 255 for air-override, 1..7 for block override
        if(b==0 && y<h) texR[idx]=255; // explicitly air where terrain would be (player broke)
        else if(b!=0 && y>=h) texR[idx]=(unsigned char)b; // placed above terrain
        // else 0 = let procedural handle it
        texR[idx+3]=255;
    }
    UpdateTexture(editTex,texR);
}

// C-side 6D DDA for interaction
static bool dda6(float pos[6],float dir[6],int hvox[6],int *lastAxis,int *lastStep){
    float tM[6],tD[6]; int st[6],v[6];
    for(int i=0;i<6;i++){
        v[i]=(int)floorf(pos[i]);
        float d=dir[i];
        if(fabsf(d)<1e-8f){tM[i]=1e30f;tD[i]=1e30f;st[i]=0;continue;}
        st[i]=d>0?1:-1; tD[i]=fabsf(1.f/d);
        tM[i]=d>0?(v[i]+1-pos[i])/d:(pos[i]-v[i])/(-d);
    }
    *lastAxis=2;*lastStep=1;
    for(int it=0;it<80;it++){
        int b=getBlock(v[0],v[1],v[2],v[3],v[4],v[5]);
        if(b){for(int i=0;i<6;i++)hvox[i]=v[i];return true;}
        float mn=tM[0];*lastAxis=0;
        for(int i=1;i<6;i++) if(tM[i]<mn){mn=tM[i];*lastAxis=i;}
        *lastStep=st[*lastAxis];
        v[*lastAxis]+=st[*lastAxis];tM[*lastAxis]+=tD[*lastAxis];
        if(v[0]<0||v[1]<-1||v[2]<0||v[0]>=XS||v[1]>=YS||v[2]>=ZS) break;
        if(v[3]<0||v[4]<0||v[5]<0||v[3]>=WS||v[4]>=VS||v[5]>=US) break;
    }
    return false;
}

// Shaders — procedural terrain (no texture needed for base blocks)
// Edits stored in editTex sampler
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
"uniform vec3 camPos;\n"    // xyz
"uniform vec3 camExt;\n"    // wvu
// VM rows 0,1,2 split into xyz and wvu parts (6 individual vec3 uniforms)
"uniform vec3 r0xyz;\n"  // VM row 0 (right) xyz
"uniform vec3 r1xyz;\n"  // VM row 1 (up) xyz
"uniform vec3 r2xyz;\n"  // VM row 2 (fwd) xyz
"uniform vec3 r0wvu;\n"  // VM row 0 (right) wvu
"uniform vec3 r1wvu;\n"  // VM row 1 (up) wvu
"uniform vec3 r2wvu;\n"  // VM row 2 (fwd) wvu
"uniform sampler2D editTex;\n"
"\n"
"// Simple hash matching C-side terrainH\n"
"int terrainH(int x,int z,int w,int v){\n"
"  uint h=uint(x)*73856093u^uint(z)*19349663u^uint(w*7+v*3)*83492791u;\n"
"  h^=h>>13u; h*=1274126177u; h^=h>>16u;\n"
"  return 2+int(h%5u);\n"
"}\n"
"\n"
"int blockAt(ivec3 xyz,ivec3 wvu){\n"
"  if(any(lessThan(xyz,ivec3(0)))||any(lessThan(wvu,ivec3(0)))) return 0;\n"
"  if(any(greaterThanEqual(xyz,ivec3(10,10,10)))) return 0;\n"
"  if(any(greaterThanEqual(wvu,ivec3(5,5,1)))) return 0;\n"
// Check edit texture first
"  int idx=xyz.x+10*(xyz.y+10*(xyz.z+10*(wvu.x+5*wvu.y)));\n"
"  int eR=int(texelFetch(editTex,ivec2(idx%256,idx/256),0).r*255.0+0.5);\n"
"  if(eR==255) return 0;\n"           // player broke this block
"  if(eR>0&&eR<8) return eR;\n"      // player placed block
// Procedural terrain
"  int h=terrainH(xyz.x,xyz.z,wvu.x,wvu.y);\n"
"  if(xyz.y>=h) return 0;\n"
"  if(xyz.y<h-1) return (xyz.y<h-3)?3:1;\n"
"  return 2;\n"
"}\n"
"\n"
"void main(){\n"
"  vec2 ndc=(gl_FragCoord.xy/res)*2.0-1.0;\n"
"  ndc.y=-ndc.y; ndc.x*=res.x/res.y;\n"
"\n"
"  vec3 rdc=vec3(ndc.x*fovTan,ndc.y*fovTan,1.0);\n"
// World-space ray = VM^T * camera_ray (only rows 0,1,2 matter since rdc_wvu=0)
"  vec3 wdXYZ=mat3(r0xyz,r1xyz,r2xyz)*rdc;\n"
"  vec3 wdWVU=mat3(r0wvu,r1wvu,r2wvu)*rdc;\n"
"\n"
"  ivec3 vX=ivec3(int(floor(camPos.x)),int(floor(camPos.y)),int(floor(camPos.z)));\n"
"  ivec3 vW=ivec3(int(floor(camExt.x)),int(floor(camExt.y)),int(floor(camExt.z)));\n"
"  ivec3 sX=ivec3(wdXYZ.x>0.?1:(wdXYZ.x<0.?-1:0),wdXYZ.y>0.?1:(wdXYZ.y<0.?-1:0),wdXYZ.z>0.?1:(wdXYZ.z<0.?-1:0));\n"
"  ivec3 sW=ivec3(wdWVU.x>0.?1:(wdWVU.x<0.?-1:0),wdWVU.y>0.?1:(wdWVU.y<0.?-1:0),wdWVU.z>0.?1:(wdWVU.z<0.?-1:0));\n"
"  vec3 tdX=vec3(abs(wdXYZ.x)<1e-8?1e30:abs(1./wdXYZ.x),abs(wdXYZ.y)<1e-8?1e30:abs(1./wdXYZ.y),abs(wdXYZ.z)<1e-8?1e30:abs(1./wdXYZ.z));\n"
"  vec3 tdW=vec3(abs(wdWVU.x)<1e-8?1e30:abs(1./wdWVU.x),abs(wdWVU.y)<1e-8?1e30:abs(1./wdWVU.y),abs(wdWVU.z)<1e-8?1e30:abs(1./wdWVU.z));\n"
"  vec3 tmX=vec3(\n"
"    wdXYZ.x>1e-8?(float(vX.x+1)-camPos.x)/wdXYZ.x:wdXYZ.x<-1e-8?(camPos.x-float(vX.x))/(-wdXYZ.x):1e30,\n"
"    wdXYZ.y>1e-8?(float(vX.y+1)-camPos.y)/wdXYZ.y:wdXYZ.y<-1e-8?(camPos.y-float(vX.y))/(-wdXYZ.y):1e30,\n"
"    wdXYZ.z>1e-8?(float(vX.z+1)-camPos.z)/wdXYZ.z:wdXYZ.z<-1e-8?(camPos.z-float(vX.z))/(-wdXYZ.z):1e30);\n"
"  vec3 tmW=vec3(\n"
"    wdWVU.x>1e-8?(float(vW.x+1)-camExt.x)/wdWVU.x:wdWVU.x<-1e-8?(camExt.x-float(vW.x))/(-wdWVU.x):1e30,\n"
"    wdWVU.y>1e-8?(float(vW.y+1)-camExt.y)/wdWVU.y:wdWVU.y<-1e-8?(camExt.y-float(vW.y))/(-wdWVU.y):1e30,\n"
"    wdWVU.z>1e-8?(float(vW.z+1)-camExt.z)/wdWVU.z:wdWVU.z<-1e-8?(camExt.z-float(vW.z))/(-wdWVU.z):1e30);\n"
"\n"
"  int hit=0,ax=2; bool neg=false;\n"
"  for(int i=0;i<70;i++){\n"
"    hit=blockAt(vX,vW); if(hit!=0) break;\n"
"    float m0=min(tmX.x,min(tmX.y,tmX.z)),m1=min(tmW.x,min(tmW.y,tmW.z));\n"
"    if(m0<=m1){\n"
"      if(tmX.x<=tmX.y&&tmX.x<=tmX.z){vX.x+=sX.x;tmX.x+=tdX.x;neg=sX.x<0;ax=0;}\n"
"      else if(tmX.y<=tmX.z){vX.y+=sX.y;tmX.y+=tdX.y;neg=sX.y<0;ax=1;}\n"
"      else{vX.z+=sX.z;tmX.z+=tdX.z;neg=sX.z<0;ax=2;}\n"
"    } else {\n"
"      if(tmW.x<=tmW.y&&tmW.x<=tmW.z){vW.x+=sW.x;tmW.x+=tdW.x;neg=sW.x<0;ax=3;}\n"
"      else if(tmW.y<=tmW.z){vW.y+=sW.y;tmW.y+=tdW.y;neg=sW.y<0;ax=4;}\n"
"      else{vW.z+=sW.z;tmW.z+=tdW.z;neg=sW.z<0;ax=5;}\n"
"    }\n"
"    if(any(lessThan(vX,ivec3(-1)))||any(greaterThan(vX,ivec3(10)))) break;\n"
"    if(any(lessThan(vW,ivec3(0)))||any(greaterThanEqual(vW,ivec3(5,5,1)))) break;\n"
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
    genWorld();

    Image img={.data=texR,.width=TEXW,.height=TEXW,.mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    editTex=LoadTextureFromImage(img);
    syncEditTex();

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
    int locETex  =GetShaderLocation(shader,"editTex");

    float cx=5,cy=8,cz=5, cw=2.5f,cv=2.5f,cu=0.5f;
    float yaw=0,pitch=-0.4f,look_w=0,look_v=0;
    int selBlock=1;
    float mspd=5.f,lspd=0.0018f,espd=1.5f,lwspd=1.2f;
    bool locked=false;

    while(!WindowShouldClose()&&!IsKeyPressed(KEY_ESCAPE)){
        float dt=GetFrameTime();

        if(!locked&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){DisableCursor();locked=true;}
        if(locked&&IsKeyPressed(KEY_TAB)){EnableCursor();locked=false;}

        Vector2 md=locked?GetMouseDelta():(Vector2){0,0};
        bool mmb=locked&&IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);

        if(!mmb){yaw-=md.x*lspd;pitch-=md.y*lspd;pitch=fmaxf(-1.4f,fminf(1.4f,pitch));}
        else{look_w+=md.x*lspd;look_v-=md.y*lspd;}
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
        cw=fmaxf(0,fminf(WS-0.01f,cw)); cv=fmaxf(0,fminf(VS-0.01f,cv));
        cu=fmaxf(0,fminf(US-0.01f,cu));
        for(int k=KEY_ONE;k<=KEY_SEVEN;k++) if(IsKeyPressed(k)) selBlock=k-KEY_ONE+1;

        // 6D DDA interaction (center ray = VM column 2 = world-space forward)
        float pos6[6]={cx,cy,cz,cw,cv,cu};
        float dir6[6]; for(int j=0;j<6;j++) dir6[j]=VM[j][2];
        int hvox[6],lax,lst; bool hit=dda6(pos6,dir6,hvox,&lax,&lst);
        if(hit&&locked&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){setBlock(hvox[0],hvox[1],hvox[2],hvox[3],hvox[4],hvox[5],0);syncEditTex();}
        if(hit&&locked&&IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)){
            int pv[6];for(int i=0;i<6;i++)pv[i]=hvox[i];pv[lax]-=lst;
            if(!getBlock(pv[0],pv[1],pv[2],pv[3],pv[4],pv[5])){setBlock(pv[0],pv[1],pv[2],pv[3],pv[4],pv[5],selBlock);syncEditTex();}
        }

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
        SetShaderValueTexture(shader,locETex,editTex);

        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(shader);
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),WHITE);
        EndShaderMode();

        int W=GetScreenWidth(),H=GetScreenHeight();
        DrawLine(W/2-10,H/2,W/2+10,H/2,WHITE);
        DrawLine(W/2,H/2-10,W/2,H/2+10,WHITE);
        if(!locked)
            DrawText("CLICK TO LOCK MOUSE AND PLAY",W/2-180,H/2-40,20,YELLOW);
        DrawText("voxel6d - 6D raycast",10,10,20,WHITE);
        DrawText("WASD/Space/Shift=move  Q/E=W  R/F=V  LMB=break  RMB=place  Tab=unlock",10,34,13,LIGHTGRAY);
        DrawText("Z/X=lookW(WARP)  C/V=lookV  MMB+drag",10,50,13,YELLOW);
        char buf[160];
        snprintf(buf,sizeof(buf),"pos:%.1f %.1f %.1f  W=%.2f V=%.2f  lookW=%.2f lookV=%.2f  block:%d",cx,cy,cz,cw,cv,look_w,look_v,selBlock);
        DrawText(buf,10,66,13,YELLOW);
        DrawFPS(10,82);
        EndDrawing();
    }
    UnloadShader(shader); UnloadTexture(editTex); CloseWindow();
    return 0;
}

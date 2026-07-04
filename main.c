#include "raylib.h"
#include <math.h>
#include <string.h>

#define WORLD_SIZE 8
#define FOCAL 5.0f

// 6D point
typedef struct { float x, y, z, w, v, u; } Vec6;

// 6D camera state
typedef struct {
    Vec6 pos;
    float yaw, pitch;   // rotation in xy plane (yaw) and xz plane (pitch)
    float rot_w, rot_v, rot_u; // extra axis rotations
} Cam6;

// Simple 6D voxel world: just a flat plane at u=0,v=0,w=0 with some blocks
static int world[WORLD_SIZE][WORLD_SIZE][WORLD_SIZE]; // x,y,z grid

// Project 6D point to 3D via three perspective divides on u, v, w axes
static Vector3 project6D(Vec6 p) {
    float su = FOCAL / (FOCAL + p.u + 0.001f);
    float sv = FOCAL / (FOCAL + p.v * su + 0.001f);
    float sw = FOCAL / (FOCAL + p.w * su * sv + 0.001f);
    return (Vector3){ p.x * su * sv * sw, p.y * su * sv * sw, p.z * su * sv * sw };
}

// Rotate a 6D point around the xy plane (yaw) and xz plane (pitch)
static Vec6 rotateBasic(Vec6 p, float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    // yaw: rotate x,z
    float nx = p.x * cy - p.z * sy;
    float nz = p.x * sy + p.z * cy;
    p.x = nx; p.z = nz;
    // pitch: rotate y,z
    float ny = p.y * cp - p.z * sp;
    nz = p.y * sp + p.z * cp;
    p.y = ny; p.z = nz;
    return p;
}

// Translate 6D point relative to camera
static Vec6 relativeTo(Vec6 p, Cam6 cam) {
    p.x -= cam.pos.x; p.y -= cam.pos.y; p.z -= cam.pos.z;
    p.w -= cam.pos.w; p.v -= cam.pos.v; p.u -= cam.pos.u;
    return rotateBasic(p, -cam.yaw, -cam.pitch);
}

// Draw a 6D unit cube at position (bx,by,bz,bw,bv,bu) with given color
static void drawBlock6D(int bx, int by, int bz, int bw, int bv, int bu, Color col, Cam6 cam) {
    // 6D cube has 64 corners
    Vector3 corners[64];
    for (int i = 0; i < 64; i++) {
        Vec6 p = {
            bx + ((i>>0)&1),
            by + ((i>>1)&1),
            bz + ((i>>2)&1),
            bw + ((i>>3)&1) * 0.4f,
            bv + ((i>>4)&1) * 0.4f,
            bu + ((i>>5)&1) * 0.4f
        };
        Vec6 rel = relativeTo(p, cam);
        if (rel.z < 0.1f) { corners[i] = (Vector3){9999,9999,9999}; continue; }
        corners[i] = project6D(rel);
    }

    // Draw the 12 face-edges of the 3D base cube (ignoring higher dims for now)
    int edges[12][2] = {
        {0,1},{2,3},{4,5},{6,7},
        {0,2},{1,3},{4,6},{5,7},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (int e = 0; e < 12; e++) {
        Vector3 a = corners[edges[e][0]], b = corners[edges[e][1]];
        if (a.x == 9999 || b.x == 9999) continue;
        DrawLine3D(a, b, col);
    }
}

int main(void) {
    InitWindow(1280, 720, "voxel6d - 6D first person voxel");
    SetTargetFPS(60);
    DisableCursor();

    // Build a simple flat floor
    for (int x = 0; x < WORLD_SIZE; x++)
        for (int z = 0; z < WORLD_SIZE; z++)
            world[x][0][z] = 1;
    // A few extra blocks
    world[3][1][3] = 2;
    world[4][1][3] = 3;
    world[3][1][4] = 4;
    world[2][2][2] = 5;

    Color blockColors[] = {
        BLANK, GRAY, RED, GREEN, BLUE, YELLOW, PURPLE, ORANGE
    };

    Cam6 cam = { .pos = {2.0f, 1.6f, 2.0f, 0, 0, 0}, .yaw = 0, .pitch = 0 };

    // 3D raylib camera just for rendering context
    Camera3D rcam = {
        .position = {0,0,0},
        .target = {0,0,-1},
        .up = {0,1,0},
        .fovy = 70,
        .projection = CAMERA_PERSPECTIVE
    };

    float moveSpeed = 3.0f;
    float lookSpeed = 0.002f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 md = GetMouseDelta();
        cam.yaw   += md.x * lookSpeed;
        cam.pitch -= md.y * lookSpeed;
        if (cam.pitch >  1.4f) cam.pitch =  1.4f;
        if (cam.pitch < -1.4f) cam.pitch = -1.4f;

        // Movement: WASD=xz, Space/LShift=y, QE=w, RF=v, TG=u
        float cy = cosf(cam.yaw), sy = sinf(cam.yaw);
        if (IsKeyDown(KEY_W)) { cam.pos.x += cy*moveSpeed*dt; cam.pos.z -= sy*moveSpeed*dt; }
        if (IsKeyDown(KEY_S)) { cam.pos.x -= cy*moveSpeed*dt; cam.pos.z += sy*moveSpeed*dt; }
        if (IsKeyDown(KEY_A)) { cam.pos.x -= sy*moveSpeed*dt; cam.pos.z -= cy*moveSpeed*dt; }
        if (IsKeyDown(KEY_D)) { cam.pos.x += sy*moveSpeed*dt; cam.pos.z += cy*moveSpeed*dt; }
        if (IsKeyDown(KEY_SPACE))       cam.pos.y += moveSpeed*dt;
        if (IsKeyDown(KEY_LEFT_SHIFT))  cam.pos.y -= moveSpeed*dt;
        if (IsKeyDown(KEY_Q)) cam.pos.w += moveSpeed*dt;
        if (IsKeyDown(KEY_E)) cam.pos.w -= moveSpeed*dt;
        if (IsKeyDown(KEY_R)) cam.pos.v += moveSpeed*dt;
        if (IsKeyDown(KEY_F)) cam.pos.v -= moveSpeed*dt;
        if (IsKeyDown(KEY_T)) cam.pos.u += moveSpeed*dt;
        if (IsKeyDown(KEY_G)) cam.pos.u -= moveSpeed*dt;

        BeginDrawing();
        ClearBackground((Color){20,20,30,255});

        BeginMode3D(rcam);
        // Draw all blocks
        for (int x = 0; x < WORLD_SIZE; x++)
            for (int y = 0; y < WORLD_SIZE; y++)
                for (int z = 0; z < WORLD_SIZE; z++) {
                    if (!world[x][y][z]) continue;
                    int ci = world[x][y][z] % 8;
                    // Draw in w/v/u slices near camera
                    for (int bw = -1; bw <= 1; bw++)
                    for (int bv = -1; bv <= 1; bv++)
                    for (int bu = -1; bu <= 1; bu++) {
                        Color c = blockColors[ci];
                        // Fade blocks far in extra dims
                        float dist = sqrtf(bw*bw + bv*bv + bu*bu);
                        c.a = (unsigned char)(255 * (1.0f - dist*0.35f));
                        if (c.a < 20) continue;
                        drawBlock6D(x, y, z, bw, bv, bu, c, cam);
                    }
                }
        EndMode3D();

        DrawText("voxel6d", 10, 10, 20, WHITE);
        DrawText("WASD=move  Space/Shift=up/down  QE=w  RF=v  TG=u", 10, 35, 14, LIGHTGRAY);
        DrawFPS(10, 55);
        char buf[128];
        snprintf(buf, sizeof(buf), "pos: %.1f %.1f %.1f | w=%.1f v=%.1f u=%.1f",
            cam.pos.x, cam.pos.y, cam.pos.z, cam.pos.w, cam.pos.v, cam.pos.u);
        DrawText(buf, 10, 75, 14, LIGHTGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

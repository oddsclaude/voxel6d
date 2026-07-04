#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// World: 6D grid [x][y][z][w][v][u]
// Each extra dimension has EXTRA_SIZE slices
#define XYZ_SIZE  16
#define EXTRA_SIZE 4   // slices per extra axis (w, v, u)
#define BLOCK_TYPES 8

static unsigned char world[XYZ_SIZE][XYZ_SIZE][XYZ_SIZE][EXTRA_SIZE][EXTRA_SIZE][EXTRA_SIZE];

// Camera
typedef struct {
    Vector3 pos;
    float yaw, pitch;
    // Extra dimension positions (integer slice index)
    int sw, sv, su;
} Cam;

static Color blockColor[BLOCK_TYPES] = {
    BLANK,                           // 0: air
    (Color){120,80,40,255},          // 1: dirt
    (Color){60,120,40,255},          // 2: grass
    (Color){150,150,150,255},        // 3: stone
    (Color){40,80,200,255},          // 4: water
    (Color){200,180,60,255},         // 5: sand
    (Color){80,60,40,255},           // 6: wood
    (Color){60,180,60,255},          // 7: leaves
};

static int getBlock(int x, int y, int z, int w, int v, int u) {
    if (x<0||y<0||z<0||w<0||v<0||u<0) return 0;
    if (x>=XYZ_SIZE||y>=XYZ_SIZE||z>=XYZ_SIZE) return 0;
    if (w>=EXTRA_SIZE||v>=EXTRA_SIZE||u>=EXTRA_SIZE) return 0;
    return world[x][y][z][w][v][u];
}

static void setBlock(int x, int y, int z, int w, int v, int u, int type) {
    if (x<0||y<0||z<0||w<0||v<0||u<0) return;
    if (x>=XYZ_SIZE||y>=XYZ_SIZE||z>=XYZ_SIZE) return;
    if (w>=EXTRA_SIZE||v>=EXTRA_SIZE||u>=EXTRA_SIZE) return;
    world[x][y][z][w][v][u] = (unsigned char)type;
}

// DDA raycast in current 3D slice, returns hit block pos and face normal
static bool raycast(Vector3 origin, Vector3 dir, int sw, int sv, int su,
                    int *hx, int *hy, int *hz, Vector3 *normal) {
    int x = (int)floorf(origin.x);
    int y = (int)floorf(origin.y);
    int z = (int)floorf(origin.z);

    float dx = fabsf(dir.x) < 0.0001f ? 1e30f : 1.0f / fabsf(dir.x);
    float dy = fabsf(dir.y) < 0.0001f ? 1e30f : 1.0f / fabsf(dir.y);
    float dz = fabsf(dir.z) < 0.0001f ? 1e30f : 1.0f / fabsf(dir.z);

    int sx = dir.x < 0 ? -1 : 1;
    int sy = dir.y < 0 ? -1 : 1;
    int sz = dir.z < 0 ? -1 : 1;

    float tx = (dir.x < 0 ? (origin.x - x) : (x + 1 - origin.x)) * dx;
    float ty = (dir.y < 0 ? (origin.y - y) : (y + 1 - origin.y)) * dy;
    float tz = (dir.z < 0 ? (origin.z - z) : (z + 1 - origin.z)) * dz;

    Vector3 norm = {0,0,0};
    for (int i = 0; i < 64; i++) {
        if (getBlock(x, y, z, sw, sv, su)) {
            *hx = x; *hy = y; *hz = z;
            *normal = norm;
            return true;
        }
        if (tx < ty && tx < tz) {
            x += sx; norm = (Vector3){-(float)sx, 0, 0}; tx += dx;
        } else if (ty < tz) {
            y += sy; norm = (Vector3){0, -(float)sy, 0}; ty += dy;
        } else {
            z += sz; norm = (Vector3){0, 0, -(float)sz}; tz += dz;
        }
    }
    return false;
}

static void genWorld(void) {
    // For each extra-dim slice, generate slightly different terrain
    for (int w = 0; w < EXTRA_SIZE; w++)
    for (int v = 0; v < EXTRA_SIZE; v++)
    for (int u = 0; u < EXTRA_SIZE; u++) {
        int groundHeight = 4 + (w + v + u) % 3;
        for (int x = 0; x < XYZ_SIZE; x++)
        for (int z = 0; z < XYZ_SIZE; z++) {
            for (int y = 0; y < groundHeight - 1; y++)
                setBlock(x, y, z, w, v, u, 1); // dirt
            setBlock(x, groundHeight - 1, z, w, v, u, 2); // grass
        }
        // A column of stone in each slice
        int cx = 6 + (w*2) % 4, cz = 6 + (v*2) % 4;
        for (int y = groundHeight; y < groundHeight + 3 + u; y++)
            setBlock(cx, y, cz, w, v, u, 3);
    }
}

int main(void) {
    InitWindow(1280, 720, "voxel6d - 6D slice-based voxel");
    SetTargetFPS(60);
    DisableCursor();

    genWorld();

    Cam cam = { .pos = {8.0f, 6.5f, 8.0f}, .yaw = 0, .pitch = 0,
                .sw = 0, .sv = 0, .su = 0 };
    int selectedBlock = 1;

    // Slice transition timers (so you see a brief "crossfade" hint)
    float sliceAnim = 0;

    Camera3D rcam = {
        .position = {0,0,-0.01f},
        .target = {0,0,0},
        .up = {0,1,0},
        .fovy = 70,
        .projection = CAMERA_PERSPECTIVE
    };

    float moveSpeed = 5.0f;
    float lookSpeed = 0.002f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        sliceAnim = fmaxf(0, sliceAnim - dt * 4);

        // Mouse look
        Vector2 md = GetMouseDelta();
        cam.yaw   += md.x * lookSpeed;
        cam.pitch -= md.y * lookSpeed;
        if (cam.pitch >  1.4f) cam.pitch =  1.4f;
        if (cam.pitch < -1.4f) cam.pitch = -1.4f;

        // WASD movement
        float cy = cosf(cam.yaw), sy = sinf(cam.yaw);
        if (IsKeyDown(KEY_W)) { cam.pos.x += cy*moveSpeed*dt; cam.pos.z -= sy*moveSpeed*dt; }
        if (IsKeyDown(KEY_S)) { cam.pos.x -= cy*moveSpeed*dt; cam.pos.z += sy*moveSpeed*dt; }
        if (IsKeyDown(KEY_A)) { cam.pos.x -= sy*moveSpeed*dt; cam.pos.z -= cy*moveSpeed*dt; }
        if (IsKeyDown(KEY_D)) { cam.pos.x += sy*moveSpeed*dt; cam.pos.z += cy*moveSpeed*dt; }
        if (IsKeyDown(KEY_SPACE))      cam.pos.y += moveSpeed*dt;
        if (IsKeyDown(KEY_LEFT_SHIFT)) cam.pos.y -= moveSpeed*dt;

        // Extra dimension navigation (step by slice)
        if (IsKeyPressed(KEY_Q)) { cam.sw = (cam.sw + 1) % EXTRA_SIZE; sliceAnim = 1; }
        if (IsKeyPressed(KEY_E)) { cam.sw = (cam.sw - 1 + EXTRA_SIZE) % EXTRA_SIZE; sliceAnim = 1; }
        if (IsKeyPressed(KEY_R)) { cam.sv = (cam.sv + 1) % EXTRA_SIZE; sliceAnim = 1; }
        if (IsKeyPressed(KEY_F)) { cam.sv = (cam.sv - 1 + EXTRA_SIZE) % EXTRA_SIZE; sliceAnim = 1; }
        if (IsKeyPressed(KEY_T)) { cam.su = (cam.su + 1) % EXTRA_SIZE; sliceAnim = 1; }
        if (IsKeyPressed(KEY_G)) { cam.su = (cam.su - 1 + EXTRA_SIZE) % EXTRA_SIZE; sliceAnim = 1; }

        // Block selection (1-7)
        for (int k = KEY_ONE; k <= KEY_SEVEN; k++)
            if (IsKeyPressed(k)) selectedBlock = k - KEY_ONE + 1;

        // Build camera look direction
        Vector3 forward = {
            cosf(cam.pitch) * cosf(cam.yaw),
            sinf(cam.pitch),
            cosf(cam.pitch) * -sinf(cam.yaw)
        };

        // Raycast for target block
        int hx, hy, hz;
        Vector3 hnorm;
        bool hit = raycast(cam.pos, forward, cam.sw, cam.sv, cam.su,
                           &hx, &hy, &hz, &hnorm);

        // Break block (left click)
        if (hit && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            setBlock(hx, hy, hz, cam.sw, cam.sv, cam.su, 0);

        // Place block (right click)
        if (hit && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            int px = hx + (int)hnorm.x;
            int py = hy + (int)hnorm.y;
            int pz = hz + (int)hnorm.z;
            if (!getBlock(px, py, pz, cam.sw, cam.sv, cam.su))
                setBlock(px, py, pz, cam.sw, cam.sv, cam.su, selectedBlock);
        }

        // Sync raylib camera
        rcam.position = cam.pos;
        rcam.target = Vector3Add(cam.pos, forward);

        BeginDrawing();
        unsigned char flash = (unsigned char)(sliceAnim * 30);
        ClearBackground((Color){10 + flash, 10, 20 + flash, 255});

        BeginMode3D(rcam);

        // Draw current slice
        for (int x = 0; x < XYZ_SIZE; x++)
        for (int y = 0; y < XYZ_SIZE; y++)
        for (int z = 0; z < XYZ_SIZE; z++) {
            int b = getBlock(x, y, z, cam.sw, cam.sv, cam.su);
            if (!b) continue;
            Vector3 bp = {x + 0.5f, y + 0.5f, z + 0.5f};
            DrawCube(bp, 1, 1, 1, blockColor[b]);
            DrawCubeWires(bp, 1, 1, 1, (Color){0,0,0,40});
        }

        // Highlight targeted block
        if (hit)
            DrawCubeWires((Vector3){hx+0.5f, hy+0.5f, hz+0.5f}, 1.01f, 1.01f, 1.01f, WHITE);

        EndMode3D();

        // Crosshair
        int cx2 = GetScreenWidth()/2, cy2 = GetScreenHeight()/2;
        DrawLine(cx2-10, cy2, cx2+10, cy2, WHITE);
        DrawLine(cx2, cy2-10, cx2, cy2+10, WHITE);

        // HUD
        DrawText("voxel6d", 10, 10, 20, WHITE);
        DrawText("WASD=move  Space/Shift=y  Q/E=W-axis  R/F=V-axis  T/G=U-axis", 10, 34, 13, LIGHTGRAY);
        DrawText("LClick=break  RClick=place  1-7=block type", 10, 50, 13, LIGHTGRAY);

        char buf[128];
        snprintf(buf, sizeof(buf), "slice: W=%d V=%d U=%d  |  pos: %.1f %.1f %.1f",
            cam.sw, cam.sv, cam.su, cam.pos.x, cam.pos.y, cam.pos.z);
        DrawText(buf, 10, 66, 13, YELLOW);
        DrawFPS(10, 82);

        // Selected block indicator
        snprintf(buf, sizeof(buf), "block: %d", selectedBlock);
        DrawText(buf, GetScreenWidth()-100, 10, 16, blockColor[selectedBlock]);

        // Slice flash indicator
        if (sliceAnim > 0.1f) {
            const char *msg = "--- dimension shift ---";
            int tw = MeasureText(msg, 24);
            DrawText(msg, (GetScreenWidth()-tw)/2, GetScreenHeight()/2 + 40,
                     24, (Color){255,255,100,(unsigned char)(sliceAnim*200)});
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

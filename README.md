# voxel6d

A first-person 6D voxel game prototype using raylib. Navigate a six-dimensional block world.

## Controls

| Key | Action |
|-----|--------|
| WASD | Move in XZ plane |
| Space / LShift | Move up / down (Y) |
| Q / E | Move along W axis |
| R / F | Move along V axis |
| T / G | Move along U axis |
| Mouse | Look |
| Esc | Quit |

## How it works

6D points are projected to 3D using three sequential perspective divides (one per extra dimension), then passed to raylib for final 3D→2D rendering. The 4th, 5th, and 6th dimensions are rendered as faded copies of the world, fading out with distance from the camera's position in those axes.

## Build

```bash
cmake -B build
cmake --build build
./build/voxel6d
```

Requires raylib (auto-fetched via CMake if not found).

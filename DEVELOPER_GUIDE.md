# Developer Guide — HW-SWGL 2026
> Computer Graphics HW-SWGL 2026 · C++ / OpenGL + GLM  
> Software Graphics Pipeline: CPU-side vertex transform + triangle rasterisation

---

## Table of Contents
1. [Prerequisites](#1-prerequisites)
2. [Build & Run](#2-build--run)
3. [Project Structure](#3-project-structure)
4. [Architecture Overview](#4-architecture-overview)
5. [Global State](#5-global-state)
6. [Core Math Functions (Phase 1)](#6-core-math-functions-phase-1)
7. [Matrix Stack / Scene Graph (Phase 2)](#7-matrix-stack--scene-graph-phase-2)
8. [Asset Pipeline (Phase 3)](#8-asset-pipeline-phase-3)
9. [Scene Save / Load (Phase 4)](#9-scene-save--load-phase-4)
10. [Software Rasteriser (Phase 5)](#10-software-rasteriser-phase-5)
11. [Rendering Loop](#11-rendering-loop)
12. [Keyboard Reference](#12-keyboard-reference)
13. [Adding a New Model](#13-adding-a-new-model)
14. [Common Pitfalls](#14-common-pitfalls)
15. [Performance Notes](#15-performance-notes)

---

## 1. Prerequisites

Install system packages (Ubuntu / Debian):

```bash
# OpenGL + GLFW + GLEW
sudo apt install libglfw3-dev libglew-dev freeglut3-dev

# OpenGL Mathematics (GLM)
sudo apt install libglm-dev

# CMake + build tools
sudo apt install cmake build-essential pkg-config
```

---

## 2. Build & Run

```bash
# In the project root
mkdir build && cd build
cmake ..
make

# Run (from build/)
./trans

# To load a custom OBJ at startup, press O inside the running window
# (the program looks for "model.obj" in the current working directory)
```

The executable is named `trans` (defined in `CMakeLists.txt`).

---

## 3. Project Structure

```
hw2-swgl-1/
├── CMakeLists.txt            # Build configuration
├── main.cpp                  # Entire implementation (single-file)
├── README.txt                # Quick dependency install notes
├── SOFTWARE_PIPELINE_NOTES.md  # In-depth learning reference (Chinese)
└── DEVELOPER_GUIDE.md        # ← You are here
```

`main.cpp` is intentionally a single file; all phases are implemented here.

---

## 4. Architecture Overview

### Vertex Pipeline

$$v_{clip} = P \cdot V \cdot M \cdot v_{obj}$$

```
Object Space  →[M]→  World Space  →[V]→  View Space  →[P]→  Clip Space
   →  [÷w]  →  NDC  →  [Viewport]  →  Screen Space  →  [Rasterise]  →  Pixels
```

Each step maps directly to code:

| Stage | Function | Output |
|---|---|---|
| Model transform | `swTranslate / swRotate* / swScale` | `transformMat` |
| View transform | `swLookAt` | `ViewMat` |
| Projection | `swPerspective` | `ProjectionMat` |
| Full chain | `swTriangle` | Clip→NDC coords |
| Rasterisation | `swRasterizeTriangle` | writes `frameBuffer` / `zBuffer` |
| Display | `glDrawPixels` in `Display()` | screen output |

### Feature Flags

```cpp
const bool STEP2 = true;  // enable swLookAt  (false → uses gluLookAt)
const bool STEP3 = true;  // enable swPerspective + software rasteriser
```

When `STEP3 = false`, `swTriangle` falls back to OpenGL immediate-mode `glVertex3f`.

---

## 5. Global State

| Variable | Type | Purpose |
|---|---|---|
| `transformMat` | `mat4x4` | Current model matrix (modified by keyboard) |
| `ViewMat` | `mat4x4` | Camera view matrix (set by `swLookAt`) |
| `ProjectionMat` | `mat4x4` | Perspective matrix (set by `swPerspective`) |
| `matrixStack` | `vector<mat4x4>` | LIFO stack for scene-graph hierarchy |
| `activeModel` | `ActiveModel` | Which model to draw (0=tet, 1=cube, 2=obj) |
| `theta` | `float` | Camera orbit angle (radians) |
| `childAngle` | `float` | Satellite tetrahedron orbit angle |
| `frameBuffer` | `vector<vec4>` | CPU RGBA frame buffer (Phase 5) |
| `zBuffer` | `vector<float>` | CPU depth buffer, init to `+∞` (Phase 5) |
| `objMeshVerts` | `vector<vec3>` | Loaded OBJ vertex positions |
| `objMeshFaces` | `vector<ObjFace>` | Loaded OBJ triangle indices (0-based) |

---

## 6. Core Math Functions (Phase 1)

> **GLM is Column-Major**: access via `mat[col][row]`. Translation lives in column 3.
> ```cpp
> mat[3][0] = Tx;  mat[3][1] = Ty;  mat[3][2] = Tz;
> ```

### `swTranslate(x, y, z)`
Returns a 4×4 translation matrix.

```cpp
mat4x4 T = swTranslate(2.0f, 0.0f, 0.0f);  // shift +2 along X
```

### `swRotateX/Y/Z(angle)`
Returns a 4×4 rotation matrix around the given axis. `angle` is in **radians**.

```cpp
mat4x4 R = swRotateZ(glm::radians(45.0f));
```

### `swRotate(angle, x, y, z)`
Arbitrary-axis rotation via Rodrigues' formula.

### `swScale(x, y, z)`
Returns a 4×4 non-uniform scale matrix.

### `swLookAt(...)`
Builds a view matrix equivalent to `gluLookAt`. Camera basis vectors (`s`, `u`, `f`) are constructed from `eye`, `center`, and `up`, then packed column-major.

### `swPerspective(fovyDeg, aspect, zNear, zFar)`
Builds the standard OpenGL frustum matrix.  
`P[2][3] = -1` encodes $w_c = -z_v$ (required for perspective division).

### Multiply Order

| Expression | Effect |
|---|---|
| `T * transformMat` | **Left-multiply** → Global (world-space) transform |
| `transformMat * T` | **Right-multiply** → Local (object-space) transform |

---

## 7. Matrix Stack / Scene Graph (Phase 2)

```cpp
swPushMatrix();           // save current transformMat onto the stack
  transformMat = transformMat * childTransform;
  Draw(transformMat);     // draw child in parent's space
swPopMatrix();            // restore parent's transformMat
```

**Nesting** is supported to arbitrary depth — just balance every `Push` with a `Pop`.

See `Draw_Tetrahedron()` for a live example: the satellite tetrahedron orbits the parent using `swPushMatrix / swPopMatrix` with a right-multiplied `swTranslate * swScale`.

---

## 8. Asset Pipeline (Phase 3)

### Procedural Cube — `Draw_Cube()`
8 vertices at ±0.5, 12 triangles, CCW winding. Each face has a distinct flat colour.

### OBJ Loader — `loadOBJ(path)`
- Parses `v` and `f` lines; strips `v/vt/vn` to vertex-index only.
- Converts 1-based OBJ indices to 0-based.
- Returns `false` if the file cannot be opened or is empty.
- Results stored in `objMeshVerts` / `objMeshFaces`; `objLoaded` flag set to `true`.

### Drawing an OBJ — `Draw_OBJ()`
Iterates `objMeshFaces`, bounds-checks each index, cycles through 6 flat colours per face.

### Loading a custom model
Place `model.obj` next to the executable (or the working directory), then press `O` at runtime.

---

## 9. Scene Save / Load (Phase 4)

```
scene.txt format:
  theta 0.785398
  model 1
  mat 1 0 0 0  0 1 0 0  0 0 1 0  2 0 0 1
```

| Action | Key | Function |
|---|---|---|
| Save | `K` | `SaveScene("scene.txt")` |
| Load | `L` | `LoadScene("scene.txt")` |

Saved state: `theta`, `activeModel`, `transformMat` (16 floats, column-major order).

---

## 10. Software Rasteriser (Phase 5)

### CPU Buffers

```cpp
// Allocated in init():
frameBuffer.assign(winWidth * winHeight, vec4(0.0f));
zBuffer.assign(winWidth * winHeight, numeric_limits<float>::infinity());

// Cleared each frame in Display() before drawing:
ClearBuffers();
```

`frameBuffer[y * winWidth + x]` stores RGBA; `alpha = 0` means "no fragment" (transparent over the OpenGL grid).

### `swRasterizeTriangle(color, ndc1, ndc2, ndc3)`

**Step-by-step:**

1. **Viewport transform** — NDC `[-1,1]` → pixel coordinates `[0, W-1] × [0, H-1]`
   ```cpp
   float sx = (ndc.x + 1.0f) * 0.5f * (winWidth - 1);
   float sy = (ndc.y + 1.0f) * 0.5f * (winHeight - 1);
   ```

2. **Signed area** — encodes winding order; degenerate triangles (`|area| < 1e-6`) are skipped.

3. **AABB** — bounding box clamped to screen, avoids testing all pixels.

4. **Per-pixel inside test** — edge function (2D cross product):
   $$E(A,B,P) = (B_x - A_x)(P_y - A_y) - (B_y - A_y)(P_x - A_x)$$
   All three weights must share the sign of `area`.

5. **Barycentric interpolation** — normalise weights and interpolate `z_ndc`.

6. **Z-test** — write pixel only if `z < zBuffer[idx]` (closer to camera wins).

### `swTriangle(color, v1, v2, v3, ModelMatrix)`

This is the top-level entry point called by all Draw functions:

```
Object Space → [M] → World Space → [V] → View Space → [P] → Clip Space → [÷w] → NDC
→ swRasterizeTriangle
```

Do **not** wrap `swTriangle` calls inside `glBegin/glEnd` — it is self-contained.

---

## 11. Rendering Loop

```
glfwPollEvents()
  └─ KeyCallback / SpecialKey   (update globals: transformMat, theta, activeModel, …)

Display(window):
  ├─ ClearBuffers()             (zero CPU frame/z-buffer)
  ├─ glClear(…)                 (clear OpenGL depth + colour)
  ├─ gluPerspective + gluLookAt (OpenGL camera for the grid)
  ├─ DrawGrid()                 (reference grid via OpenGL immediate mode)
  ├─ swPerspective + swLookAt   (set ProjectionMat / ViewMat for CPU pipeline)
  ├─ Draw_Tetrahedron/Cube/OBJ  (calls swTriangle → swRasterizeTriangle)
  └─ glDrawPixels(frameBuffer)  (blit CPU buffer to screen with alpha-blend)
```

`childAngle` is incremented every frame to animate the satellite tetrahedron orbit.

---

## 12. Keyboard Reference

| Key | Action |
|---|---|
| `F1` | Reset / add tetrahedron |
| `F2` / `C` | Switch to Cube model |
| `T` | Switch to Tetrahedron |
| `O` | Load `model.obj` from cwd |
| `Q` / `A` | World-space +X / −X translate |
| `W` / `S` | World-space +Y / −Y translate |
| `E` / `R` | Object-space +Z / −Z rotate |
| `Z` / `X` | Object-space scale up / down |
| `9` / `0` | Orbit camera +θ / −θ |
| `-` | Reset `transformMat` to identity |
| `[` | `swPushMatrix` (save state) |
| `]` | `swPopMatrix` (restore state) |
| `K` | Save scene to `scene.txt` |
| `L` | Load scene from `scene.txt` |
| `ESC` | Quit |

---

## 13. Adding a New Model

1. **Add an enum value** in `ActiveModel`:
   ```cpp
   enum ActiveModel { MODEL_TETRAHEDRON=0, MODEL_CUBE=1, MODEL_OBJ=2, MODEL_MYSHAPE=3 };
   ```

2. **Write a draw function** using `swTriangle`:
   ```cpp
   void Draw_MyShape() {
       swTriangle(vec3(1,0.5,0), v0, v1, v2, transformMat);
       // …more triangles…
   }
   ```

3. **Add a case** in `Display()`:
   ```cpp
   case MODEL_MYSHAPE: Draw_MyShape(); break;
   ```

4. **Bind a key** in `KeyCallback`:
   ```cpp
   case GLFW_KEY_M:
       activeModel = MODEL_MYSHAPE;
       break;
   ```

---

## 14. Common Pitfalls

### GLM column-major access
```cpp
// WRONG  (row-major assumption)
Translate[0][3] = Tx;

// CORRECT  (GLM: mat[col][row])
Translate[3][0] = Tx;
Translate[3][1] = Ty;
Translate[3][2] = Tz;
```

### Forgetting perspective division
```cpp
// WRONG — uses clip-space coords directly
glVertex3f(v.x, v.y, v.z);

// CORRECT — divide by w first
v = vec4(v.x/v.w, v.y/v.w, v.z/v.w, 1.0f);
```

### Rotation sin sign
The mathematical matrix element $R_{ij}$ maps to `mat[j][i]` in GLM. Placing a sin in the wrong cell flips the rotation direction.

### OBJ 1-based indices
```cpp
face.idx[i] = std::stoi(token) - 1;  // always subtract 1
```

### Wrapping `swTriangle` in `glBegin/glEnd`
`swTriangle` writes directly to the CPU `frameBuffer`; it never calls `glVertex3f`. Do not surround it with `glBegin/glEnd`.

### Degenerate near-clip triangles
If a vertex has `w ≈ 0` (on the near clip plane), perspective division produces extreme NDC values. The current code does not clip — the bounding-box viewport clamp provides partial safety but very large triangles may misbehave.

---

## 15. Performance Notes

| Scene | Triangle count | Frame cost |
|---|---|---|
| Tetrahedron | 4 | negligible |
| Cube | 12 | negligible |
| Complex OBJ | thousands | **noticeably slow** |

The software rasteriser runs on a single CPU thread: complexity is **O(W × H × N_triangles)** in the worst case (1 280 × 720 = ~921 600 z-tests per triangle bounding box). This is intentional — it mirrors exactly what GPU shader cores do in massive parallel.

Optimisation opportunities (not required for the homework):
- Tile-based rendering
- SIMD / multi-threading
- Backface culling (skip triangles with `area < 0`)
- Frustum / clip-space culling before rasterisation

---

*Guide corresponds to `main.cpp` HW-SWGL 2026, Phase 1–5 complete implementation.*

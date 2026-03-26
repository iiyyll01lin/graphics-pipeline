# Software Graphics Pipeline — 完整學習筆記
> 課程：Computer Graphics HW-SWGL 2026  
> 語言：C++ / OpenGL + GLM  
> 目標：**用純 CPU 程式碼重現 GPU 的頂點管線與三角形光柵化流程**

---

## 目錄
1. [整體架構](#整體架構)
2. [GLM 基礎：Column-Major 記憶體佈局](#glm-基礎column-major-記憶體佈局)
3. [Phase 1 — 仿射變換矩陣](#phase-1--仿射變換矩陣)
4. [Phase 2 — Matrix Stack（場景樹）](#phase-2--matrix-stack場景樹)
5. [Phase 3 — Asset Pipeline（OBJ / Cube）](#phase-3--asset-pipelineobj--cube)
6. [Phase 4 — Scene I/O](#phase-4--scene-io)
7. [Phase 5 — 軟體光柵化（Z-Buffer）](#phase-5--軟體光柵化z-buffer)
8. [鍵盤快捷鍵總表](#鍵盤快捷鍵總表)
9. [常見錯誤與除錯技巧](#常見錯誤與除錯技巧)

---

## 整體架構

```
main.cpp
│
├── Globals
│   ├── transformMat     ← 目前物體的 Model Matrix (可用鍵盤操作)
│   ├── ViewMat          ← Camera View Matrix (swLookAt)
│   ├── ProjectionMat    ← Perspective Matrix (swPerspective)
│   ├── matrixStack      ← Phase 2：用於場景樹的矩陣堆疊
│   ├── frameBuffer      ← Phase 5：CPU 端 RGBA 影格緩衝區
│   └── zBuffer          ← Phase 5：CPU 端深度緩衝區
│
├── 矩陣函式 (Phase 1)
│   ├── swTranslate / swRotateX/Y/Z / swRotate / swScale
│   ├── swLookAt          ← 取代 gluLookAt
│   └── swPerspective     ← 取代 gluPerspective
│
├── 場景樹 (Phase 2)
│   ├── swPushMatrix / swPopMatrix
│   └── Draw_Tetrahedron  ← 示範 parent-child 軌道動畫
│
├── Asset Pipeline (Phase 3)
│   ├── loadOBJ           ← 讀取 .obj 檔
│   ├── Draw_Cube         ← 程式生成的正方體
│   └── Draw_OBJ          ← 繪製讀入的 OBJ 模型
│
├── Scene I/O (Phase 4)
│   ├── SaveScene / LoadScene
│   └── 對應鍵：K = 儲存, L = 載入
│
└── 光柵化引擎 (Phase 5)
    ├── ClearBuffers      ← 每幀清空 CPU buffers
    ├── edgeFn            ← 邊函數 helper
    ├── swRasterizeTriangle ← Bounding Box + Barycentric + Z-Test
    └── Display           ← 最後用 glDrawPixels 把 frameBuffer 送到螢幕
```

### 完整頂點管線流程

$$v_{clip} = P \cdot V \cdot M \cdot v_{obj}$$

```
Object Space  →[M]→  World Space  →[V]→  View Space  →[P]→  Clip Space
    →  [÷wc]  →  NDC  →  [Viewport]  →  Screen Space  →  [Rasterize]  →  Pixels
```

---

## GLM 基礎：Column-Major 記憶體佈局

> **這是最容易出錯的地方！**

GLM 和 OpenGL 使用 **Column-Major（列-主序）** 矩陣，存取方式是 `mat[col][row]`：

```
數學上長這樣（Row-Major 直觀）:
| m00  m01  m02  Tx |
| m10  m11  m12  Ty |
| m20  m21  m22  Tz |
|  0    0    0    1 |

GLM 在記憶體裡長這樣（Column-Major）:
mat[0] = {m00, m10, m20, 0}   ← 第 0 欄
mat[1] = {m01, m11, m21, 0}   ← 第 1 欄
mat[2] = {m02, m12, m22, 0}   ← 第 2 欄
mat[3] = {Tx,  Ty,  Tz,  1}   ← 第 3 欄（平移在這裡）
```

**記憶口訣：** `mat[欄][列]`，平移儲存在第 3 欄。

### 存取範例

```cpp
mat4x4 M = mat4x4(1);   // identity matrix

M[3][0] = 5.0f;  // Tx = 5  (欄3, 列0)
M[3][1] = 3.0f;  // Ty = 3  (欄3, 列1)
M[3][2] = 1.0f;  // Tz = 1  (欄3, 列2)
```

---

## Phase 1 — 仿射變換矩陣

### 1-1 Translation（平移）

```cpp
mat4x4 swTranslate(float x, float y, float z)
{
    mat4x4 T = mat4x4(1);
    T[3][0] = x;  // 欄3 列0 = Tx
    T[3][1] = y;
    T[3][2] = z;
    return T;
}
```

### 1-2 Rotation（旋轉）

旋轉矩陣需把 cos/sin 放在**正確的格子**：

**繞 X 軸：**
$$R_x = \begin{pmatrix} 1 & 0 & 0 \\ 0 & \cos\theta & -\sin\theta \\ 0 & \sin\theta & \cos\theta \end{pmatrix}$$

```cpp
mat4x4 swRotateX(float angle)
{
    mat4x4 R = mat4x4(1);
    // GLM column-major: R[col][row]
    R[1][1] =  cos(angle);   R[2][1] = -sin(angle);
    R[1][2] =  sin(angle);   R[2][2] =  cos(angle);
    return R;
}
```

**繞 Y 軸：**
```cpp
R[0][0] =  cos(angle);   R[2][0] =  sin(angle);
R[0][2] = -sin(angle);   R[2][2] =  cos(angle);
```

**繞 Z 軸：**
```cpp
R[0][0] =  cos(angle);   R[1][0] = -sin(angle);
R[0][1] =  sin(angle);   R[1][1] =  cos(angle);
```

> **推導關鍵：** 數學矩陣 $R_{ij}$ 存在 GLM 的 `mat[j][i]`（欄行互換）。

### 1-3 Scale（縮放）

```cpp
mat4x4 swScale(float x, float y, float z)
{
    mat4x4 S = mat4x4(1);
    S[0][0] = x;
    S[1][1] = y;
    S[2][2] = z;
    return S;
}
```

### 1-4 View Matrix — swLookAt

#### 概念：建立正交相機座標系

```
forward (f) = normalize(center - eye)     ← 指向場景的方向
right   (s) = normalize(cross(f, up))     ← 右方
true_up (u) = cross(s, f)                 ← 真正的上方（避免漂移）
```

$$V = R \cdot T(-eye)$$

```cpp
mat4x4 swLookAt(float eyeX, float eyeY, float eyeZ,
                float centerX, float centerY, float centerZ,
                float upX, float upY, float upZ)
{
    vec3 eye(eyeX, eyeY, eyeZ);
    vec3 f = normalize(vec3(centerX,centerY,centerZ) - eye);
    vec3 s = normalize(cross(f, vec3(upX,upY,upZ)));
    vec3 u = cross(s, f);

    mat4x4 V = mat4x4(1);
    V[0][0]=s.x;  V[1][0]=s.y;  V[2][0]=s.z;  V[3][0]=-dot(s, eye);
    V[0][1]=u.x;  V[1][1]=u.y;  V[2][1]=u.z;  V[3][1]=-dot(u, eye);
    V[0][2]=-f.x; V[1][2]=-f.y; V[2][2]=-f.z; V[3][2]= dot(f, eye);
    return V;
}
```

> **注意：** `V[3][2] = dot(f, eye)` 不加負號，因為 z 軸是 `-f`，兩個負號相消。

### 1-5 Perspective Matrix — swPerspective

#### 標準 OpenGL Frustum Matrix：

$$P = \begin{pmatrix} f/a & 0 & 0 & 0 \\ 0 & f & 0 & 0 \\ 0 & 0 & \frac{z_f+z_n}{z_n-z_f} & \frac{2z_fz_n}{z_n-z_f} \\ 0 & 0 & -1 & 0 \end{pmatrix}$$

其中 $f = 1/\tan(fovy/2)$, $a$ = aspect ratio。

```cpp
mat4x4 swPerspective(float fovyDeg, float aspect, float zNear, float zFar)
{
    mat4x4 P = mat4x4(0);
    float f = 1.0f / tan(fovyDeg * M_PI / 360.0f);

    P[0][0] = f / aspect;
    P[1][1] = f;
    P[2][2] = (zFar + zNear) / (zNear - zFar);
    P[2][3] = -1.0f;          // ← 讓 w_clip = -z_view（透視除法關鍵）
    P[3][2] = (2.0f * zFar * zNear) / (zNear - zFar);
    return P;
}
```

> **`P[2][3] = -1`** 的意義：乘法後 $w_c = -z_v$，除以 $w_c$ 才能得到正確 NDC 深度。

### 1-6 透視除法（Perspective Division）

```cpp
// Clip space → NDC
v1 = vec4(v1.x/v1.w, v1.y/v1.w, v1.z/v1.w, 1.0f);
```

$$x_{ndc} = \frac{x_c}{w_c}, \quad y_{ndc} = \frac{y_c}{w_c}, \quad z_{ndc} = \frac{z_c}{w_c}$$

### 1-7 矩陣相乘順序（Left vs Right Multiply）

| 乘法位置 | 效果 | 程式碼 |
|----------|------|--------|
| **Left-multiply** `T * M` | Global（世界空間）變換 | `transformMat = swTranslate(1,0,0) * transformMat` |
| **Right-multiply** `M * T` | Local（物體空間）變換 | `transformMat = transformMat * swRotateZ(angle)` |

---

## Phase 2 — Matrix Stack（場景樹）

### 概念

類似 OpenGL 的 `glPushMatrix` / `glPopMatrix`，用來儲存/還原矩陣狀態，實現 parent → child 的層次變換。

### 實作

```cpp
std::vector<mat4x4> matrixStack;

void swPushMatrix() {
    matrixStack.push_back(transformMat);  // 把目前矩陣推入堆疊
}

void swPopMatrix() {
    if (!matrixStack.empty()) {
        transformMat = matrixStack.back();  // 從堆疊頂端取回
        matrixStack.pop_back();
    }
}
```

### 使用範例：衛星軌道

```cpp
// Parent（主體）
Draw(transformMat);

// Child（衛星，繞 parent 轉）
swPushMatrix();                              // 儲存 parent 狀態
transformMat = transformMat
             * swTranslate(3*cos(t), 3*sin(t), 0)  // RIGHT: 物體空間平移
             * swScale(0.4f, 0.4f, 0.4f);           // RIGHT: 物體空間縮放
Draw(transformMat);
swPopMatrix();                               // 還原 parent 狀態
```

### 多層巢狀

```cpp
swPushMatrix();         // Level 1 進入
  // 改 transformMat
  swPushMatrix();       // Level 2 進入
    // 改 transformMat
  swPopMatrix();        // Level 2 離開
swPopMatrix();          // Level 1 離開
```

---

## Phase 3 — Asset Pipeline（OBJ / Cube）

### 3-1 OBJ 檔格式

```obj
# 頂點 (x y z)
v 1.0 0.0 0.0
v 0.0 1.0 0.0
v 0.0 0.0 1.0

# 面（1-based 索引）
f 1 2 3
f 1/1/1 2/2/2 3/3/3    <- 支援 v/vt/vn 格式
```

### 3-2 OBJ Loader

```cpp
bool loadOBJ(const char* path)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            objMeshVerts.push_back(vec3(x,y,z));

        } else if (token == "f") {
            ObjFace face;
            for (int i = 0; i < 3; i++) {
                std::string ft;
                ss >> ft;
                // 去掉 "/vt/vn" 只保留頂點索引，並轉換成 0-based
                int vi = std::stoi(ft.substr(0, ft.find('/')));
                face.idx[i] = vi - 1;
            }
            objMeshFaces.push_back(face);
        }
    }
    return !objMeshFaces.empty();
}
```

### 3-3 Procedural Cube（程式生成正方體）

8 個頂點 ± 0.5，12 個三角形（每面 2 個），CCW winding（右手法則）：

```
頂點編號（以左下角後方為 0）：
    7──────6
   /|     /|     y
  4──────5 |     │  z
  │ 3────│─2     │ /
  │/     │/      O──── x
  0──────1
```

```cpp
vec3 v[8] = {
    vec3(-0.5,-0.5,-0.5), // 0
    vec3( 0.5,-0.5,-0.5), // 1
    ...
};
// Front face (z=+0.5): v4 v5 v6, v4 v6 v7
// Back face  (z=-0.5): v1 v0 v3, v1 v3 v2
// (確保從外面看是 CCW)
```

---

## Phase 4 — Scene I/O

### 儲存的狀態變數

| 變數 | 說明 |
|------|------|
| `theta` | 相機軌道角度 |
| `activeModel` | 目前顯示的模型（0=tet, 1=cube, 2=obj）|
| `transformMat` | 16 個浮點數（column-major 順序）|

### scene.txt 格式

```
theta 0.785398
model 1
mat 1 0 0 0 0 1 0 0 0 0 1 0 2 0 0 1
```

### 程式碼

```cpp
void SaveScene(const char* filename) {
    std::ofstream f(filename);
    f << "theta " << theta << "\n";
    f << "model " << (int)activeModel << "\n";
    f << "mat";
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            f << " " << transformMat[col][row];
    f << "\n";
}

void LoadScene(const char* filename) {
    std::ifstream f(filename);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string key; ss >> key;
        if      (key == "theta") ss >> theta;
        else if (key == "model") { int m; ss >> m; activeModel = (ActiveModel)m; }
        else if (key == "mat")
            for (int col=0; col<4; col++)
                for (int row=0; row<4; row++)
                    ss >> transformMat[col][row];
    }
}
```

---

## Phase 5 — 軟體光柵化（Z-Buffer）

### 整體流程

```
NDC 頂點 (x,y,z ∈ [-1,1])
    ↓ Viewport Transform
Screen Space 頂點 (pixel 座標)
    ↓ Bounding Box 計算
每個 pixel 做 Barycentric 檢定
    ↓ 在三角形內？
插值 z_ndc
    ↓ Z-Buffer Test (z < zBuffer[idx])
寫入 frameBuffer
    ↓ glDrawPixels
顯示到螢幕
```

### 5-1 CPU 緩衝區

```cpp
std::vector<vec4>  frameBuffer;  // RGBA, alpha=0 表示空像素
std::vector<float> zBuffer;      // 深度，初始化為 +∞

// 在 init() 中分配：
frameBuffer.assign(winWidth * winHeight, vec4(0));
zBuffer.assign(winWidth * winHeight, numeric_limits<float>::infinity());

// 每幀清空（Display() 一開始就呼叫）：
void ClearBuffers() {
    fill(frameBuffer.begin(), frameBuffer.end(), vec4(0));
    fill(zBuffer.begin(),     zBuffer.end(),     INFINITY);
}
```

### 5-2 Edge Function（邊函數）

核心數學：**2D 叉積**，判斷點 P 在邊 AB 的哪一側。

$$E(A, B, P) = (B_x - A_x)(P_y - A_y) - (B_y - A_y)(P_x - A_x)$$

```cpp
inline float edgeFn(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx-ax)*(py-ay) - (by-ay)*(px-ax);
}
```

- 正值 → P 在 AB 左側（CCW 三角形的「內側」）
- 負值 → P 在 AB 右側（外側）

### 5-3 完整光柵化函式

```cpp
void swRasterizeTriangle(vec3 color, vec3 ndc1, vec3 ndc2, vec3 ndc3)
{
    // Step 1: NDC → Screen pixels
    float sx0 = (ndc1.x + 1.0f) * 0.5f * (winWidth  - 1);
    float sy0 = (ndc1.y + 1.0f) * 0.5f * (winHeight - 1);
    // ... (sx1,sy1), (sx2,sy2) 同理

    // Step 2: 簽名面積（判斷 winding order）
    float area = edgeFn(sx0,sy0, sx1,sy1, sx2,sy2);
    if (fabsf(area) < 1e-6f) return;  // 退化三角形

    // Step 3: Bounding Box + 邊界 Clamp
    int minX = max(0,          (int)floor(min({sx0,sx1,sx2})));
    int maxX = min(winWidth-1, (int)ceil (max({sx0,sx1,sx2})));
    int minY = max(0,          (int)floor(min({sy0,sy1,sy2})));
    int maxY = min(winHeight-1,(int)ceil (max({sy0,sy1,sy2})));

    // Step 4: 逐像素掃描
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float px = x + 0.5f, py = y + 0.5f;  // 像素中心

            // Barycentric 權重（未正規化）
            float w0 = edgeFn(sx1,sy1, sx2,sy2, px,py);  // ← 第0頂點的權重
            float w1 = edgeFn(sx2,sy2, sx0,sy0, px,py);
            float w2 = edgeFn(sx0,sy0, sx1,sy1, px,py);

            // 內部判斷：三個權重符號必須與 area 相同
            if (area > 0) { if (w0<0||w1<0||w2<0) continue; }
            else          { if (w0>0||w1>0||w2>0) continue; }

            // 正規化 → 三角形重心座標 (λ0 + λ1 + λ2 = 1)
            float l0 = w0/area, l1 = w1/area, l2 = w2/area;

            // 插值 z_ndc
            float z = l0*ndc1.z + l1*ndc2.z + l2*ndc3.z;

            // Z-Test：比舊值小才通過（closer to camera）
            int idx = y * winWidth + x;
            if (z < zBuffer[idx]) {
                zBuffer[idx]     = z;
                frameBuffer[idx] = vec4(color, 1.0f);  // alpha=1 標記已寫入
            }
        }
    }
}
```

### 5-4 Display 中上傳 framebuffer

```cpp
// 設定 2D ortho 投影，確保 (0,0) 對應到左下角像素
glMatrixMode(GL_PROJECTION);
glPushMatrix(); glLoadIdentity();
glOrtho(0, winWidth, 0, winHeight, -1, 1);
glMatrixMode(GL_MODELVIEW);
glPushMatrix(); glLoadIdentity();
glRasterPos2i(0, 0);

// Alpha blend：空像素（alpha=0）透明，讓 OpenGL 畫的 grid 透出來
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
glDisable(GL_DEPTH_TEST);

glDrawPixels(winWidth, winHeight, GL_RGBA, GL_FLOAT, frameBuffer.data());

glEnable(GL_DEPTH_TEST);
glDisable(GL_BLEND);
glPopMatrix();
glMatrixMode(GL_PROJECTION); glPopMatrix();
```

### 💡 重心座標（Barycentric Coordinates）直觀理解

```
          v0
         /|\
        / | \
   λ2  /  |  \  λ1
      /   P   \
     /    |    \
    v2────────── v1
          λ0

P = λ0*v0 + λ1*v1 + λ2*v2,  其中 λ0+λ1+λ2 = 1

λ0：P 距離 v1-v2 邊的「比例距離」→ 代表 v0 的影響力
任何頂點屬性（顏色、深度、UV）都可以用這樣插值
```

---

## 鍵盤快捷鍵總表

| 按鍵 | 功能 |
|------|------|
| `F1` | 新增四面體 |
| `F2` / `C` | 切換到 Cube 模型 |
| `T` | 切換到四面體 |
| `O` | 讀取 `model.obj` |
| `Q/A` | 世界空間 +X / -X 平移 |
| `W/S` | 世界空間 +Y / -Y 平移 |
| `E/R` | 物體空間 +Z / -Z 旋轉 |
| `Z/X` | 物體空間放大 / 縮小 |
| `9/0` | 旋轉相機軌道角 θ |
| `-` | 重置 transformMat = I |
| `[` | swPushMatrix（儲存狀態） |
| `]` | swPopMatrix（還原狀態） |
| `K` | 儲存場景到 `scene.txt` |
| `L` | 從 `scene.txt` 載入場景 |
| `ESC` | 離開程式 |

---

## 常見錯誤與除錯技巧

### ❌ 矩陣填入位置錯誤

```cpp
// 錯誤：把 Tx 放在 mat[0][3]（這是 Row-Major 的寫法）
Translate[0][3] = x;

// 正確：GLM Column-Major，平移在 mat[3][0..2]
Translate[3][0] = x;
Translate[3][1] = y;
Translate[3][2] = z;
```

### ❌ 透視除法漏做

```cpp
// 錯誤：直接用 clip space 座標繪圖
glVertex3f(v_clip.x, v_clip.y, v_clip.z);

// 正確：先除以 w 才是 NDC
float w = v_clip.w;
glVertex3f(v_clip.x/w, v_clip.y/w, v_clip.z/w);
```

### ❌ 旋轉矩陣符號錯誤

旋轉矩陣的 sin 符號取決於**右手法則**；轉換成 GLM Column-Major 時，數學上的 $R_{ij}$ 要放在 `mat[j][i]`。

### ❌ OBJ 索引 off-by-one

OBJ 格式是 **1-based**，讀進來後必須 `-1` 轉成 0-based：
```cpp
face.idx[i] = std::stoi(...) - 1;  // 不能忘記 -1
```

### ❌ glBegin/glEnd 包住 swTriangle 呼叫

Phase 5 後 `swTriangle` 已經是 self-contained，不需要 `glBegin/glEnd`：
```cpp
// 錯誤（Phase 5 後）：
glBegin(GL_TRIANGLES);
swTriangle(...);  // 內部不再呼叫 glVertex3f
glEnd();

// 正確：
swTriangle(...);  // 直接呼叫
```

### 🔍 除錯工具：printMat

```cpp
printMat("ModelMat", transformMat);
// 輸出：
// ModelMat:
//   [1.000  0.000  0.000  2.000]
//   [0.000  1.000  0.000  0.000]
//   [0.000  0.000  1.000  0.000]
//   [0.000  0.000  0.000  1.000]
//                        ^ Tx=2 在這裡
```

---

## 效能注意

| 功能 | 效能影響 |
|------|----------|
| 四面體（4 個三角形）| 幾乎無負擔 |
| Cube（12 個三角形）| 無感 |
| OBJ 複雜模型（數千面）| **明顯變慢**，因為 CPU 逐像素計算 |
| 全解析度 1280×720 | 每幀 ~921,600 個 z-test |

軟體光柵化本質上是 **O(W × H × N_triangles)**，這正是為什麼 GPU 用大量並行核心的原因。

---

*筆記對應版本：main.cpp HW-SWGL 2026, Phase 1–5 完整實作*

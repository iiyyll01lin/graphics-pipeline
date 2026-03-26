//Computer Graphics HW-SWGL 2026
//simple modeling tool
//Transformation and Projection

#include<iostream>
#include<vector>
#include<cmath>
#include<fstream>
#include<sstream>
#include<string>
#include<limits>       // std::numeric_limits  (Phase 5 z-buffer)
#include<algorithm>    // std::fill, std::min, std::max (Phase 5)

#include <GL/glu.h>
#include <GLFW/glfw3.h>

//OpenGL Mathematics (GLM)  https://glm.g-truc.net/
#include<glm/vec3.hpp>
#include<glm/vec4.hpp>
#include<glm/mat4x4.hpp>
#include<glm/geometric.hpp>  // normalize, cross, dot, length


using namespace std;
using namespace glm;

const bool STEP2 = true;
const bool STEP3 = true;

float theta = 3.14159f / 4.0f;
float tho = 3.14159f / 4.0f;

int winWidth = 1280;
int winHeight = 720;

mat4x4 transformMat = mat4x4(1);

mat4x4 ViewMat = mat4x4(1);
mat4x4 ProjectionMat = mat4x4(1);

// ── Phase 2: Matrix Stack for scene-graph hierarchy ──────────────────────────
std::vector<mat4x4> matrixStack;  // LIFO stack of saved model matrices

// Orbital angle for the child (satellite) tetrahedron animation
float childAngle = 0.0f;

// ── Phase 3 & 4: Active model enum and scene state ──────────────────────
enum ActiveModel { MODEL_TETRAHEDRON = 0, MODEL_CUBE = 1, MODEL_OBJ = 2 };
ActiveModel activeModel = MODEL_TETRAHEDRON;

// OBJ mesh storage (Phase 3)
struct ObjFace { int idx[3]; };
std::vector<vec3>    objMeshVerts;
std::vector<ObjFace> objMeshFaces;
bool objLoaded = false;

// ── Phase 5: CPU framebuffer and z-buffer ────────────────────────────────
// frameBuffer: RGBA [0,1]; alpha=0 = empty (blends over the OpenGL grid),
//              alpha=1 = rasterised pixel
// zBuffer:     stores z_ndc [-1,1]; init to +inf so first fragment always wins
std::vector<vec4>  frameBuffer;
std::vector<float> zBuffer;

void printMat(const char* name, mat4x4 m) {
    printf("%s:\n", name);
    for (int row = 0; row < 4; row++) {
        printf("  [%.3f  %.3f  %.3f  %.3f]\n",
            m[0][row], m[1][row], m[2][row], m[3][row]);
    }
}

vec3 default_tetrahedron_vertices[4] = {
	vec3(1,0,0), vec3(0,1,0), vec3(0,0,1), vec3(0,0,0)
};
vec3 tetrahedron_verts[4];


// NOTE: GLM uses COLUMN-MAJOR order: mat[col][row]
// A translation matrix looks like:
//   mat[0] = {1, 0, 0, 0}   (column 0)
//   mat[1] = {0, 1, 0, 0}   (column 1)
//   mat[2] = {0, 0, 1, 0}   (column 2)
//   mat[3] = {x, y, z, 1}   (column 3 holds translation)
// So translation T_x is at mat[3][0], T_y at mat[3][1], T_z at mat[3][2]

//step1: implement Translate Matrix
mat4x4 swTranslate(float x, float y, float z)
{
	mat4x4 Translate = mat4x4(1);
	// GLM column-major: mat[col][row]; translation stored in column 3
	Translate[3][0] = x;
	Translate[3][1] = y;
	Translate[3][2] = z;
	return Translate;
}

//step1: implement Rotate Matrix
mat4x4 swRotateX(float angle)
{
	mat4x4 Rotate = mat4x4(1);
	// Rotation around X-axis; GLM column-major: mat[col][row]
	Rotate[1][1] =  cos(angle);
	Rotate[1][2] =  sin(angle);
	Rotate[2][1] = -sin(angle);
	Rotate[2][2] =  cos(angle);
	return Rotate;
}

mat4x4 swRotateY(float angle)
{
	mat4x4 Rotate = mat4x4(1);
	// Rotation around Y-axis; GLM column-major: mat[col][row]
	Rotate[0][0] =  cos(angle);
	Rotate[0][2] = -sin(angle);
	Rotate[2][0] =  sin(angle);
	Rotate[2][2] =  cos(angle);
	return Rotate;
}

mat4x4 swRotateZ(float angle)
{
	mat4x4 Rotate = mat4x4(1);
	// Rotation around Z-axis; GLM column-major: mat[col][row]
	Rotate[0][0] =  cos(angle);
	Rotate[0][1] =  sin(angle);
	Rotate[1][0] = -sin(angle);
	Rotate[1][1] =  cos(angle);
	return Rotate;
}

//optional: arbitrary axis rotation via Rodrigues' formula
mat4x4 swRotate(float angle, float x, float y, float z)
{
	mat4x4 Rotate = mat4x4(1);
	vec3 axis(x, y, z);
	float len = length(axis);
	if (len < 1e-6f) return Rotate;
	axis = normalize(axis);
	float nx = axis.x, ny = axis.y, nz = axis.z;
	float c = cos(angle), s = sin(angle), t = 1.0f - c;
	// GLM column-major: Rotate[col][row]
	Rotate[0][0] = t*nx*nx + c;     Rotate[1][0] = t*nx*ny - s*nz;  Rotate[2][0] = t*nx*nz + s*ny;
	Rotate[0][1] = t*nx*ny + s*nz;  Rotate[1][1] = t*ny*ny + c;     Rotate[2][1] = t*ny*nz - s*nx;
	Rotate[0][2] = t*nx*nz - s*ny;  Rotate[1][2] = t*ny*nz + s*nx;  Rotate[2][2] = t*nz*nz + c;
	return Rotate;
}

//step1: implement Scale(x, y, z)
mat4x4 swScale(float x, float y, float z)
{
	mat4x4 Scale = mat4x4(1);
	// Uniform diagonal scale; GLM column-major: mat[col][row]
	Scale[0][0] = x;
	Scale[1][1] = y;
	Scale[2][2] = z;
	return Scale;
}

//step2:
mat4x4 swLookAt(float eyeX, float eyeY, float eyeZ,
    float centerX, float centerY, float centerZ,
    float upX, float upY, float upZ)
{
    vec3 eye(eyeX, eyeY, eyeZ);
    vec3 center(centerX, centerY, centerZ);
    vec3 up(upX, upY, upZ);

    // Build orthonormal camera basis
    vec3 f = normalize(center - eye);   // forward (into scene)
    vec3 s = normalize(cross(f, up));   // right
    vec3 u = cross(s, f);               // true up (recomputed to ensure orthogonality)

    // V = R * T(-eye); GLM column-major: mat[col][row]
    mat4x4 V = mat4x4(1);
    V[0][0] =  s.x;  V[1][0] =  s.y;  V[2][0] =  s.z;  V[3][0] = -dot(s, eye);
    V[0][1] =  u.x;  V[1][1] =  u.y;  V[2][1] =  u.z;  V[3][1] = -dot(u, eye);
    V[0][2] = -f.x;  V[1][2] = -f.y;  V[2][2] = -f.z;  V[3][2] =  dot(f, eye);
    V[0][3] =  0.0f; V[1][3] =  0.0f; V[2][3] =  0.0f; V[3][3] =  1.0f;
    return V;
}

// ── Phase 2: Matrix Stack push / pop ────────────────────────────────────────

// swPushMatrix: saves a copy of the current transformMat onto the stack
void swPushMatrix()
{
    matrixStack.push_back(transformMat);
}

// swPopMatrix: restores transformMat from the top of the stack
void swPopMatrix()
{
    if (!matrixStack.empty()) {
        transformMat = matrixStack.back();
        matrixStack.pop_back();
    }
}

// ── Phase 3: OBJ Loader ─────────────────────────────────────────────────────────────
bool loadOBJ(const char* path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "loadOBJ: cannot open '" << path << "'\n";
        return false;
    }
    objMeshVerts.clear();
    objMeshFaces.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;
        ss >> token;
        if (token == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            objMeshVerts.push_back(vec3(x, y, z));
        } else if (token == "f") {
            ObjFace face;
            bool ok = true;
            for (int i = 0; i < 3; i++) {
                std::string ft;
                if (!(ss >> ft)) { ok = false; break; }
                // handles "v", "v/vt", "v/vt/vn" — take only vertex index
                int vi = std::stoi(ft.substr(0, ft.find('/')));
                face.idx[i] = vi - 1;  // OBJ is 1-based
            }
            if (ok) objMeshFaces.push_back(face);
        }
    }
    std::cout << "loadOBJ: " << objMeshVerts.size() << " verts, "
              << objMeshFaces.size() << " faces from '" << path << "'\n";
    return !objMeshFaces.empty();
}

// ── Phase 4: Scene Save / Load ─────────────────────────────────────────────────────
// State saved: theta (camera orbit angle), activeModel, transformMat (16 floats, column-major)
void SaveScene(const char* filename)
{
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cout << "SaveScene: cannot open '" << filename << "' for writing\n";
        return;
    }
    f << "theta " << theta << "\n";
    f << "model " << (int)activeModel << "\n";
    f << "mat";
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            f << " " << transformMat[col][row];
    f << "\n";
    f.close();
    std::cout << "SaveScene: saved to '" << filename << "'\n";
}

void LoadScene(const char* filename)
{
    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cout << "LoadScene: cannot open '" << filename << "'\n";
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "theta") {
            ss >> theta;
        } else if (key == "model") {
            int m; ss >> m;
            activeModel = static_cast<ActiveModel>(m);
        } else if (key == "mat") {
            for (int col = 0; col < 4; col++)
                for (int row = 0; row < 4; row++)
                    ss >> transformMat[col][row];
        }
    }
    f.close();
    std::cout << "LoadScene: loaded from '" << filename << "'\n";
}

// ── Phase 5: framebuffer clear ───────────────────────────────────────────────
void ClearBuffers()
{
    std::fill(frameBuffer.begin(), frameBuffer.end(), vec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::fill(zBuffer.begin(),     zBuffer.end(),     std::numeric_limits<float>::infinity());
}

// ── Phase 5: edge function helper (2D signed area of parallelogram b−a, p−a) ───
static inline float edgeFn(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// ── Phase 5: software triangle rasterizer ────────────────────────────────
// ndc1/2/3: vertices in NDC space [-1,1] after perspective division
// color:    flat face colour (same for all three vertices)
// Writes RGBA + depth directly to frameBuffer / zBuffer (CPU memory)
void swRasterizeTriangle(vec3 color, vec3 ndc1, vec3 ndc2, vec3 ndc3)
{
    // --- Viewport transform: NDC [-1,1] -> screen pixels [0, W/H − 1] ----------
    float sx0 = (ndc1.x + 1.0f) * 0.5f * (winWidth  - 1);
    float sy0 = (ndc1.y + 1.0f) * 0.5f * (winHeight - 1);
    float sx1 = (ndc2.x + 1.0f) * 0.5f * (winWidth  - 1);
    float sy1 = (ndc2.y + 1.0f) * 0.5f * (winHeight - 1);
    float sx2 = (ndc3.x + 1.0f) * 0.5f * (winWidth  - 1);
    float sy2 = (ndc3.y + 1.0f) * 0.5f * (winHeight - 1);

    // --- Signed area; encodes winding order for inside-test ----------------------
    float area = edgeFn(sx0, sy0, sx1, sy1, sx2, sy2);
    if (fabsf(area) < 1e-6f) return;   // degenerate / zero-area triangle

    // --- Axis-aligned bounding box clamped to screen ----------------------------
    int minX = std::max(0,             (int)floorf(std::min({sx0, sx1, sx2})));
    int maxX = std::min(winWidth  - 1, (int)ceilf (std::max({sx0, sx1, sx2})));
    int minY = std::max(0,             (int)floorf(std::min({sy0, sy1, sy2})));
    int maxY = std::min(winHeight - 1, (int)ceilf (std::max({sy0, sy1, sy2})));

    // --- Per-pixel loop -----------------------------------------------------------
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float px = (float)x + 0.5f;   // sample at pixel centre
            float py = (float)y + 0.5f;

            // Barycentric weights (unnormalised); same sign as area => inside
            float w0 = edgeFn(sx1, sy1, sx2, sy2, px, py);  // weight for ndc1
            float w1 = edgeFn(sx2, sy2, sx0, sy0, px, py);  // weight for ndc2
            float w2 = edgeFn(sx0, sy0, sx1, sy1, px, py);  // weight for ndc3

            // Inside test: all weights must match sign of area
            if (area > 0.0f) { if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue; }
            else              { if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f) continue; }

            // Normalise -> barycentric coordinates λ0+λ1+λ2 = 1
            float l0 = w0 / area;   // λ for ndc1
            float l1 = w1 / area;   // λ for ndc2
            float l2 = w2 / area;   // λ for ndc3

            // Interpolated z_ndc for depth test (smaller = closer to camera)
            float z = l0 * ndc1.z + l1 * ndc2.z + l2 * ndc3.z;

            // Z-buffer test: keep the fragment with the smallest z_ndc
            int idx = y * winWidth + x;
            if (z < zBuffer[idx]) {
                zBuffer[idx]     = z;
                // Flat shading; alpha=1 marks a written pixel for blending
                frameBuffer[idx] = vec4(color.r, color.g, color.b, 1.0f);
            }
        }
    }
}

// step3: implement perspective projection matrix
mat4x4 swPerspective(float fovyDeg, float aspect, float zNear, float zFar)
{
    mat4x4 P = mat4x4(0);  // start with all zeros
    float fovyRad = fovyDeg * 3.14159265358979f / 180.0f;
    float f = 1.0f / tan(fovyRad / 2.0f);

    // Standard OpenGL perspective frustum matrix; GLM column-major: mat[col][row]
    P[0][0] = f / aspect;
    P[1][1] = f;
    P[2][2] = (zFar + zNear) / (zNear - zFar);
    P[2][3] = -1.0f;                                      // w_clip = -z_view
    P[3][2] = (2.0f * zFar * zNear) / (zNear - zFar);
    return P;
}


void swTriangle(vec3 color, vec3 in_v1, vec3 in_v2, vec3 in_v3, mat4x4 Modelmatrix)
{
	// vertices start in OBJECT SPACE
	vec4 v1(in_v1.x, in_v1.y, in_v1.z, 1);
	vec4 v2(in_v2.x, in_v2.y, in_v2.z, 1);
	vec4 v3(in_v3.x, in_v3.y, in_v3.z, 1);

	// OBJECT -> WORLD SPACE  (model transform, step1)
	v1 = Modelmatrix * v1;
	v2 = Modelmatrix * v2;
	v3 = Modelmatrix * v3;

	// WORLD -> VIEW (CAMERA) SPACE  (step2: apply view matrix, replace gluLookAt)
	if (STEP2) {
		v1 = ViewMat * v1;
		v2 = ViewMat * v2;
		v3 = ViewMat * v3;
	}

	// VIEW -> CLIP SPACE -> NDC  (step3: projection + perspective divide)
	// Full chain: v_clip = Projection * View * Model * v_obj
	if (STEP3) {
		v1 = ProjectionMat * v1;
		v2 = ProjectionMat * v2;
		v3 = ProjectionMat * v3;
		// Perspective division: x_ndc = x_clip/w_clip, y_ndc = y_clip/w_clip, z_ndc = z_clip/w_clip
		v1 = vec4(v1.x / v1.w, v1.y / v1.w, v1.z / v1.w, 1.0f);
		v2 = vec4(v2.x / v2.w, v2.y / v2.w, v2.z / v2.w, 1.0f);
		v3 = vec4(v3.x / v3.w, v3.y / v3.w, v3.z / v3.w, 1.0f);
	}

	// Phase 5: dispatch to software rasterizer OR OpenGL fallback
	if (STEP3) {
		// Software path: write directly to CPU frameBuffer / zBuffer
		swRasterizeTriangle(color, vec3(v1), vec3(v2), vec3(v3));
	} else {
		// Fallback: OpenGL immediate mode (STEP3 disabled; caller must NOT wrap in glBegin)
		glBegin(GL_TRIANGLES);
		glColor3f(color.r, color.g, color.b);
		glVertex3f(v1.x, v1.y, v1.z);
		glVertex3f(v2.x, v2.y, v2.z);
		glVertex3f(v3.x, v3.y, v3.z);
		glEnd();
	}
}



// ── Phase 3: Procedural Cube (12 triangles, CCW winding from outside) ────────
void Draw_Cube()
{
	// Unit cube centered at origin; vertices listed: [col][row] stays GLM convention
	vec3 v[8] = {
		vec3(-0.5f, -0.5f, -0.5f), // 0
		vec3( 0.5f, -0.5f, -0.5f), // 1
		vec3( 0.5f,  0.5f, -0.5f), // 2
		vec3(-0.5f,  0.5f, -0.5f), // 3
		vec3(-0.5f, -0.5f,  0.5f), // 4
		vec3( 0.5f, -0.5f,  0.5f), // 5
		vec3( 0.5f,  0.5f,  0.5f), // 6
		vec3(-0.5f,  0.5f,  0.5f), // 7
	};
	// Phase 5: no glBegin/glEnd — swTriangle is now self-contained
	// Front  (z=+0.5) — red
	swTriangle(vec3(1,0,0), v[4], v[5], v[6], transformMat);
	swTriangle(vec3(1,0,0), v[4], v[6], v[7], transformMat);
	// Back   (z=-0.5) — green
	swTriangle(vec3(0,1,0), v[1], v[0], v[3], transformMat);
	swTriangle(vec3(0,1,0), v[1], v[3], v[2], transformMat);
	// Left   (x=-0.5) — blue
	swTriangle(vec3(0,0,1), v[0], v[4], v[7], transformMat);
	swTriangle(vec3(0,0,1), v[0], v[7], v[3], transformMat);
	// Right  (x=+0.5) — yellow
	swTriangle(vec3(1,1,0), v[5], v[1], v[2], transformMat);
	swTriangle(vec3(1,1,0), v[5], v[2], v[6], transformMat);
	// Bottom (y=-0.5) — cyan
	swTriangle(vec3(0,1,1), v[0], v[1], v[5], transformMat);
	swTriangle(vec3(0,1,1), v[0], v[5], v[4], transformMat);
	// Top    (y=+0.5) — magenta
	swTriangle(vec3(1,0,1), v[7], v[6], v[2], transformMat);
	swTriangle(vec3(1,0,1), v[7], v[2], v[3], transformMat);
}

// ── Phase 3: OBJ mesh draw ────────────────────────────────────────────────────
void Draw_OBJ()
{
	if (!objLoaded || objMeshFaces.empty()) {
		std::cout << "Draw_OBJ: no OBJ loaded. Press 'O' to load model.obj\n";
		return;
	}
	// Cycle 6 colours across faces for visual clarity
	static const vec3 palette[6] = {
		vec3(1,0,0), vec3(0,1,0), vec3(0,0,1),
		vec3(1,1,0), vec3(0,1,1), vec3(1,0,1)
	};
	// Phase 5: no glBegin/glEnd — swTriangle is self-contained
	for (size_t i = 0; i < objMeshFaces.size(); i++) {
		const ObjFace& face = objMeshFaces[i];
		// bounds-check each index before drawing
		if (face.idx[0] < 0 || face.idx[0] >= (int)objMeshVerts.size() ||
		    face.idx[1] < 0 || face.idx[1] >= (int)objMeshVerts.size() ||
		    face.idx[2] < 0 || face.idx[2] >= (int)objMeshVerts.size()) continue;
		swTriangle(palette[i % 6],
			objMeshVerts[face.idx[0]],
			objMeshVerts[face.idx[1]],
			objMeshVerts[face.idx[2]],
			transformMat);
	}
}

void Draw_Tetrahedron() {
	// Phase 5: no glBegin/glEnd — swTriangle is self-contained
	// ── Parent tetrahedron (current transformMat) ─────────────────────────────
	swTriangle(vec3(1, 0, 0), tetrahedron_verts[0], tetrahedron_verts[1], tetrahedron_verts[2], transformMat);
	swTriangle(vec3(0, 0, 1), tetrahedron_verts[3], tetrahedron_verts[0], tetrahedron_verts[1], transformMat);
	swTriangle(vec3(0, 1, 0), tetrahedron_verts[2], tetrahedron_verts[3], tetrahedron_verts[0], transformMat);
	swTriangle(vec3(1, 1, 0), tetrahedron_verts[1], tetrahedron_verts[2], tetrahedron_verts[3], transformMat);

	// ── Child (satellite) tetrahedron: Phase 2 matrix stack hierarchy ─────────
	// swPushMatrix saves the parent's world transform so swPopMatrix can restore it.
	swPushMatrix();
	//   RIGHT-multiply: transformMat * localChild  =>  Local transform in parent space.
	//   The child orbits around the parent origin at radius 3 and is scaled to 0.4.
	transformMat = transformMat
	            * swTranslate(3.0f * cosf(childAngle), 3.0f * sinf(childAngle), 0.5f)
	            * swScale(0.4f, 0.4f, 0.4f);
	swTriangle(vec3(1.0f, 0.5f, 0.0f), tetrahedron_verts[0], tetrahedron_verts[1], tetrahedron_verts[2], transformMat);
	swTriangle(vec3(0.5f, 0.0f, 1.0f), tetrahedron_verts[3], tetrahedron_verts[0], tetrahedron_verts[1], transformMat);
	swTriangle(vec3(0.0f, 1.0f, 0.5f), tetrahedron_verts[2], tetrahedron_verts[3], tetrahedron_verts[0], transformMat);
	swTriangle(vec3(1.0f, 1.0f, 0.5f), tetrahedron_verts[1], tetrahedron_verts[2], tetrahedron_verts[3], transformMat);
	swPopMatrix();  // restores parent's transformMat
}

void DrawGrid(int size = 10)
{
	glBegin(GL_LINES);
	glColor3f(0.3, 0.3, 0.3);
	for (int i = 1; i < size; i++) {
		glVertex3f(i, -size, 0);
		glVertex3f(i, size, 0);
		glVertex3f(-i, -size, 0);
		glVertex3f(-i, size, 0);
		glVertex3f(-size, i, 0);
		glVertex3f(size, i, 0);
		glVertex3f(-size, -i, 0);
		glVertex3f(size, -i, 0);
	}
	glEnd();

	glBegin(GL_LINES);
		glColor3f(1, 0, 0);
		glVertex3f(0, 0, 0);
		glVertex3f(size, 0, 0);
		glColor3f(0.4, 0, 0);
		glVertex3f(0, 0, 0);
		glVertex3f(-size, 0, 0);

		glColor3f(0, 1, 0);
		glVertex3f(0, 0, 0);
		glVertex3f(0, size, 0);
		glColor3f(0, 0.4, 0);
		glVertex3f(0, 0, 0);
		glVertex3f(0, -size, 0);

		glColor3f(0, 0, 1);
		glVertex3f(0, 0, 0);
		glVertex3f(0, 0, size);
	glEnd();
}

void Display(GLFWwindow* window)
{
	// Phase 5: clear CPU framebuffer and z-buffer every frame
	ClearBuffers();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glViewport(0, 0, winWidth, winHeight);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(60, 1, 0.1, 50);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(10 * cos(theta), -10 * sin(theta), 10, 0, 0, 0, 0, 0, 1);

	DrawGrid();

	//step 3: PROJECTION
	if (STEP3 == true) {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
        //glOrtho(0, winWidth, 0, winHeight, -2.0, 2.0);
		//ProjectionMat = mat4x4(1);
		ProjectionMat = swPerspective(60, (float)winWidth / (float)winHeight, 0.1, 50);  // uses your implementation
	}

	//step 2: viewing
	if (STEP2 == true) {
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		ViewMat = swLookAt(10 * cos(theta), -10 * sin(theta), 10, 0, 0, 0, 0, 0, 1);  // sets ViewMat
	}

	// Phase 3: dispatch to active model
	switch (activeModel) {
		case MODEL_TETRAHEDRON: Draw_Tetrahedron(); break;
		case MODEL_CUBE:        Draw_Cube();        break;
		case MODEL_OBJ:         Draw_OBJ();         break;
	}

	// Phase 5: upload software-rasterised framebuffer via glDrawPixels
	// Use a 2D ortho projection so raster pos (0,0) maps to the bottom-left pixel.
	// Alpha blending: empty pixels (alpha=0) are transparent => OpenGL grid shows through.
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, winWidth, 0, winHeight, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glRasterPos2i(0, 0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glDrawPixels(winWidth, winHeight, GL_RGBA, GL_FLOAT, frameBuffer.data());
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glFlush();
	glfwSwapBuffers(window);
}

void init() {
	glClearColor(0, 0, 0, 0);
	glClearDepth(1.0);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	// Phase 5: allocate CPU framebuffer and z-buffer matching the window dimensions
	frameBuffer.assign(winWidth * winHeight, vec4(0.0f));
	zBuffer.assign(winWidth * winHeight, std::numeric_limits<float>::infinity());
}

// Converted special key function for GLFW.
void SpecialKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Process only key press and repeated actions.
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;
    
    switch (key) {
        case GLFW_KEY_F1:
            glfwSetWindowTitle(window, "F1: add a tetrahedron");
            for (int i = 0; i < 4; i++) {
                tetrahedron_verts[i][0] = default_tetrahedron_vertices[i][0];
                tetrahedron_verts[i][1] = default_tetrahedron_vertices[i][1];
                tetrahedron_verts[i][2] = default_tetrahedron_vertices[i][2];
            }
            break;
            
        case GLFW_KEY_F2:
            glfwSetWindowTitle(window, "F2: Model = Cube");
            activeModel = MODEL_CUBE;
            break;
            
        case GLFW_KEY_F5:
            glfwSetWindowTitle(window, "F5: SAVE");
            // Add save functionality here.

            break;
            
        case GLFW_KEY_F6:
            glfwSetWindowTitle(window, "F6: LOAD");
            // Add load functionality here.

            break;
            
        default:
            break;
    }
}


void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Here you can handle both regular and special keys.
    // Call your special key function for function keys.
    if (key == GLFW_KEY_F1 || key == GLFW_KEY_F2 ||
        key == GLFW_KEY_F5 || key == GLFW_KEY_F6) {
        SpecialKey(window, key, scancode, action, mods);
    }
    
    // Add handling for other keys as needed.

    // Only handle press and repeat events.
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    switch (key) {
        case GLFW_KEY_ESCAPE:  // ESC key
            exit(0);
            break;

        // rotate world
        case GLFW_KEY_9:
            theta += 3.14159f / 90.0f;
            break;
        case GLFW_KEY_0:
            theta -= 3.14159f / 90.0f;
            break;
        case GLFW_KEY_MINUS:
            transformMat = mat4x4(1);
            break;

        // translate +x (handles both 'q' and 'Q')
        case GLFW_KEY_Q:
            glfwSetWindowTitle(window, "translate +x");
            // LEFT-multiply: T_new * transformMat => Global (world-space) translation
            transformMat = swTranslate(1, 0, 0) * transformMat;
            break;

        // translate -x (handles both 'a' and 'A')
        case GLFW_KEY_A:
            glfwSetWindowTitle(window, "translate -x");
            // LEFT-multiply: T_new * transformMat => Global (world-space) translation
            transformMat = swTranslate(-1, 0, 0) * transformMat;
            break;

        // translate +y (handles both 'w' and 'W')
        case GLFW_KEY_W:
            glfwSetWindowTitle(window, "translate +y");
            // LEFT-multiply: T_new * transformMat => Global (world-space) translation
            transformMat = swTranslate(0, 1, 0) * transformMat;
            break;

        // translate -y (handles both 's' and 'S')
        case GLFW_KEY_S:
            glfwSetWindowTitle(window, "translate -y");
            // LEFT-multiply: T_new * transformMat => Global (world-space) translation
            transformMat = swTranslate(0, -1, 0) * transformMat;
            break;

        // Phase 2: manual matrix stack push / pop for debugging hierarchy
        // '[' = push current transformMat; ']' = pop (restore last saved)
        case GLFW_KEY_LEFT_BRACKET:
            glfwSetWindowTitle(window, "swPushMatrix()");
            swPushMatrix();
            break;
        case GLFW_KEY_RIGHT_BRACKET:
            glfwSetWindowTitle(window, "swPopMatrix()");
            swPopMatrix();
            break;

        // Rotate tetrahedron around Z-axis (E/R keys)
        case GLFW_KEY_E:
            glfwSetWindowTitle(window, "rotate +Z");
            // RIGHT-multiply: transformMat * R  => Local (object-space) rotation
            transformMat = transformMat * swRotateZ(3.14159f / 18.0f);
            break;
        case GLFW_KEY_R:
            glfwSetWindowTitle(window, "rotate -Z");
            // RIGHT-multiply: transformMat * R  => Local (object-space) rotation
            transformMat = transformMat * swRotateZ(-3.14159f / 18.0f);
            break;

        // Scale up/down (Z/X keys)
        case GLFW_KEY_Z:
            glfwSetWindowTitle(window, "scale up");
            // RIGHT-multiply: transformMat * S  => Local (object-space) scale
            transformMat = transformMat * swScale(1.1f, 1.1f, 1.1f);
            break;
        case GLFW_KEY_X:
            glfwSetWindowTitle(window, "scale down");
            // RIGHT-multiply: transformMat * S  => Local (object-space) scale
            transformMat = transformMat * swScale(0.9f, 0.9f, 0.9f);
            break;

        // Phase 3: model switching
        case GLFW_KEY_T:
            glfwSetWindowTitle(window, "Model: Tetrahedron");
            activeModel = MODEL_TETRAHEDRON;
            break;
        case GLFW_KEY_C:
            glfwSetWindowTitle(window, "Model: Cube");
            activeModel = MODEL_CUBE;
            break;
        case GLFW_KEY_O:
            // Attempt to load model.obj from the working directory
            if (loadOBJ("model.obj")) {
                objLoaded = true;
                activeModel = MODEL_OBJ;
                glfwSetWindowTitle(window, "Model: OBJ (model.obj)");
            } else {
                glfwSetWindowTitle(window, "OBJ load FAILED - place model.obj next to exe");
            }
            break;

        // Phase 4: scene save / load (K = save, L = load)
        case GLFW_KEY_K:
            SaveScene("scene.txt");
            glfwSetWindowTitle(window, "Scene saved -> scene.txt");
            break;
        case GLFW_KEY_L:
            LoadScene("scene.txt");
            glfwSetWindowTitle(window, "Scene loaded <- scene.txt");
            break;

        default:
            break;
    }
}


int main(void)
{
    // Initialize GLFW
    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }

    // Create a windowed mode window and its OpenGL context
    GLFWwindow* window = glfwCreateWindow(winWidth, winHeight, "trans: Press F1 to add a tetrahedron", NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    // Make the window's context current
    glfwMakeContextCurrent(window);

    // Set callback for keyboard events
    glfwSetKeyCallback(window, KeyCallback);

    // Initialize OpenGL
    init();

    // Timing for periodic updates (~33ms interval)
    double previousTime = glfwGetTime();
    const double interval = 0.033; // ~33ms

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Check if it's time to update (simulate timer)
        double currentTime = glfwGetTime();
        if (currentTime - previousTime >= interval) {
            // Advance child satellite orbit angle (~30 fps * 0.02 rad = ~0.6 rad/s)
            childAngle += 0.02f;
            previousTime = currentTime;
        }

        // Render here
        Display(window);

        // Poll for and process events
        glfwPollEvents();
    }

    // Clean up and exit
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
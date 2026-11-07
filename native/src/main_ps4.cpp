// Copyright (c) 2026 Sabak Studios. All rights reserved.
// PS4 platform layer (OpenOrbis "piglet" sample base): the console GPU (Sony's Piglet / OpenGL ES 2.0 with the
// precompiled system shaders) presents and upscales the CPU-rendered frame to 1080p. Pad via ScePad, settings in /data.
// GPU init follows the war project's proven path (privileged eboot + Piglet). Built by .github/workflows/ps4-native.yml.
#include "game.h"
#include "mp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <orbis/libkernel.h>
#include <orbis/Pigletv2VSH.h>
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <cmath>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>

static const char* CFG_PATH = "/data/hairpin_rush.cfg";

static float axis_norm(uint8_t v) {                       // 0..255 -> -1..1 with a dead zone
    float f = (float(v) - 127.5f) / 127.5f;
    return fabsf(f) < 0.12f ? 0.0f : f;
}

static void load_cfg(Game& g) {
    FILE* f = fopen(CFG_PATH, "rb");
    if (!f) return;
    char buf[200] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (n) g.load_blob(std::string(buf, n));
}
static void save_cfg(const Game& g) {
    FILE* f = fopen(CFG_PATH, "wb");
    if (!f) return;
    std::string s = g.save_blob();
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}


static void blog(const char* f, ...) {                          // boot trace: /data/hairpin_boot.txt (last line = where it died)
    FILE* fp = fopen("/data/hairpin_boot.txt", "ab");
    if (!fp) return;
    va_list a; va_start(a, f); vfprintf(fp, f, a); va_end(a);
    fputc('\n', fp); fclose(fp);
}

struct NikShader { char* ident; unsigned char hash[16]; uint64_t len; unsigned char* code; };
extern "C" NikShader scePrecompiledShaderEntries[];
static bool fetchSh(const char* n, unsigned char*& p, uint64_t& l) {
    for (NikShader* e = scePrecompiledShaderEntries; e->ident; ++e)
        if (!strcmp(n, e->ident)) { p = e->code; l = e->len; return true; }
    return false;
}

static EGLDisplay gD = EGL_NO_DISPLAY; static EGLSurface gS = EGL_NO_SURFACE; static EGLContext gC = EGL_NO_CONTEXT;
static GLuint gTex = 0, gProg = 0;
static int gDepthBits = 0;
static GLint uMV = -1, uPR = -1, uTS = -1, uSam = -1, uOp = -1, aV = -1;

static bool glInit() {
    blog("glInit begin");
    int mret = 0;
    const char* rw = sceKernelGetFsSandboxRandomWord();
    std::string pre = std::string("/") + (rw ? rw : "") + "/common/lib/";
    int pg = (int)sceKernelLoadStartModule((pre + "libScePigletv2VSH.sprx").c_str(), 0, NULL, 0, NULL, &mret);
    if (pg < 0) { blog("piglet load fail 0x%x", pg); return false; }
    int pc = (int)sceKernelLoadStartModule((pre + "libScePrecompiledShaders.sprx").c_str(), 0, NULL, 0, NULL, &mret);
    if (pc < 0) { blog("shaders load fail 0x%x", pc); return false; }
    blog("modules loaded");
    OrbisPglConfig c; memset(&c, 0, sizeof(c)); c.size = sizeof(c);
    c.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60; c.processOrder = 1;
    c.systemSharedMemorySize = 250 * 1024 * 1024; c.videoSharedMemorySize = 512 * 1024 * 1024; c.maxMappedFlexibleMemory = 170 * 1024 * 1024;
    c.drawCommandBufferSize = 1 * 1024 * 1024; c.lcueResourceBufferSize = 1 * 1024 * 1024;
    c.dbgPosCmd_0x40 = OUT_W; c.dbgPosCmd_0x44 = OUT_H; c.dbgPosCmd_0x48 = 0; c.dbgPosCmd_0x4C = 0; c.unk_0x5C = 2;
    if (!scePigletSetConfigurationVSH(&c)) { blog("piglet config fail"); return false; }
    OrbisPglWindow w = {0, (khronos_uint32_t)OUT_W, (khronos_uint32_t)OUT_H};
    EGLint at[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
                   EGL_SAMPLE_BUFFERS, 0, EGL_SAMPLES, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE}; const EGLint wa[] = {EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE};
    EGLConfig cfg; EGLint nc = 0, maj = 0, mi = 0;
    gD = eglGetDisplay(EGL_DEFAULT_DISPLAY); if (gD == EGL_NO_DISPLAY) { blog("no display"); return false; }
    if (!eglInitialize(gD, &maj, &mi)) { blog("egl init fail 0x%x", eglGetError()); return false; }
    eglBindAPI(EGL_OPENGL_ES_API); eglSwapInterval(gD, 0);   // interval 1 blocks forever in Piglet (frozen picture); we pace frames ourselves
    const int dl[3] = {24, 16, 0};                              // ask for a depth buffer (the GPU draws the 3D scene); fall back to none
    for (int di = 0; di < 3 && gS == EGL_NO_SURFACE; di++) {
        at[9] = dl[di]; nc = 0;
        if (!eglChooseConfig(gD, at, &cfg, 1, &nc) || nc < 1) { blog("choose cfg fail depth %d", dl[di]); continue; }
        gS = eglCreateWindowSurface(gD, cfg, &w, wa);
        if (gS == EGL_NO_SURFACE) { blog("surface fail depth %d 0x%x", dl[di], eglGetError()); continue; }
        EGLint gotD = -1; eglGetConfigAttrib(gD, cfg, EGL_DEPTH_SIZE, &gotD); gDepthBits = gotD;
        blog("config chosen: requested depth %d got %d", dl[di], (int)gotD);
    }
    if (gS == EGL_NO_SURFACE) { blog("no usable surface"); return false; }
    gC = eglCreateContext(gD, cfg, EGL_NO_CONTEXT, ca); if (gC == EGL_NO_CONTEXT) { blog("ctx fail 0x%x", eglGetError()); return false; }
    if (!eglMakeCurrent(gD, gS, gS, gC)) { blog("makecurrent fail 0x%x", eglGetError()); return false; }
    blog("GL %s | %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE); sceSystemServiceHideSplashScreen();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glGenTextures(1, &gTex); glBindTexture(GL_TEXTURE_2D, gTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    unsigned char *vp, *fp; uint64_t vl, fl;
    if (!fetchSh("texmap/v_2.vert", vp, vl) || !fetchSh("texmap/f_2.frag", fp, fl)) { blog("shader entries missing"); return false; }
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER), vs = glCreateShader(GL_VERTEX_SHADER); gProg = glCreateProgram();
    glShaderBinary(1, &vs, 0, vp, vl); glShaderBinary(1, &fs, 0, fp, fl); glAttachShader(gProg, vs); glAttachShader(gProg, fs); glLinkProgram(gProg);
    if (glGetError()) { blog("shader link fail"); return false; }
    uMV = glGetUniformLocation(gProg, "u_modelViewMatrix"); uPR = glGetUniformLocation(gProg, "u_projectionMatrix"); uTS = glGetUniformLocation(gProg, "u_textureSpaceMatrix");
    uSam = glGetUniformLocation(gProg, "s_sampler"); uOp = glGetUniformLocation(gProg, "u_opacity"); aV = glGetAttribLocation(gProg, "a_vertex");
    if (uMV < 0 || uPR < 0 || uTS < 0 || uSam < 0 || uOp < 0 || aV < 0) { blog("uniform lookup fail"); return false; }
    blog("piglet ok");
    return true;
}


// ---- 3D scene on the GPU: Sony's precompiled "trilithium" textured shader. Colours are palette lookups (UV into a 1024x1024 colour cube).
static GLuint gpPg = 0, gpPal = 0, gpSky = 0; static GLint gpVP = -1, gpM = -1, gpSam = -1, gpaP = -1, gpaT = -1;
static bool gpuInit() {
    unsigned char *vp, *fp; uint64_t vl, fl;
    if (!fetchSh("trilithium/vpBasic.vert", vp, vl) || !fetchSh("trilithium/fpTexture.frag", fp, fl)) { blog("gpu: trilithium shaders missing"); return false; }
    GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER); gpPg = glCreateProgram();
    glShaderBinary(1, &vs, 0, vp, vl); glShaderBinary(1, &fs, 0, fp, fl); glAttachShader(gpPg, vs); glAttachShader(gpPg, fs); glLinkProgram(gpPg);
    GLint ok = 0, nu = 0; glGetProgramiv(gpPg, GL_LINK_STATUS, &ok); glGetProgramiv(gpPg, GL_ACTIVE_UNIFORMS, &nu);
    for (int i = 0; i < nu && i < 16; i++) { char nm[64]; GLsizei l = 0; GLint sz = 0; GLenum ty = 0; glGetActiveUniform(gpPg, i, 64, &l, &sz, &ty, nm); GLint loc = glGetUniformLocation(gpPg, nm); if (ty == 0x8B5E) gpSam = loc; }
    gpVP = glGetUniformLocation(gpPg, "uVPMatrix"); gpM = glGetUniformLocation(gpPg, "uMMatrix");
    gpaP = glGetAttribLocation(gpPg, "aPosition"); gpaT = glGetAttribLocation(gpPg, "aTextureCoord");
    blog("gpu: link ok=%d uniforms=%d VP%d M%d sampler%d aP%d aT%d", (int)ok, (int)nu, (int)gpVP, (int)gpM, (int)gpSam, (int)gpaP, (int)gpaT);
    if (gpVP < 0 || gpaP < 0 || gpaT < 0) return false;
    static std::vector<uint32_t> pal(1024 * 1024);
    for (int i = 0; i < 1024 * 1024; i++) { int r7 = (i >> 13) & 127, g7 = (i >> 6) & 127, b6 = i & 63; pal[i] = 0xFF000000u | ((uint32_t)(b6 * 255 / 63) << 16) | ((uint32_t)(g7 * 255 / 127) << 8) | (uint32_t)(r7 * 255 / 127); }
    glGenTextures(1, &gpPal); glBindTexture(GL_TEXTURE_2D, gpPal); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 1024, 0, GL_RGBA, GL_UNSIGNED_BYTE, pal.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &gpSky); glBindTexture(GL_TEXTURE_2D, gpSky);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    blog("gpu: init done err=0x%x depthBits=%d", (unsigned)glGetError(), gDepthBits);
    return true;
}
static void gpuDraw(const float* v, size_t nv, const float* vp) {
    static const float ID[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(gpVP, 1, GL_FALSE, vp); if (gpM >= 0) glUniformMatrix4fv(gpM, 1, GL_FALSE, ID);
    for (size_t o = 0; o < nv; o += 60000) { size_t c = std::min<size_t>(60000, nv - o); c -= c % 3; if (!c) break;
        glVertexAttribPointer(gpaP, 3, GL_FLOAT, GL_FALSE, 20, v + o * 5); glVertexAttribPointer(gpaT, 2, GL_FLOAT, GL_FALSE, 20, v + o * 5 + 3); glDrawArrays(GL_TRIANGLES, 0, (GLsizei)c); }
}
static void gpuScene(Renderer& r, int w, int h, int frame, int vx = 0, int vy = 0, int vw = OUT_W, int vh = OUT_H) {
    for (int i = 0; i < 8; i++) glDisableVertexAttribArray(i);
    glViewport(vx, OUT_H - vy - vh, vw, vh); glEnable(GL_SCISSOR_TEST); glScissor(vx, OUT_H - vy - vh, vw, vh); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDepthMask(GL_TRUE);
    glClearColor(0.05f, 0.06f, 0.1f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(gpPg); glActiveTexture(GL_TEXTURE0); if (gpSam >= 0) glUniform1i(gpSam, 0);
    glEnableVertexAttribArray(gpaP); glEnableVertexAttribArray(gpaT);
    // sky: a small vertical gradient texture (same colours the CPU path used) on a full-screen quad
    { const int SKN = 128; static uint8_t sk[SKN * 4 * 4];
      uint32_t st = r.sky_top(), sh = r.sky_horizon(), fg = r.fog_colour();
      float hy = r.horizon_y();
      for (int j = 0; j < SKN; j++) { float y = (j + 0.5f) / SKN * h; uint32_t c;
        if (y < hy) { float t = hy > 1 ? y / hy : 1.f; float k = std::min(1.f, t * t * 0.4f + t * 0.6f); int rr = ((st >> 16) & 255), gg = ((st >> 8) & 255), bb = (st & 255); rr += (int)((((sh >> 16) & 255) - rr) * k); gg += (int)((((sh >> 8) & 255) - gg) * k); bb += (int)(((sh & 255) - bb) * k); c = (rr << 16) | (gg << 8) | bb; }
        else c = fg;
        for (int i = 0; i < 4; i++) { uint8_t* q = &sk[(j * 4 + i) * 4]; q[0] = (c >> 16) & 255; q[1] = (c >> 8) & 255; q[2] = c & 255; q[3] = 255; } }
      glBindTexture(GL_TEXTURE_2D, gpSky); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, SKN, 0, GL_RGBA, GL_UNSIGNED_BYTE, sk);
      static const float ID[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      static const float Q[] = {-1, -1, 0, 0, 1, 1, -1, 0, 1, 1, 1, 1, 0, 1, 0, -1, -1, 0, 0, 1, 1, 1, 0, 1, 0, -1, 1, 0, 0, 0};
      glDisable(GL_DEPTH_TEST); gpuDraw(Q, 6, ID); }
    // triangles: layer 0 = world, layer 1 = cockpit/hands (depth cleared, tighter near/far so they never z-fight)
    if (gDepthBits > 0) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); } else glDisable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, gpPal);
    static std::vector<float> sorted;
    for (int layer = 0; layer < 2; layer++) {
        const std::vector<float>& gv = r.gpu_verts(layer); size_t nv = gv.size() / 5;
        if (!nv) continue;
        const float* src = gv.data();
        if (gDepthBits > 0) { if (layer == 1) glClear(GL_DEPTH_BUFFER_BIT); }
        else {                                                          // no depth buffer: painter's algorithm, far to near
            const std::vector<float>& gz = r.gpu_depth(layer); size_t nt = gz.size();
            static std::vector<uint32_t> ord; ord.resize(nt); for (size_t i = 0; i < nt; i++) ord[i] = (uint32_t)i;
            std::sort(ord.begin(), ord.end(), [&](uint32_t a, uint32_t b) { return gz[a] > gz[b]; });
            sorted.resize(gv.size()); for (size_t i = 0; i < nt; i++) memcpy(&sorted[i * 15], &gv[(size_t)ord[i] * 15], 15 * sizeof(float));
            src = sorted.data();
        }
        float f = r.focal(), n = layer ? 0.04f : 0.2f, fa = layer ? 40.f : 900.f; float P[16] = {0};
        P[0] = 2.f * f / w; P[5] = 2.f * f / h; P[10] = (fa + n) / (n - fa); P[11] = -1; P[14] = 2.f * fa * n / (n - fa);
        gpuDraw(src, nv, P);
        if (frame < 3 || frame % 600 == 0) blog("gpuScene %d layer %d tris %d err=0x%x", frame, layer, (int)(nv / 3), (unsigned)glGetError());
    }
    for (int i = 0; i < 8; i++) glDisableVertexAttribArray(i);
    glDisable(GL_SCISSOR_TEST);
}

// Upload the CPU frame (RGBA, w x h) as a texture and let the GPU draw it full-screen (bilinear upscale to 1080p).
static void glPresent(const uint32_t* px, int w, int h) {
    static int tw = 0, th = 0;
    glViewport(0, 0, OUT_W, OUT_H); glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // 2D layer is premultiplied ARGB
    glBindTexture(GL_TEXTURE_2D, gTex);
    if (w != tw || h != th) { glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); tw = w; th = h; }
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    static const GLfloat v[] = {0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0};
    GLfloat id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    GLfloat mv[16] = {(GLfloat)OUT_W, 0, 0, 0, 0, (GLfloat)OUT_H, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    GLfloat pr[16] = {2.f / OUT_W, 0, 0, 0, 0, -2.f / OUT_H, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1};
    glUseProgram(gProg); glUniform1f(uOp, 1.f); glUniformMatrix4fv(uMV, 1, GL_FALSE, mv); glUniformMatrix4fv(uPR, 1, GL_FALSE, pr); glUniformMatrix4fv(uTS, 1, GL_FALSE, id);
    glVertexAttribPointer(aV, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), v); glEnableVertexAttribArray(aV);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gTex); glUniform1i(uSam, 0); glDrawArrays(GL_TRIANGLES, 0, 6);
    static int pf = 0; if (++pf <= 3) blog("draw issued err=0x%x, calling swap", (unsigned)glGetError());
    EGLBoolean sr = eglSwapBuffers(gD, gS);
    if (pf <= 3) blog("swap ret=%d err=0x%x", (int)sr, (unsigned)eglGetError());
}

static uint32_t ticks() { return (uint32_t)(sceKernelGetProcessTime() / 1000); }

int main(int, char**) {
    { FILE* f = fopen("/data/hairpin_boot.txt", "wb"); if (f) fclose(f); }
    blog("== Hairpin Rush build %d ==", (int)BUILD_ID);
    if (!glInit()) { blog("glInit FAILED"); for (;;) sceKernelUsleep(100000); }
    blog("gl up");
    bool useGpu = gpuInit();
    blog("gpu scene %s", useGpu ? "ENABLED" : "failed - using CPU renderer");

    OrbisUserServiceInitializeParams up; up.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    int m1 = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    int m2 = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    blog("internal modules 0x%x 0x%x", (unsigned)m1, (unsigned)m2);
    int ui = sceUserServiceInitialize(&up);
    int userId = 0; int gi = sceUserServiceGetInitialUser(&userId);
    int pi = scePadInit();
    blog("userservice init 0x%x, initial user ret 0x%x uid %d, padInit 0x%x", (unsigned)ui, (unsigned)gi, userId, (unsigned)pi);
    // Try several user ids (the initial user, the system/everyone ids); poll every pad that opened and use whichever delivers data.
    int cand[6] = {userId, 0xFF, 0xFE, 1, 2, 0};
    int pads[6]; int np = 0;
    for (int i = 0; i < 6; i++) {
        bool dup = false; for (int j = 0; j < i; j++) if (cand[j] == cand[i]) dup = true;
        if (dup) continue;
        int h = scePadOpen(cand[i], 0, 0, NULL);
        blog("pad open uid %d -> %d", cand[i], h);
        if (h >= 0) pads[np++] = h;
    }

    // extra controllers for split screen: one per logged-in user (PS4 gives every controller to a user)
    int extraPads[3] = {-1, -1, -1};
    { OrbisUserServiceLoginUserIdList ul; memset(&ul, 0xFF, sizeof ul); int lr = sceUserServiceGetLoginUserIdList(&ul);
      int k = 0; blog("login users ret 0x%x: %d %d %d %d", (unsigned)lr, (int)ul.userId[0], (int)ul.userId[1], (int)ul.userId[2], (int)ul.userId[3]);
      for (int i = 0; i < 4 && k < 3; i++) { int id = (int)ul.userId[i]; if (id == -1 || id == userId) continue;
        int h = scePadOpen(id, 0, 0, NULL); blog("extra pad uid %d -> %d", id, h); if (h >= 0) extraPads[k++] = h; } }
    static Multi multi;
    Game& game = multi.lead;
    game.set_threads(1);
    game.renderer().gpu = useGpu;
    load_cfg(game);
    blog("game ok");
    static std::vector<uint32_t> fb, up_;
    uint32_t last = ticks();
    int frame = 0;
    for (;;) {
        PadState ps; ps.down = 0; ps.lx = ps.ly = ps.l2 = ps.r2 = 0;
        OrbisPadData d; memset(&d, 0, sizeof d);
        int rr = -1;
        for (int k = 0; k < np; k++) {                          // use the first pad that shows any activity
            OrbisPadData t; memset(&t, 0, sizeof t);
            int r = scePadReadState(pads[k], &t);
            bool active = t.buttons != 0 || abs((int)t.leftStick.x - 128) > 20 || abs((int)t.leftStick.y - 128) > 20 || abs((int)t.rightStick.x - 128) > 20 || t.analogButtons.l2 > 10 || t.analogButtons.r2 > 10;
            if (k == 0 || active) { d = t; rr = r; if (active) { static int lg = 0; if (lg++ < 3) blog("pad %d (handle %d) active", k, pads[k]); break; } }
        }
        { static uint32_t lastB = 0xFFFFFFFFu; static int n = 0;
          if ((d.buttons != lastB) && n < 30) { lastB = d.buttons; n++; blog("pad ret=0x%x btn=%05X LS %d,%d RS %d,%d L2 %d R2 %d", (unsigned)rr, (unsigned)d.buttons, d.leftStick.x, d.leftStick.y, d.rightStick.x, d.rightStick.y, d.analogButtons.l2, d.analogButtons.r2); } }
        if (np > 0) {
            uint32_t b = d.buttons, o = 0;
            if (b & ORBIS_PAD_BUTTON_CROSS) o |= B_CROSS;
            if (b & ORBIS_PAD_BUTTON_CIRCLE) o |= B_CIRCLE;
            if (b & ORBIS_PAD_BUTTON_SQUARE) o |= B_SQUARE;
            if (b & ORBIS_PAD_BUTTON_TRIANGLE) o |= B_TRIANGLE;
            if (b & ORBIS_PAD_BUTTON_L1) o |= B_L1;
            if (b & ORBIS_PAD_BUTTON_R1) o |= B_R1;
            if (b & ORBIS_PAD_BUTTON_L3) o |= B_L3;
            if (b & ORBIS_PAD_BUTTON_R3) o |= B_R3;
            if (b & ORBIS_PAD_BUTTON_OPTIONS) o |= B_OPTIONS;
            if (b & ORBIS_PAD_BUTTON_TOUCH_PAD) o |= B_TOUCHPAD;
            if (b & ORBIS_PAD_BUTTON_UP) o |= B_UP;
            if (b & ORBIS_PAD_BUTTON_DOWN) o |= B_DOWN;
            if (b & ORBIS_PAD_BUTTON_LEFT) o |= B_LEFT;
            if (b & ORBIS_PAD_BUTTON_RIGHT) o |= B_RIGHT;
            ps.down = o;
            ps.lx = axis_norm(d.leftStick.x);
            ps.ly = axis_norm(d.leftStick.y);
            ps.l2 = d.analogButtons.l2 / 255.0f;
            ps.r2 = d.analogButtons.r2 / 255.0f;
        }
        PadState mpads[4]; mpads[0] = ps;
        for (int k = 0; k < 3; k++) { PadState q; q.down = 0; q.lx = q.ly = q.l2 = q.r2 = 0;
            if (extraPads[k] >= 0) { OrbisPadData t; memset(&t, 0, sizeof t); scePadReadState(extraPads[k], &t); uint32_t b = t.buttons, o = 0;
                if (b & ORBIS_PAD_BUTTON_CROSS) o |= B_CROSS; if (b & ORBIS_PAD_BUTTON_CIRCLE) o |= B_CIRCLE; if (b & ORBIS_PAD_BUTTON_SQUARE) o |= B_SQUARE; if (b & ORBIS_PAD_BUTTON_TRIANGLE) o |= B_TRIANGLE;
                if (b & ORBIS_PAD_BUTTON_L1) o |= B_L1; if (b & ORBIS_PAD_BUTTON_R1) o |= B_R1; if (b & ORBIS_PAD_BUTTON_L3) o |= B_L3; if (b & ORBIS_PAD_BUTTON_R3) o |= B_R3;
                if (b & ORBIS_PAD_BUTTON_UP) o |= B_UP; if (b & ORBIS_PAD_BUTTON_DOWN) o |= B_DOWN; if (b & ORBIS_PAD_BUTTON_LEFT) o |= B_LEFT; if (b & ORBIS_PAD_BUTTON_RIGHT) o |= B_RIGHT;
                q.down = o; q.lx = axis_norm(t.leftStick.x); q.ly = axis_norm(t.leftStick.y); q.l2 = t.analogButtons.l2 / 255.0f; q.r2 = t.analogButtons.r2 / 255.0f; }
            mpads[k + 1] = q; }
        uint32_t now = ticks();
        float dt = fminf(0.05f, (now - last) / 1000.0f);
        last = now;
        uint64_t t0 = sceKernelGetProcessTime();
        multi.update(dt, mpads);
        if (game.take_dirty()) save_cfg(game);
        bool mp = multi.active();
        int w = mp ? OUT_W : game.render_w(), h = mp ? OUT_H : game.render_h();
        if ((int)fb.size() != w * h) { fb.assign(size_t(w) * h, 0); up_.assign(size_t(w) * h, 0); }
        uint64_t t1 = sceKernelGetProcessTime();
        static std::vector<std::vector<uint32_t>> mtmp;
        if (mp) multi.render(fb, mtmp); else game.render(fb.data());
        uint64_t t2 = sceKernelGetProcessTime();
        if (frame == 0) blog("first frame rendered %dx%d", w, h);
        for (size_t i = 0; i < fb.size(); i++) { uint32_t p = fb[i]; up_[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16) | (useGpu ? 0u : 0xFF000000u); }  // ARGB -> ABGR bytes (RGBA)
        uint64_t t3 = sceKernelGetProcessTime();
        if (useGpu) {
            if (mp) { glViewport(0, 0, OUT_W, OUT_H); glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                      for (int i = 0; i < multi.count; i++) gpuScene(multi.pane(i).renderer(), multi.rect[i].w, multi.rect[i].h, frame, multi.rect[i].x, multi.rect[i].y, multi.rect[i].w, multi.rect[i].h); }
            else gpuScene(game.renderer(), w, h, frame);
        } else { glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); }
        uint64_t t4 = sceKernelGetProcessTime();
        glPresent(up_.data(), w, h);
        uint64_t t5 = sceKernelGetProcessTime();
        { static double a[5] = {0, 0, 0, 0, 0}; static int n = 0;
          a[0] += (t1 - t0) / 1000.0; a[1] += (t2 - t1) / 1000.0; a[2] += (t3 - t2) / 1000.0; a[3] += (t4 - t3) / 1000.0; a[4] += (t5 - t4) / 1000.0;
          if (++n == 120) { blog("avg ms: update %.2f render(cpu 3D+2D) %.2f swizzle %.2f gpuScene %.2f present(upload+swap) %.2f  | tris %d", a[0] / n, a[1] / n, a[2] / n, a[3] / n, a[4] / n, (int)game.renderer().tri_count()); for (int i = 0; i < 5; i++) a[i] = 0; n = 0; } }
        if (frame < 3 || frame % 600 == 0) blog("frame %d presented dt=%.1fms", frame, dt * 1000.0f);
        frame++;
        { uint32_t el = ticks() - now; if (el < 16) sceKernelUsleep((16 - el) * 1000); }   // cap at ~60 fps
    }
    return 0;
}

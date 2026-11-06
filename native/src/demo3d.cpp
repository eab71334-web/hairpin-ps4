// host demo: renders the 3D world with real car meshes
#include "world3d.h"
#include <chrono>
#include <cstdio>
#include <string>
static void save(const char* p, int w, int h, const std::vector<uint32_t>& fb) {
    FILE* f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (uint32_t c : fb) { unsigned char o[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c}; fwrite(o, 1, 3, f); }
    fclose(f);
}
int main(int argc, char** argv) {
    int W = argc > 2 ? atoi(argv[2]) : 1280, H = W * 9 / 16;
    const char* out = argc > 1 ? argv[1] : "/tmp";
    Renderer r; r.resize(W, H); r.threads = argc > 3 ? atoi(argv[3]) : 2;
    World3D w;
    std::vector<uint32_t> fb(W * H);
    for (int st = 0; st < 3; st++) {
        w.load(st);
        for (int shot = 0; shot < 2; shot++) {
            float s = 300 + shot * 600;
            V3 cp = w.pos_at(s, 0); float h = w.heading_at(s);
            Xf cx = Xf::make(cp + V3(0, 0.0f, 0), h);
            // chase camera
            V3 fwd(-sinf(h), 0, -cosf(h));
            Camera cam; cam.pos = cp - fwd * 7.5f + V3(0, 3.3f, 0); cam.yaw = h; cam.pitch = -0.12f; cam.fov_deg = 62;
            const Theme& th = w.theme();
            auto t0 = std::chrono::steady_clock::now();
            r.begin(cam, th.sky_top, th.sky_hor, th.fog, 60, 420);
            w.draw(r, cam, s);
            r.mesh(car_mesh(8), cx, 0xFFFF6A00u, false, 1000);
            V3 p2 = w.pos_at(s + 22, -3.0f);
            r.mesh(car_mesh(3), Xf::make(p2, w.heading_at(s + 22)), 0xFF2060E0u, false, 1000);
            r.end(fb.data());
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            printf("stage %d shot %d: %zu tris %.1f ms\n", st, shot, r.tri_count(), ms);
            char p[256]; snprintf(p, sizeof p, "%s/w%d_%d.ppm", out, st, shot); save(p, W, H, fb);
        }
    }
    // cockpit
    w.load(1);
    float s = 400; V3 cp = w.pos_at(s, 0); float h = w.heading_at(s);
    Xf cx = Xf::make(cp, h);
    Camera cam; cam.pos = cx.apply(V3(-0.45f, 1.55f, 0.1f)); cam.yaw = h; cam.pitch = -0.03f; cam.fov_deg = 75;
    r.begin(cam, w.theme().sky_top, w.theme().sky_hor, w.theme().fog, 60, 420);
    w.draw(r, cam, s);
    r.mesh(car_mesh(1), cx, 0xFFD02030u, true, 1000);
    r.end(fb.data());
    char p[256]; snprintf(p, sizeof p, "%s/cockpit.ppm", out); save(p, W, H, fb);
    return 0;
}

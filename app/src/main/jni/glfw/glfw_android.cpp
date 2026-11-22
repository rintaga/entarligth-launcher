// libglfw.so - backend GLFW 3.4 untuk Android (ANativeWindow + EGL).
// Dipakai LWJGL resmi lewat -Dorg.lwjgl.glfw.libname. Input datang dari controller virtual
// lewat lumina_glfw_push_event (dipanggil libluminacore.so).
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <deque>
#include <mutex>
#include <string>

#define API extern "C" __attribute__((visibility("default")))
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "LuminaGLFW", __VA_ARGS__)

#define CURSOR_MODE      0x00033001
#define CURSOR_NORMAL    0x00034001
#define CURSOR_DISABLED  0x00034003
#define ERR_PLATFORM     0x00010008
#define PLATFORM_NULL    0x00060005

struct VidMode   { int width, height, redBits, greenBits, blueBits, refreshRate; };
struct GammaRamp { unsigned short *red, *green, *blue; unsigned int size; };
struct Image     { int width, height; unsigned char *pixels; };
struct PadState  { unsigned char buttons[15]; float axes[6]; };

struct Win {
    int w = 854, h = 480;
    bool shouldClose = false;
    void *user = nullptr;
    EGLSurface surf = EGL_NO_SURFACE;
    void *cbKey = nullptr, *cbChar = nullptr, *cbCharMods = nullptr, *cbMouse = nullptr,
         *cbCursor = nullptr, *cbEnter = nullptr, *cbScroll = nullptr, *cbDrop = nullptr,
         *cbPos = nullptr, *cbSize = nullptr, *cbClose = nullptr, *cbRefresh = nullptr,
         *cbFocus = nullptr, *cbIconify = nullptr, *cbMaximize = nullptr, *cbFb = nullptr,
         *cbScale = nullptr;
    double cx = 0, cy = 0;
    int inputModes[8] = {CURSOR_NORMAL, 0, 0, 0, 0, 0, 0, 0};
    int cursorMode = CURSOR_NORMAL;
    bool keys[512] = {};
    bool btns[8] = {};
    std::string title;
};

struct Ev { int type, a, b; float x, y; };
enum { EV_KEY = 1, EV_MBTN = 2, EV_MMOVE = 3, EV_SCROLL = 4 };

static std::mutex g_mu;
static std::deque<Ev> g_q;
static ANativeWindow *g_nw = nullptr, *g_old = nullptr;
static int g_nwW = 0, g_nwH = 0;
static bool g_dirty = false, g_resized = false;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLConfig g_cfg = nullptr;
static Win *g_win = nullptr, *g_cur = nullptr;
static std::string g_clip;
static int g_err = 0;
static void *g_errCb = nullptr;
static int g_monitorDummy = 0, g_cursorDummy = 0;
static double g_t0 = 0;
static VidMode g_vm = {854, 480, 8, 8, 8, 60};
static bool g_hasPad = false;

static double nowSec() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

// ---------- dipanggil dari libluminacore.so ----------

API void lumina_glfw_push_event(int type, int a, int b, float x, float y) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_q.size() >= 4096) g_q.pop_front();
    g_q.push_back({type, a, b, x, y});
}

API void lumina_glfw_set_window(ANativeWindow *nw, int w, int h) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_nw != nw) {
        if (g_nw) { if (g_old) ANativeWindow_release(g_old); g_old = g_nw; }
        g_nw = nw;
        g_dirty = true;
    }
    if (w > 0 && h > 0 && (w != g_nwW || h != g_nwH)) {
        g_nwW = w; g_nwH = h;
        g_vm.width = w; g_vm.height = h;
        if (g_win) { g_win->w = w; g_win->h = h; g_resized = true; }
    }
}

// ---------- EGL ----------

static bool initEgl() {
    if (g_ctx != EGL_NO_CONTEXT) return true;
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, nullptr, nullptr)) return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint versions[] = {3, 2};   // NG-GL4ES butuh ES 3.x; fallback ES 2
    const EGLint depths[] = {24, 16};
    for (EGLint ver : versions) {
        for (EGLint d : depths) {
            const EGLint ca[] = {EGL_RENDERABLE_TYPE, ver == 3 ? 0x40 /*ES3_BIT*/ : EGL_OPENGL_ES2_BIT,
                                 EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                 EGL_DEPTH_SIZE, d, EGL_STENCIL_SIZE, 8, EGL_NONE};
            EGLint n = 0;
            if (!eglChooseConfig(g_dpy, ca, &g_cfg, 1, &n) || n < 1) continue;
            const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, ver, EGL_NONE};
            g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, xa);
            if (g_ctx != EGL_NO_CONTEXT) { LOGI("EGL ES%d depth=%d ctx=%p", ver, d, g_ctx); return true; }
        }
    }
    return false;
}

// Dipanggil dari thread render. Membuat/menghancurkan EGLSurface sesuai ANativeWindow terbaru.
static void ensureSurface() {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_win || g_dpy == EGL_NO_DISPLAY) return;
    if (g_dirty) {
        if (g_win->surf != EGL_NO_SURFACE) {
            eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(g_dpy, g_win->surf);
            g_win->surf = EGL_NO_SURFACE;
        }
        if (g_old) { ANativeWindow_release(g_old); g_old = nullptr; }
        g_dirty = false;
    }
    if (g_win->surf == EGL_NO_SURFACE && g_nw) {
        EGLint fmt = 0;
        eglGetConfigAttrib(g_dpy, g_cfg, EGL_NATIVE_VISUAL_ID, &fmt);
        ANativeWindow_setBuffersGeometry(g_nw, 0, 0, fmt);
        g_win->surf = eglCreateWindowSurface(g_dpy, g_cfg, g_nw, nullptr);
        if (g_cur == g_win && g_win->surf != EGL_NO_SURFACE)
            eglMakeCurrent(g_dpy, g_win->surf, g_win->surf, g_ctx);
    }
}

// ---------- init / error / versi ----------

API int glfwInit(void) { g_t0 = nowSec(); return 1; }
API void glfwTerminate(void) {}
API void glfwInitHint(int, int) {}
API void glfwInitAllocator(const void *) {}
API void glfwInitVulkanLoader(void *) {}
API void glfwGetVersion(int *a, int *b, int *c) { if (a) *a = 3; if (b) *b = 4; if (c) *c = 0; }
API const char *glfwGetVersionString(void) { return "3.4.0 Android (Lumina)"; }
API int glfwGetError(const char **d) { if (d) *d = nullptr; int e = g_err; g_err = 0; return e; }
API void *glfwSetErrorCallback(void *cb) { void *o = g_errCb; g_errCb = cb; return o; }
API int glfwGetPlatform(void) { return PLATFORM_NULL; }
API int glfwPlatformSupported(int p) { return p == PLATFORM_NULL; }

// ---------- monitor ----------

static void *g_monArr[1] = {&g_monitorDummy};
API void **glfwGetMonitors(int *n) { if (n) *n = 1; return g_monArr; }
API void *glfwGetPrimaryMonitor(void) { return &g_monitorDummy; }
API void glfwGetMonitorPos(void *, int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
API void glfwGetMonitorWorkarea(void *, int *x, int *y, int *w, int *h) {
    if (x) *x = 0; if (y) *y = 0; if (w) *w = g_vm.width; if (h) *h = g_vm.height;
}
API void glfwGetMonitorPhysicalSize(void *, int *w, int *h) {
    if (w) *w = (int) (g_vm.width * 25.4 / 160); if (h) *h = (int) (g_vm.height * 25.4 / 160);
}
API void glfwGetMonitorContentScale(void *, float *x, float *y) { if (x) *x = 1.f; if (y) *y = 1.f; }
API const char *glfwGetMonitorName(void *) { return "Android"; }
API void glfwSetMonitorUserPointer(void *, void *) {}
API void *glfwGetMonitorUserPointer(void *) { return nullptr; }
API void *glfwSetMonitorCallback(void *) { return nullptr; }
API const VidMode *glfwGetVideoModes(void *, int *n) { if (n) *n = 1; return &g_vm; }
API const VidMode *glfwGetVideoMode(void *) { return &g_vm; }
API void glfwSetGamma(void *, float) {}
API const GammaRamp *glfwGetGammaRamp(void *) { return nullptr; }
API void glfwSetGammaRamp(void *, const GammaRamp *) {}

// ---------- jendela ----------

API void glfwDefaultWindowHints(void) {}
API void glfwWindowHint(int, int) {}
API void glfwWindowHintString(int, const char *) {}

API void *glfwCreateWindow(int w, int h, const char *title, void *, void *) {
    for (int i = 0; i < 600; i++) {   // tunggu surface dari Activity (maks 30 detik)
        { std::lock_guard<std::mutex> l(g_mu); if (g_nw) break; }
        usleep(50000);
    }
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (!g_nw || !initEgl()) { g_err = ERR_PLATFORM; return nullptr; }
        Win *x = new Win();
        x->w = g_nwW > 0 ? g_nwW : w;
        x->h = g_nwH > 0 ? g_nwH : h;
        x->title = title ? title : "";
        x->cx = x->w / 2.0; x->cy = x->h / 2.0;
        g_win = x;
        g_dirty = true;
    }
    ensureSurface();
    LOGI("window %dx%d", g_win->w, g_win->h);
    return g_win;
}

API void glfwDestroyWindow(void *window) {
    Win *x = (Win *) window;
    if (!x) return;
    std::lock_guard<std::mutex> l(g_mu);
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (x->surf != EGL_NO_SURFACE) eglDestroySurface(g_dpy, x->surf);
    if (g_win == x) g_win = nullptr;
    if (g_cur == x) g_cur = nullptr;
    delete x;
}

API int glfwWindowShouldClose(void *w) { return w && ((Win *) w)->shouldClose; }
API void glfwSetWindowShouldClose(void *w, int v) { if (w) ((Win *) w)->shouldClose = v != 0; }
API const char *glfwGetWindowTitle(void *w) { return w ? ((Win *) w)->title.c_str() : ""; }
API void glfwSetWindowTitle(void *w, const char *t) { if (w) ((Win *) w)->title = t ? t : ""; }
API void glfwSetWindowIcon(void *, int, const Image *) {}
API void glfwGetWindowPos(void *, int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
API void glfwSetWindowPos(void *, int, int) {}
API void glfwGetWindowSize(void *w, int *a, int *b) {
    Win *x = (Win *) w; if (a) *a = x ? x->w : 0; if (b) *b = x ? x->h : 0;
}
API void glfwSetWindowSizeLimits(void *, int, int, int, int) {}
API void glfwSetWindowAspectRatio(void *, int, int) {}
API void glfwSetWindowSize(void *, int, int) {}
API void glfwGetFramebufferSize(void *w, int *a, int *b) {
    Win *x = (Win *) w; if (a) *a = x ? x->w : 0; if (b) *b = x ? x->h : 0;
}
API void glfwGetWindowFrameSize(void *, int *l, int *t, int *r, int *b) {
    if (l) *l = 0; if (t) *t = 0; if (r) *r = 0; if (b) *b = 0;
}
API void glfwGetWindowContentScale(void *, float *x, float *y) { if (x) *x = 1.f; if (y) *y = 1.f; }
API float glfwGetWindowOpacity(void *) { return 1.f; }
API void glfwSetWindowOpacity(void *, float) {}
API void glfwIconifyWindow(void *) {}
API void glfwRestoreWindow(void *) {}
API void glfwMaximizeWindow(void *) {}
API void glfwShowWindow(void *) {}
API void glfwHideWindow(void *) {}
API void glfwFocusWindow(void *) {}
API void glfwRequestWindowAttention(void *) {}
API void *glfwGetWindowMonitor(void *) { return nullptr; }
API void glfwSetWindowMonitor(void *, void *, int, int, int, int, int) {}
API void glfwSetWindowUserPointer(void *w, void *p) { if (w) ((Win *) w)->user = p; }
API void *glfwGetWindowUserPointer(void *w) { return w ? ((Win *) w)->user : nullptr; }

API int glfwGetWindowAttrib(void *, int a) {
    switch (a) {
        case 0x20001: return 1;        // FOCUSED
        case 0x20003: return 1;        // RESIZABLE
        case 0x20004: return 1;        // VISIBLE
        case 0x20006: return 1;        // AUTO_ICONIFY
        case 0x2000B: return 1;        // HOVERED
        case 0x22001: return 0x30001;  // CLIENT_API = OPENGL_API
        case 0x22002: return 3;        // CONTEXT_VERSION_MAJOR
        case 0x22003: return 2;        // CONTEXT_VERSION_MINOR
        case 0x22006: return 1;        // OPENGL_FORWARD_COMPAT
        case 0x22008: return 0x32001;  // OPENGL_PROFILE = CORE
        default: return 0;
    }
}
API void glfwSetWindowAttrib(void *, int, int) {}

#define SETCB(name, field) \
    API void *name(void *w, void *cb) { Win *x = (Win *) w; if (!x) return nullptr; void *o = x->field; x->field = cb; return o; }
SETCB(glfwSetWindowPosCallback, cbPos)
SETCB(glfwSetWindowSizeCallback, cbSize)
SETCB(glfwSetWindowCloseCallback, cbClose)
SETCB(glfwSetWindowRefreshCallback, cbRefresh)
SETCB(glfwSetWindowFocusCallback, cbFocus)
SETCB(glfwSetWindowIconifyCallback, cbIconify)
SETCB(glfwSetWindowMaximizeCallback, cbMaximize)
SETCB(glfwSetFramebufferSizeCallback, cbFb)
SETCB(glfwSetWindowContentScaleCallback, cbScale)
SETCB(glfwSetKeyCallback, cbKey)
SETCB(glfwSetCharCallback, cbChar)
SETCB(glfwSetCharModsCallback, cbCharMods)
SETCB(glfwSetMouseButtonCallback, cbMouse)
SETCB(glfwSetCursorPosCallback, cbCursor)
SETCB(glfwSetCursorEnterCallback, cbEnter)
SETCB(glfwSetScrollCallback, cbScroll)
SETCB(glfwSetDropCallback, cbDrop)

// ---------- event ----------

static void dispatch(Win *w, const Ev &e) {
    switch (e.type) {
        case EV_KEY: {
            if (e.a < 0 || e.a >= 512) break;
            w->keys[e.a] = e.b != 0;
            if (w->cbKey) ((void (*)(void *, int, int, int, int)) w->cbKey)(w, e.a, e.a, e.b ? 1 : 0, 0);
            break;
        }
        case EV_MBTN: {
            if (e.a < 0 || e.a >= 8) break;
            w->btns[e.a] = e.b != 0;
            if (w->cbMouse) ((void (*)(void *, int, int, int)) w->cbMouse)(w, e.a, e.b ? 1 : 0, 0);
            break;
        }
        case EV_MMOVE: {
            w->cx += e.x; w->cy += e.y;
            if (w->cursorMode != CURSOR_DISABLED) {
                if (w->cx < 0) w->cx = 0; if (w->cy < 0) w->cy = 0;
                if (w->cx > w->w) w->cx = w->w; if (w->cy > w->h) w->cy = w->h;
            }
            if (w->cbCursor) ((void (*)(void *, double, double)) w->cbCursor)(w, w->cx, w->cy);
            break;
        }
        case EV_SCROLL: {
            // controller: R1 = +1 (slot kanan) -> GLFW scroll turun (-1)
            if (w->cbScroll) ((void (*)(void *, double, double)) w->cbScroll)(w, 0.0, -(double) e.y);
            break;
        }
    }
}

API void glfwPollEvents(void) {
    ensureSurface();
    std::deque<Ev> evs;
    bool resized;
    Win *w;
    {
        std::lock_guard<std::mutex> l(g_mu);
        evs.swap(g_q);
        resized = g_resized; g_resized = false;
        w = g_win;
    }
    if (!w) return;
    if (resized) {
        if (w->cbSize) ((void (*)(void *, int, int)) w->cbSize)(w, w->w, w->h);
        if (w->cbFb) ((void (*)(void *, int, int)) w->cbFb)(w, w->w, w->h);
    }
    for (auto &e : evs) dispatch(w, e);
}
API void glfwWaitEvents(void) { usleep(5000); glfwPollEvents(); }
API void glfwWaitEventsTimeout(double t) { usleep((useconds_t) ((t < 0.01 ? t : 0.01) * 1e6)); glfwPollEvents(); }
API void glfwPostEmptyEvent(void) {}

// ---------- input ----------

API int glfwGetInputMode(void *w, int mode) {
    Win *x = (Win *) w; if (!x) return 0;
    if (mode == CURSOR_MODE) return x->cursorMode;
    return (mode >= 0x33001 && mode < 0x33001 + 8) ? x->inputModes[mode - 0x33001] : 0;
}
API void glfwSetInputMode(void *w, int mode, int value) {
    Win *x = (Win *) w; if (!x) return;
    if (mode == CURSOR_MODE) x->cursorMode = value;
    if (mode >= 0x33001 && mode < 0x33001 + 8) x->inputModes[mode - 0x33001] = value;
}
API int glfwRawMouseMotionSupported(void) { return 1; }
API const char *glfwGetKeyName(int, int) { return nullptr; }
API int glfwGetKeyScancode(int key) { return key; }
API int glfwGetKey(void *w, int k) { Win *x = (Win *) w; return (x && k >= 0 && k < 512 && x->keys[k]) ? 1 : 0; }
API int glfwGetMouseButton(void *w, int b) { Win *x = (Win *) w; return (x && b >= 0 && b < 8 && x->btns[b]) ? 1 : 0; }
API void glfwGetCursorPos(void *w, double *a, double *b) {
    Win *x = (Win *) w; if (a) *a = x ? x->cx : 0; if (b) *b = x ? x->cy : 0;
}
API void glfwSetCursorPos(void *w, double a, double b) { Win *x = (Win *) w; if (x) { x->cx = a; x->cy = b; } }
API void *glfwCreateCursor(const Image *, int, int) { return &g_cursorDummy; }
API void *glfwCreateStandardCursor(int) { return &g_cursorDummy; }
API void glfwDestroyCursor(void *) {}
API void glfwSetCursor(void *, void *) {}

API int glfwJoystickPresent(int) { return 0; }
API const float *glfwGetJoystickAxes(int, int *n) { if (n) *n = 0; return nullptr; }
API const unsigned char *glfwGetJoystickButtons(int, int *n) { if (n) *n = 0; return nullptr; }
API const unsigned char *glfwGetJoystickHats(int, int *n) { if (n) *n = 0; return nullptr; }
API const char *glfwGetJoystickName(int) { return nullptr; }
API const char *glfwGetJoystickGUID(int) { return nullptr; }
API void glfwSetJoystickUserPointer(int, void *) {}
API void *glfwGetJoystickUserPointer(int) { return nullptr; }
API int glfwJoystickIsGamepad(int) { return 0; }
API void *glfwSetJoystickCallback(void *) { return nullptr; }
API int glfwUpdateGamepadMappings(const char *) { return 1; }
API const char *glfwGetGamepadName(int) { return nullptr; }
API int glfwGetGamepadState(int, PadState *) { return 0; }

API void glfwSetClipboardString(void *, const char *s) { g_clip = s ? s : ""; }
API const char *glfwGetClipboardString(void *) { return g_clip.c_str(); }

// ---------- waktu ----------

API double glfwGetTime(void) { return nowSec() - g_t0; }
API void glfwSetTime(double t) { g_t0 = nowSec() - t; }
API uint64_t glfwGetTimerValue(void) {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000000000ull + (uint64_t) t.tv_nsec;
}
API uint64_t glfwGetTimerFrequency(void) { return 1000000000ull; }

// ---------- konteks ----------

API void glfwMakeContextCurrent(void *window) {
    Win *x = (Win *) window;
    if (!x) {
        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        g_cur = nullptr;
        return;
    }
    g_cur = x;
    ensureSurface();
    if (x->surf != EGL_NO_SURFACE) eglMakeCurrent(g_dpy, x->surf, x->surf, g_ctx);
}
API void *glfwGetCurrentContext(void) { return g_cur; }
API void glfwSwapBuffers(void *window) {
    Win *x = (Win *) window;
    if (!x) return;
    ensureSurface();
    if (x->surf != EGL_NO_SURFACE) eglSwapBuffers(g_dpy, x->surf);
}
API void glfwSwapInterval(int i) { if (g_dpy != EGL_NO_DISPLAY) eglSwapInterval(g_dpy, i); }
API int glfwExtensionSupported(const char *) { return 0; }
API void *glfwGetProcAddress(const char *n) {
    void *p = (void *) eglGetProcAddress(n);
    return p ? p : dlsym(RTLD_DEFAULT, n);
}

// ---------- Vulkan (tidak didukung) ----------

API int glfwVulkanSupported(void) { return 0; }
API const char **glfwGetRequiredInstanceExtensions(uint32_t *n) { if (n) *n = 0; return nullptr; }
API void *glfwGetInstanceProcAddress(void *, const char *) { return nullptr; }
API int glfwGetPhysicalDevicePresentationSupport(void *, void *, uint32_t) { return 0; }
API int glfwCreateWindowSurface(void *, void *, const void *, void *) { return -7; }

// ---------- native EGL (GLFWNativeEGL) ----------

API void *glfwGetEGLDisplay(void) { return g_dpy; }
API void *glfwGetEGLContext(void *) { return g_ctx; }
API void *glfwGetEGLSurface(void *w) { return w ? ((Win *) w)->surf : EGL_NO_SURFACE; }

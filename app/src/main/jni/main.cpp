// libluminacore.so - JNI loader untuk Entarligth Launcher
// 1) Memuat runtime OpenJDK (libjli.so) dari files/.plugin/jre lalu memanggil JLI_Launch.
// 2) Meneruskan input controller virtual dan Surface ke libglfw.so.
#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#define TAG  "LuminaCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef int (*JLI_Launch_t)(int argc, char **argv,
                            int jargc, const char **jargv,
                            int appclassc, const char **appclassv,
                            const char *fullversion, const char *dotversion,
                            const char *pname, const char *lname,
                            jboolean javaargs, jboolean cpwildcard,
                            jboolean javaw, jint ergo);

typedef void (*PushEv_t)(int, int, int, float, float);
typedef void (*SetWin_t)(ANativeWindow *, int, int);

enum { EV_KEY = 1, EV_MBTN = 2, EV_MMOVE = 3, EV_SCROLL = 4 };

static std::mutex g_mu;
static PushEv_t g_push = nullptr;
static SetWin_t g_setWin = nullptr;
static ANativeWindow *g_pendingWin = nullptr;
static int g_pw = 0, g_ph = 0;

static std::string toStr(JNIEnv *env, jstring s) {
    if (!s) return "";
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(s, c);
    return r;
}

static void preload(const std::string &path) {
    if (access(path.c_str(), R_OK) != 0) return;
    if (!dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL))
        LOGE("preload gagal %s: %s", path.c_str(), dlerror());
}

static void loadGlfw() {
    const char *dir = getenv("LUMINA_LIBDIR");
    if (!dir) return;
    std::string p = std::string(dir) + "/libglfw.so";
    void *h = dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) { LOGE("libglfw.so: %s", dlerror()); return; }
    std::lock_guard<std::mutex> l(g_mu);
    g_push = (PushEv_t) dlsym(h, "lumina_glfw_push_event");
    g_setWin = (SetWin_t) dlsym(h, "lumina_glfw_set_window");
    if (g_pendingWin && g_setWin) g_setWin(g_pendingWin, g_pw, g_ph);
    LOGI("libglfw dimuat push=%p setWin=%p", (void *) g_push, (void *) g_setWin);
}

// NG-GL4ES butuh tahu cara mencari fungsi GLES/EGL: beri eglGetProcAddress (kalau simbolnya ada).
static void setupGl4es() {
    const char *p = getenv("LUMINA_GL_LIB");
    if (!p) return;
    void *h = dlopen(p, RTLD_NOW | RTLD_LOCAL);
    if (!h) { LOGE("gl4es: %s", dlerror()); return; }
    typedef void (*SetProc_t)(void *(*)(const char *));
    auto setProc = (SetProc_t) dlsym(h, "set_getprocaddress");
    if (setProc) setProc((void *(*)(const char *)) eglGetProcAddress);
    LOGI("gl4es dimuat %s set_getprocaddress=%p", p, (void *) setProc);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_entarligth_launcher_NativeBridge_launch(JNIEnv *env, jclass,
                                                 jstring jHome, jstring jWork,
                                                 jstring jLog, jobjectArray jArgs) {
    std::string home = toStr(env, jHome);
    std::string work = toStr(env, jWork);
    std::string logp = toStr(env, jLog);

    int fd = open(logp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }

    setenv("JAVA_HOME", home.c_str(), 1);
    setenv("HOME", work.c_str(), 1);
    std::string ld = home + "/lib:" + home + "/lib/jli:" + home + "/lib/server:" + home + "/lib/client";
    if (const char *lib = getenv("LUMINA_LIBDIR")) ld += std::string(":") + lib;
    if (const char *old = getenv("LD_LIBRARY_PATH")) ld += std::string(":") + old;
    setenv("LD_LIBRARY_PATH", ld.c_str(), 1);

    // pengaturan GL4ES (OpenGL -> GLES2)
    setenv("LIBGL_ES", "3", 1);
    setenv("LIBGL_GL", "32", 1);
    setenv("LIBGL_NOERROR", "1", 1);
    setenv("LIBGL_MIPMAP", "3", 1);
    setenv("LIBGL_NORMALIZE", "1", 1);
    chdir(work.c_str());

    loadGlfw();
    setupGl4es();

    preload(home + "/lib/server/libjvm.so");
    preload(home + "/lib/client/libjvm.so");
    preload(home + "/lib/libverify.so");

    void *jli = dlopen((home + "/lib/libjli.so").c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!jli) { LOGE("libjli.so: %s", dlerror()); return -1; }
    auto JLI_Launch = (JLI_Launch_t) dlsym(jli, "JLI_Launch");
    if (!JLI_Launch) { LOGE("JLI_Launch tidak ditemukan"); return -2; }

    std::vector<std::string> store;
    store.push_back(home + "/bin/java");
    jsize n = env->GetArrayLength(jArgs);
    for (jsize i = 0; i < n; i++)
        store.push_back(toStr(env, (jstring) env->GetObjectArrayElement(jArgs, i)));

    std::vector<char *> argv;
    for (auto &s : store) argv.push_back((char *) s.c_str());
    argv.push_back(nullptr);

    LOGI("JLI_Launch argc=%d", (int) store.size());
    int rc = JLI_Launch((int) store.size(), argv.data(), 0, nullptr, 0, nullptr,
                        "", "", argv[0], argv[0], JNI_FALSE, JNI_TRUE, JNI_FALSE, 0);
    LOGI("JLI_Launch selesai rc=%d", rc);
    return rc;
}

// ---------------- Surface dari Activity ----------------

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_setSurface(JNIEnv *env, jclass, jobject surface, jint w, jint h) {
    ANativeWindow *nw = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    std::lock_guard<std::mutex> l(g_mu);
    if (g_setWin) {
        g_pendingWin = nw;           // libglfw memegang dan melepas referensi lamanya sendiri
        g_pw = w; g_ph = h;
        g_setWin(nw, w, h);
    } else {
        if (g_pendingWin) ANativeWindow_release(g_pendingWin);
        g_pendingWin = nw; g_pw = w; g_ph = h;
    }
}

// ---------------- Input controller virtual ----------------

static void sendEv(int type, int a, int b, float x, float y) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_push) g_push(type, a, b, x, y);
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendKey(JNIEnv *, jclass, jint key, jboolean down) {
    sendEv(EV_KEY, key, down ? 1 : 0, 0.f, 0.f);
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendMouseButton(JNIEnv *, jclass, jint btn, jboolean down) {
    sendEv(EV_MBTN, btn, down ? 1 : 0, 0.f, 0.f);
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendMouseMove(JNIEnv *, jclass, jfloat dx, jfloat dy) {
    sendEv(EV_MMOVE, 0, 0, dx, dy);
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendScroll(JNIEnv *, jclass, jfloat dy) {
    sendEv(EV_SCROLL, 0, 0, 0.f, dy);
}

// State gamepad belum dipakai (Minecraft Java vanilla tidak membaca gamepad).
extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendGamepad(JNIEnv *, jclass, jfloatArray, jint) {}

// libluminacore.so - JNI loader untuk Entarligth Launcher
// Memuat runtime OpenJDK (libjli.so) dari files/.plugin/jre lalu memanggil JLI_Launch.
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <android/log.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include <mutex>

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

extern "C" JNIEXPORT jint JNICALL
Java_com_entarligth_launcher_NativeBridge_launch(JNIEnv *env, jclass,
                                                 jstring jHome, jstring jWork,
                                                 jstring jLog, jobjectArray jArgs) {
    std::string home = toStr(env, jHome);
    std::string work = toStr(env, jWork);
    std::string logp = toStr(env, jLog);

    // stdout/stderr JVM -> file log
    int fd = open(logp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }

    setenv("JAVA_HOME", home.c_str(), 1);
    setenv("HOME", work.c_str(), 1);
    std::string ld = home + "/lib:" + home + "/lib/jli:" + home + "/lib/server:" + home + "/lib/client";
    if (const char *old = getenv("LD_LIBRARY_PATH")) ld += std::string(":") + old;
    setenv("LD_LIBRARY_PATH", ld.c_str(), 1);
    chdir(work.c_str());

    // urutan muat eksplisit (LD_LIBRARY_PATH runtime tidak selalu dibaca bionic)
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


// ---------------- Input controller virtual ----------------
// Antrean event untuk GLFW shim (dibaca lewat lumina_poll_event / lumina_get_gamepad).
struct LuminaInputEvent { int type; int a; int b; float x; float y; };
enum { LEV_KEY = 1, LEV_MOUSE_BTN = 2, LEV_MOUSE_MOVE = 3, LEV_SCROLL = 4 };

static std::mutex g_inMu;
static std::deque<LuminaInputEvent> g_inQ;
static float g_padAxes[6] = {0, 0, 0, 0, -1, -1};
static int g_padButtons = 0;

static void pushEv(const LuminaInputEvent &e) {
    std::lock_guard<std::mutex> l(g_inMu);
    if (g_inQ.size() >= 4096) g_inQ.pop_front();
    g_inQ.push_back(e);
}

extern "C" bool lumina_poll_event(LuminaInputEvent *out) {
    std::lock_guard<std::mutex> l(g_inMu);
    if (g_inQ.empty()) return false;
    *out = g_inQ.front();
    g_inQ.pop_front();
    return true;
}

extern "C" void lumina_get_gamepad(float *axes6, int *buttons) {
    std::lock_guard<std::mutex> l(g_inMu);
    for (int i = 0; i < 6; i++) axes6[i] = g_padAxes[i];
    *buttons = g_padButtons;
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendKey(JNIEnv *, jclass, jint key, jboolean down) {
    pushEv({LEV_KEY, key, down ? 1 : 0, 0.f, 0.f});
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendMouseButton(JNIEnv *, jclass, jint btn, jboolean down) {
    pushEv({LEV_MOUSE_BTN, btn, down ? 1 : 0, 0.f, 0.f});
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendMouseMove(JNIEnv *, jclass, jfloat dx, jfloat dy) {
    pushEv({LEV_MOUSE_MOVE, 0, 0, dx, dy});
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendScroll(JNIEnv *, jclass, jfloat dy) {
    pushEv({LEV_SCROLL, 0, 0, 0.f, dy});
}

extern "C" JNIEXPORT void JNICALL
Java_com_entarligth_launcher_NativeBridge_sendGamepad(JNIEnv *env, jclass, jfloatArray axes, jint buttons) {
    float tmp[6];
    env->GetFloatArrayRegion(axes, 0, 6, tmp);
    std::lock_guard<std::mutex> l(g_inMu);
    for (int i = 0; i < 6; i++) g_padAxes[i] = tmp[i];
    g_padButtons = buttons;
}

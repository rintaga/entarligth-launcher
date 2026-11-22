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

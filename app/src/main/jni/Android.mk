LOCAL_PATH := $(call my-dir)

# libglfw.so : backend GLFW untuk Android (dipakai LWJGL resmi)
include $(CLEAR_VARS)
LOCAL_MODULE    := glfw
LOCAL_SRC_FILES := glfw/glfw_android.cpp
LOCAL_LDLIBS    := -llog -ldl -landroid -lEGL
LOCAL_CPPFLAGS  := -std=c++17 -fexceptions -O2 -fvisibility=hidden
include $(BUILD_SHARED_LIBRARY)

# libluminacore.so : JNI loader JVM + jembatan input/surface
include $(CLEAR_VARS)
LOCAL_MODULE    := luminacore
LOCAL_SRC_FILES := main.cpp
LOCAL_LDLIBS    := -llog -ldl -landroid -lEGL
LOCAL_CPPFLAGS  := -std=c++17 -fexceptions -O2
include $(BUILD_SHARED_LIBRARY)

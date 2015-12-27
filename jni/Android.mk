LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := D:/Android/android-ndk-r10e/sources/cxx-stl/gnu-libstdc++/4.8/include
LOCAL_C_INCLUDES += F:/ffmpeg2.8.4-android-build-static/arm/include
LOCAL_MODULE    := pureNative
LOCAL_SRC_FILES := pureNative.cpp
LOCAL_CPPFLAGS := -std=c++11
LOCAL_CPPFLAGS += -D__cplusplus=201103L
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_LDLIBS := -landroid -llog -lEGL -lGLESv2 -lm -lz
LOCAL_LDLIBS += -LF:/ffmpeg2.8.4-android-build-static/arm/lib -lavdevice -lavfilter -lswscale -lavformat -lavcodec -lswresample -lavutil
include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)

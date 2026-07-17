cmake_minimum_required(VERSION 3.10)
project(ProcessAudioRecorder)

# 指定 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启 Windows 的 Unicode 支持 (解决 CreateFileA 报错)
add_definitions(-DUNICODE -D_UNICODE)

# 告诉 CMake 头文件在哪里
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/source)

# 添加核心源文件 (删除了报错的 .rc 资源文件)
add_executable(ProcessAudioRecorder
    source/ProcessAudioRecorder.cpp
    source/LoopbackCapture.cpp
    source/AudioSessionLister.cpp
    source/WavSink.cpp
    source/M4aSink.cpp
)

# 强制链接原版项目所依赖的 Windows 底层音频与媒体库
target_link_libraries(ProcessAudioRecorder
    mfplat
    mfuuid
    mmdevapi
    mfreadwrite
    windowsapp
    userenv
)
# WinDV — CMake build for MSVC 2017+ with XP compatibility
#
# Quick start:
#   cmake -B build -G "Visual Studio 15 2017" -A Win32 -T v141_xp
#   cmake --build build --config Release
#
# The v141_xp toolset is optional.  Any Win32 MSVC generator works for
# development; v141_xp is only required for XP-compatible release builds.
#
# MFC is used as a shared DLL (_AFXDLL) by default.  For static linking
# (no MFC DLL dependency on target machine), pass -DSTATIC_MFC=ON.

cmake_minimum_required(VERSION 3.16)
project(WinDV VERSION 1.4.0 LANGUAGES CXX)

# ---- MFC configuration ----
option(STATIC_MFC "Link MFC statically instead of as shared DLL" OFF)
if(STATIC_MFC)
    set(CMAKE_MFC_FLAG 1)  # 1 = Static MFC
else()
    set(CMAKE_MFC_FLAG 2)  # 2 = Shared DLL (_AFXDLL)
endif()

# ---- DirectShow BaseClasses (vendored) ----
add_subdirectory(baseclasses)

# ---- WinDV executable ----
set(WINDV_SOURCES
    AVICheck.cpp
    CaptureCfg.cpp
    DropFilesEdit.cpp
    DShow.cpp
    DV.cpp
    DVToolsDlg.cpp
    RecordCfg.cpp
    StdAfx.cpp
    ToolTab.cpp
    VideoDeviceSel.cpp
    WinDV.cpp
)

set(WINDV_HEADERS
    AVICheck.h
    CaptureCfg.h
    DropFilesEdit.h
    DShow.h
    DV.h
    DVToolsDlg.h
    RecordCfg.h
    Resource.h
    StdAfx.h
    ToolTab.h
    Version.h
    VideoDeviceSel.h
    WinDV.h
)

add_executable(WinDV WIN32 ${WINDV_SOURCES} ${WINDV_HEADERS} WinDV.rc)

# ---- Compiler settings ----
target_compile_definitions(WinDV PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    _WIN32_DCOM
    _WIN32_WINNT=0x0501
    WINVER=0x0501
    $<$<NOT:$<BOOL:${STATIC_MFC}>>:_AFXDLL>
)

# Precompiled header (CMake 3.16+)
target_precompile_headers(WinDV PRIVATE StdAfx.h)

# ---- Include paths ----
target_include_directories(WinDV PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/baseclasses
)

# ---- Link libraries ----
target_link_libraries(WinDV PRIVATE
    strmbase
    strmiids
    quartz
    winmm
    version
    uuid
)

# Suppress default CRT conflict when using shared MFC (/MD).
if(NOT STATIC_MFC)
    set_property(TARGET WinDV APPEND_STRING PROPERTY LINK_FLAGS " /NODEFAULTLIB:libcmt")
endif()

# The manifest is already embedded via WinDV.rc (IDR_MANIFEST).
# Disable linker-generated manifest to avoid duplicate resource error.
set_target_properties(WinDV PROPERTIES LINK_FLAGS "/MANIFEST:NO")

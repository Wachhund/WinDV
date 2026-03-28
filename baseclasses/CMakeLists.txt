# DirectShow BaseClasses — built as static library (strmbase)
#
# These are the Microsoft DirectShow Base Classes from the Windows SDK Samples.
# Vendored here so the project builds without an external SDK installation.

file(GLOB BASECLASSES_CPP "*.cpp")
file(GLOB BASECLASSES_H "*.h")

# Exclude DLL-specific entry points that conflict with EXE linking.
list(FILTER BASECLASSES_CPP EXCLUDE REGEX "dllentry\\.cpp$|dllsetup\\.cpp$")

add_library(strmbase STATIC ${BASECLASSES_CPP} ${BASECLASSES_H})

target_include_directories(strmbase PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(strmbase PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    _WIN32_DCOM
    _WIN32_WINNT=0x0501
    WINVER=0x0501
)

# Suppress warnings in vendored third-party code.
target_compile_options(strmbase PRIVATE /W0)

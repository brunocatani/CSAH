cmake_minimum_required(VERSION 3.23)

# ---- Project Info ----
set(NAME "fo4vr-community-shaders")
set(VERSION 0.1.0)

message(">>> Start build for '${NAME}' v${VERSION}")

# ---- Folders ----
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# ---- Include guards ----
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR ">> In-source builds not allowed.")
endif()

# ---- Setup vcpkg BEFORE project() ----
macro(set_from_environment VARIABLE)
    if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
        set(${VARIABLE} $ENV{${VARIABLE}})
    endif()
endmacro()

set_from_environment(VCPKG_ROOT)
if(DEFINED VCPKG_ROOT)
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
else()
    message(FATAL_ERROR ">> Variable VCPKG_ROOT is not set!")
endif()

# ---- CommonLibF4VR path ----
set_from_environment(COMMON_LIB_F4VR_PATH)
if(NOT DEFINED COMMON_LIB_F4VR_PATH)
    set(COMMON_LIB_F4VR_PATH "${ROOT_DIR}/extern/CommonLibF4VR")
endif()
message(">>> Using CommonLibF4VR at: ${COMMON_LIB_F4VR_PATH}")

# ---- Create Project ----
project(${NAME} VERSION ${VERSION} LANGUAGES CXX)
message(">>> Building project '${PROJECT_NAME}' v${PROJECT_VERSION}")

# ---- C++ Standard ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Build flags ----
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

include(CheckIPOSupported)
check_ipo_supported(RESULT USE_IPO OUTPUT IPO_OUTPUT)
if(USE_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
endif()

# ---- Bring in CommonLibF4VR ----
set(ENABLE_FALLOUT_F4 ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON CACHE BOOL "" FORCE)
add_subdirectory("${COMMON_LIB_F4VR_PATH}/CommonLibF4" CommonLibF4)

# ---- Find packages ----
find_package(spdlog REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(imgui REQUIRED CONFIG)
find_package(directxtk REQUIRED CONFIG)

# Detours uses find_path/find_library pattern per vcpkg
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)

# ---- Sources ----
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${SOURCE_DIR}/*.cpp"
    "${SOURCE_DIR}/*.h"
)

source_group(
    TREE "${SOURCE_DIR}"
    PREFIX "Source Files"
    FILES ${SOURCES}
)

# ---- Create DLL ----
add_library(${PROJECT_NAME} SHARED ${SOURCES} "${SOURCE_DIR}/exports.def")

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${SOURCE_DIR}"
    ${DETOURS_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibF4::CommonLibF4
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    imgui::imgui
    Microsoft::DirectXTK
    d3d11.lib
    d3dcompiler.lib
    dxgi.lib
    ${DETOURS_LIBRARY}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    F4SEVR_SUPPORT
    ENABLE_FALLOUT_VR=1
    ENABLE_FALLOUT_F4=1
    ENABLE_FALLOUT_NG=1
)

# ---- Precompiled header ----
target_precompile_headers(${PROJECT_NAME} PRIVATE
    "${SOURCE_DIR}/PCH.h"
)

# ---- Post build ----
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ROOT_DIR}/package/Data/F4SE/Plugins"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "${ROOT_DIR}/package/Data/F4SE/Plugins/"
    COMMENT "Packaging ${PROJECT_NAME}..."
)

message(">>> ${PROJECT_NAME} configured successfully")

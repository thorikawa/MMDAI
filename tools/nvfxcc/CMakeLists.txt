project(nvfxcc)

cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})

set(CMAKE_BUILD_TYPE "Debug")

# GLFW3
set(GLFW_BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../glfw-src/build-debug/install-root")
find_library(GLFW_LIBRARY glfw3 glfw)
if(NOT GLFW_LIBRARY)
  find_library(GLFW_LIBRARY glfw3 glfw PATH_SUFFIXES lib PATHS ${GLFW_BUNDLE_ROOT} NO_DEFAULT_PATH)
endif()
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h)
if(NOT GLFW_INCLUDE_DIR)
  find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h PATH_SUFFIXES include PATHS ${GLFW_BUNDLE_ROOT} NO_DEFAULT_PATH)
endif()
target_link_libraries(${PROJECT_NAME} ${GLFW_LIBRARY})
include_directories(${GLFW_INCLUDE_DIR})

# Regal
set(REGAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../regal-src")
find_library(REGAL_LIBRARY Regal PATH_SUFFIXES lib PATHS ${REGAL_ROOT} NO_DEFAULT_PATH)
find_path(REGAL_INCLUDE_DIR GL/Regal.h PATH_SUFFIXES include PATHS ${REGAL_ROOT} NO_DEFAULT_PATH)
target_link_libraries(${PROJECT_NAME} ${REGAL_LIBRARY})
include_directories(${REGAL_INCLUDE_DIR})

# GLEW
set(GLEW_BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../glew-src/build-debug/install-root")
find_library(GLEW_LIBRARY glew)
if(NOT GLEW_LIBRARY)
  find_library(GLEW_LIBRARY glew PATH_SUFFIXES lib PATHS ${GLEW_BUNDLE_ROOT} NO_DEFAULT_PATH)
endif()
find_path(GLEW_INCLUDE_DIR GL/glew.h)
if(NOT GLEW_INCLUDE_DIR)
  find_path(GLEW_INCLUDE_DIR GL/glew.h PATH_SUFFIXES include PATHS ${GLEW_BUNDLE_ROOT} NO_DEFAULT_PATH)
endif()
target_link_libraries(${PROJECT_NAME} ${GLEW_LIBRARY})
include_directories(${GLEW_INCLUDE_DIR})

# nvFX
set(NVFX_BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../nvfx-src/build-debug/install-root")
find_library(NVFX_FXLIB_LIBRARY FxLib)
if(NVFX_FXLIB_LIBRARY)
  find_library(NVFX_FXLIBGL_LIBRARY FxLibGL)
  find_library(NVFX_FXPARSER_LIBRARY FxParser)
else()
  find_library(NVFX_FXLIB_LIBRARY FxLib PATH_SUFFIXES lib PATHS ${NVFX_BUNDLE_ROOT} NO_DEFAULT_PATH)
  find_library(NVFX_FXLIBGL_LIBRARY FxLibGL PATH_SUFFIXES lib PATHS ${NVFX_BUNDLE_ROOT} NO_DEFAULT_PATH)
  find_library(NVFX_FXPARSER_LIBRARY FxParser PATH_SUFFIXES lib PATHS ${NVFX_BUNDLE_ROOT} NO_DEFAULT_PATH)
endif()
find_path(NVFX_INCLUDE_DIR FxLib.h)
if(NOT NVFX_INCLUDE_DIR)
  find_path(NVFX_INCLUDE_DIR FxLib.h PATH_SUFFIXES include PATHS ${NVFX_BUNDLE_ROOT} NO_DEFAULT_PATH)
endif()
target_link_libraries(${PROJECT_NAME} ${NVFX_FXPARSER_LIBRARY} ${NVFX_FXLIBGL_LIBRARY} ${NVFX_FXLIB_LIBRARY})
include_directories(${NVFX_INCLUDE_DIR})

if(APPLE)
  find_library(COCOA_FRAMEWORK Cocoa)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
  find_library(IOKIT_FRAMEWORK IOKit)
  find_library(OPENGL_FRAMEWORK OpenGL)
  target_link_libraries(${PROJECT_NAME} ${COCOA_FRAMEWORK} ${OPENGL_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

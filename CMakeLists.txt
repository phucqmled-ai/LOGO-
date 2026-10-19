cmake_minimum_required(VERSION 3.15)
project(QMLogoRunner LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

file(GLOB FFGL_SOURCES
  ${CMAKE_SOURCE_DIR}/ffgl/lib/ffgl/*.cpp
  ${CMAKE_SOURCE_DIR}/ffgl/lib/ffglex/*.cpp
  ${CMAKE_SOURCE_DIR}/ffgl/lib/ffglquickstart/*.cpp
)

set(PLUGIN_SOURCES
  ${CMAKE_SOURCE_DIR}/src/LogoRunner.cpp
  ${CMAKE_SOURCE_DIR}/src/LogoRunner.h
)

if(APPLE)
  add_library(QMLogoRunner MODULE ${PLUGIN_SOURCES} ${FFGL_SOURCES})
  set_target_properties(QMLogoRunner PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION bundle
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER com.qmled.logorunner
    OUTPUT_NAME "QMLogoRunner")
  target_compile_definitions(QMLogoRunner PRIVATE GL_SILENCE_DEPRECATION)
  target_link_libraries(QMLogoRunner PRIVATE "-framework OpenGL" "-framework Cocoa")
elseif(WIN32)
  add_library(QMLogoRunner SHARED ${PLUGIN_SOURCES} ${FFGL_SOURCES} ${CMAKE_SOURCE_DIR}/ffgl/FFGLPlugins.def)
  target_include_directories(QMLogoRunner PRIVATE ${CMAKE_SOURCE_DIR}/ffgl/deps/glew-2.1.0/include)
  target_compile_definitions(QMLogoRunner PRIVATE GLEW_STATIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_link_libraries(QMLogoRunner PRIVATE ${CMAKE_SOURCE_DIR}/ffgl/deps/glew-2.1.0/lib/Release/x64/glew32s.lib opengl32)
  if(MSVC)
    set_property(TARGET QMLogoRunner PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_options(QMLogoRunner PRIVATE /utf-8 /W3)
  endif()
  set_target_properties(QMLogoRunner PROPERTIES OUTPUT_NAME "QMLogoRunner" PREFIX "")
else()
  # Linux: only used for automated testing
  add_library(QMLogoRunner SHARED ${PLUGIN_SOURCES} ${FFGL_SOURCES} ${GLEW_SOURCE})
  target_include_directories(QMLogoRunner PRIVATE ${GLEW_INCLUDE} ${EGL_INCLUDE})
  target_compile_definitions(QMLogoRunner PRIVATE GLEW_STATIC GLEW_NO_GLU GLEW_EGL)
  target_link_libraries(QMLogoRunner PRIVATE ${EGL_LIBRARY} ${GL_LIBRARY})
endif()

target_include_directories(QMLogoRunner PRIVATE ${CMAKE_SOURCE_DIR}/ffgl/lib)

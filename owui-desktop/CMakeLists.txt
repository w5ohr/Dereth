cmake_minimum_required(VERSION 3.20)
project(owui_desktop LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# `cmake --install build` drops the bundle straight into /Applications unless
# a prefix is given.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/Applications" CACHE PATH "Install location" FORCE)
endif()

add_compile_options(-Wall -Wno-unused-function -Wno-deprecated-declarations)
# ARC for every Objective-C++ translation unit.
set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")

set(IMGUI third_party/imgui)

set(IMGUI_SOURCES
  ${IMGUI}/imgui.cpp
  ${IMGUI}/imgui_draw.cpp
  ${IMGUI}/imgui_tables.cpp
  ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/imgui_demo.cpp
  ${IMGUI}/misc/cpp/imgui_stdlib.cpp
  ${IMGUI}/backends/imgui_impl_metal.mm
  ${IMGUI}/backends/imgui_impl_osx.mm
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
  src/*.cpp src/*.mm)

set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns)
set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

add_executable(OpenWebUI MACOSX_BUNDLE ${APP_SOURCES} ${IMGUI_SOURCES} ${APP_ICON})

target_include_directories(OpenWebUI PRIVATE
  src
  ${IMGUI}
  ${IMGUI}/backends
  ${IMGUI}/misc/cpp
  third_party/json
  third_party/stb
)

target_compile_definitions(OpenWebUI PRIVATE
  IMGUI_DEFINE_MATH_OPERATORS
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
  IMGUI_USE_WCHAR32   # emoji and maths alphanumerics live above U+FFFF
  IMGUI_ENABLE_TEST_ENGINE   # item hooks feed the accessibility tree (src/ui/a11y_imgui.cpp)
)

find_library(COCOA Cocoa REQUIRED)
find_library(METAL Metal REQUIRED)
find_library(METALKIT MetalKit REQUIRED)
find_library(QUARTZCORE QuartzCore REQUIRED)
find_library(GAMECONTROLLER GameController REQUIRED)
find_library(SECURITY Security REQUIRED)
find_library(UTTYPE UniformTypeIdentifiers REQUIRED)
find_library(AVFOUNDATION AVFoundation REQUIRED)
find_library(COREAUDIO CoreAudio REQUIRED)
find_library(AUDIOTOOLBOX AudioToolbox REQUIRED)
find_library(USERNOTIFICATIONS UserNotifications REQUIRED)
find_library(CARBON Carbon REQUIRED)
find_library(CORESPOTLIGHT CoreSpotlight REQUIRED)
find_library(QUARTZ Quartz REQUIRED)
find_library(VISION Vision REQUIRED)
find_library(CORESERVICES CoreServices REQUIRED)
find_library(WEBKIT WebKit REQUIRED)
find_library(JAVASCRIPTCORE JavaScriptCore REQUIRED)
find_library(IOKIT IOKit REQUIRED)
find_library(SERVICEMANAGEMENT ServiceManagement REQUIRED)
find_library(CORETEXT CoreText REQUIRED)
find_library(IMAGEIO ImageIO REQUIRED)
find_library(EVENTKIT EventKit REQUIRED)
find_library(CONTACTS Contacts REQUIRED)
find_library(SCREENCAPTUREKIT ScreenCaptureKit REQUIRED)

target_link_libraries(OpenWebUI PRIVATE
  ${CORETEXT} ${IMAGEIO} ${EVENTKIT} ${CONTACTS} ${SCREENCAPTUREKIT}
  ${COCOA} ${METAL} ${METALKIT} ${QUARTZCORE} ${GAMECONTROLLER}
  ${SECURITY} ${UTTYPE} ${AVFOUNDATION} ${COREAUDIO} ${AUDIOTOOLBOX}
  ${USERNOTIFICATIONS} ${CARBON} ${CORESPOTLIGHT} ${QUARTZ} ${VISION} ${CORESERVICES} ${WEBKIT} ${JAVASCRIPTCORE} ${IOKIT} ${SERVICEMANAGEMENT}
  curl sqlite3 z
)

set_target_properties(OpenWebUI PROPERTIES
  OUTPUT_NAME "Open WebUI"
  MACOSX_BUNDLE_BUNDLE_NAME "Open WebUI"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.davidcochran.owui-desktop"
  MACOSX_BUNDLE_BUNDLE_VERSION "1.1"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "1.1"
  MACOSX_BUNDLE_ICON_FILE "AppIcon"
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
)

install(TARGETS OpenWebUI BUNDLE DESTINATION .)
# The copy loses nothing but its signature, so re-sign where it landed.
install(CODE "
  execute_process(COMMAND xattr -cr \"\${CMAKE_INSTALL_PREFIX}/Open WebUI.app\")
  execute_process(COMMAND codesign --force --sign - --timestamp=none
                          \"\${CMAKE_INSTALL_PREFIX}/Open WebUI.app\")
")

# Ad-hoc sign the bundle so macOS gives it a stable-per-build identity; without
# this the keychain prompts on every launch instead of once.
add_custom_command(TARGET OpenWebUI POST_BUILD
  COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:OpenWebUI>"
  COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:OpenWebUI>"
  COMMENT "Ad-hoc signing the app bundle")

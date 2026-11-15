cmake_minimum_required(VERSION 3.29)
project(MVC2Development LANGUAGES C Swift ASM)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "Use build-development-ipa.sh on macOS with the full Xcode app")
endif()
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_Swift_LANGUAGE_VERSION 5)
set(MVC2_DEV_BUILD_NUMBER "1" CACHE STRING "Development build number")
file(GLOB IOS_COMPONENTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.swift")
file(GLOB IOS_APP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/*.swift")
if(NOT IOS_APP)
    message(FATAL_ERROR "The development app sources are missing")
endif()

# Compile the complete linked native arcade program from upstream 0.2.1.
# The original 32-bit desktop/3DS build remains a separate CMake project.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../native64/engine" native-engine)
add_executable(MVC2Development MACOSX_BUNDLE ${IOS_COMPONENTS} ${IOS_APP}
    assets/mvc2_rom.c assets/zip_reader.c input/input.c display/viewport.c)
target_include_directories(MVC2Development PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(MVC2Development PRIVATE mvc2_native_engine z
    "-framework UIKit" "-framework SwiftUI" "-framework Foundation"
    "-framework Metal" "-framework MetalKit" "-framework QuartzCore"
    "-framework UniformTypeIdentifiers" "-framework AVFoundation")
set_target_properties(MVC2Development PROPERTIES
    OUTPUT_NAME MVC2Development
    MACOSX_BUNDLE_BUNDLE_NAME MVC2Development
    MACOSX_BUNDLE_GUI_IDENTIFIER com.spl01nkuh.mvc2.native.dev
    MACOSX_BUNDLE_SHORT_VERSION_STRING 0.2.1
    MACOSX_BUNDLE_BUNDLE_VERSION "${MVC2_DEV_BUILD_NUMBER}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/app/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.spl01nkuh.mvc2.native.dev
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/NativeBridge.h"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
    XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING NO
    XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES)

# Compile the production Metal presentation code into the app's default library.
if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator|iPhoneSimulator")
    set(METAL_TARGET air64-apple-ios16.0-simulator)
else()
    set(METAL_TARGET air64-apple-ios16.0)
endif()
add_custom_command(TARGET MVC2Development POST_BUILD
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/compile-metal.sh"
        "${CMAKE_OSX_SYSROOT}" "${METAL_TARGET}"
        "${CMAKE_CURRENT_SOURCE_DIR}/display/Presentation.metal"
        "${CMAKE_CURRENT_BINARY_DIR}/Presentation.air"
    COMMENT "Compile bundled native CRT presentation shaders"
    VERBATIM)

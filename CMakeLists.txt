#-----------------------------------------------------------------------------------------------#
# Aethereal Device Service                                                                      #
#                                                                                               #
#   A modified OpenRGB, built as a background service with no window: OpenRGB's device         #
#   drivers and its SDK server, and nothing else. No Qt.                                        #
#                                                                                               #
#   OpenRGB's own build (OpenRGB.pro) is left as it is, so upstream merges stay simple. This    #
#   file only picks which of OpenRGB's sources go into the service. See AETHEREAL.md.           #
#                                                                                               #
#   SPDX-License-Identifier: GPL-2.0-or-later                                                   #
#-----------------------------------------------------------------------------------------------#
cmake_minimum_required(VERSION 3.20)

project(aethereal-devices VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

#-----------------------------------------------------------------------------------------------#
# Build information (OpenRGB's sources expect these)                                           #
#-----------------------------------------------------------------------------------------------#
find_package(Git QUIET)
set(GIT_COMMIT_ID   "unknown")
set(GIT_COMMIT_DATE "unknown")
set(GIT_BRANCH      "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} log -n 1 --pretty=format:%H  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_COMMIT_ID   ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} log -n 1 --pretty=format:%ci WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_COMMIT_DATE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_BRANCH      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S UTC" UTC)

# The OpenRGB release this is built from, and our own version on top of it
set(OPENRGB_VERSION "1.0")
set(SERVICE_VERSION_STRING "${PROJECT_VERSION} (OpenRGB ${OPENRGB_VERSION})")

#-----------------------------------------------------------------------------------------------#
# Sources                                                                                       #
#-----------------------------------------------------------------------------------------------#
file(GLOB_RECURSE CONTROLLER_CPP CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Controllers/*.cpp")
file(GLOB_RECURSE CONTROLLER_H   CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Controllers/*.h")

# As in OpenRGB.pro: a _Windows, _Linux, _FreeBSD or _MacOS suffix (or several, such as
# _Windows_Linux) limits a file to those systems
list(FILTER CONTROLLER_CPP EXCLUDE REGEX "_(Windows|Linux|FreeBSD|MacOS)[^/]*\\.cpp$")
if(WIN32)
    set(OS_SUFFIX "Windows")
elseif(APPLE)
    set(OS_SUFFIX "MacOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(OS_SUFFIX "FreeBSD")
else()
    set(OS_SUFFIX "Linux")
endif()
file(GLOB_RECURSE CONTROLLER_CPP_OS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Controllers/*_${OS_SUFFIX}*.cpp")
list(APPEND CONTROLLER_CPP ${CONTROLLER_CPP_OS})

# A driver's .cpp that another one #includes (compiling it again would define it twice)
list(FILTER CONTROLLER_CPP EXCLUDE REGEX "/GenesisXenon200Controller\\.cpp$")

set(CONTROLLER_INCLUDES "")
foreach(header ${CONTROLLER_H})
    get_filename_component(dir ${header} DIRECTORY)
    list(APPEND CONTROLLER_INCLUDES ${dir})
endforeach()
list(REMOVE_DUPLICATES CONTROLLER_INCLUDES)

# OpenRGB's core, without its window (qt/, startup/), plugins (PluginManager, OpenRGBPluginAPI)
# or the Qt-only parts (SuspendResume, ColorWheel)
set(CORE_SOURCES
    aethereal/service_main.cpp
    cli.cpp
    DetectionManager.cpp
    dmiinfo/dmiinfo.cpp
    JsonUtils.cpp
    LogManager.cpp
    NetworkClient.cpp
    NetworkProtocol.cpp
    NetworkServer.cpp
    ProfileManager.cpp
    ResourceManager.cpp
    SettingsManager.cpp
    MathUtils.cpp
    StringUtils.cpp
    SPDAccessor/DDR4DirectAccessor.cpp
    SPDAccessor/DDR5DirectAccessor.cpp
    SPDAccessor/SPDAccessor.cpp
    SPDAccessor/SPDDetector.cpp
    SPDAccessor/SPDWrapper.cpp
    i2c_smbus/i2c_smbus.cpp
    i2c_tools/i2c_tools.cpp
    interop/DeviceGuard.cpp
    interop/DeviceGuardLock.cpp
    interop/DeviceGuardManager.cpp
    net_port/net_port.cpp
    serial_port/serial_port.cpp
    AutoStart/AutoStart.cpp
    KeyboardLayoutManager/KeyboardLayoutManager.cpp
    RGBController/RGBController.cpp
    RGBController/RGBController_Dummy.cpp
    RGBController/RGBControllerKeyNames.cpp
    RGBController/RGBController_Network.cpp
    RGBController/RGBController_Virtual.cpp
    dependencies/libe131/src/e131.c
    qt/hsv.cpp
)

file(GLOB HUEPLUSPLUS_SOURCES "${CMAKE_SOURCE_DIR}/dependencies/hueplusplus-1.2.0/src/*.cpp")
list(FILTER HUEPLUSPLUS_SOURCES EXCLUDE REGEX "(Win|Lin)HttpHandler\\.cpp$")

if(WIN32)
    set(OS_SOURCES
        dependencies/hueplusplus-1.2.0/src/WinHttpHandler.cpp
        dependencies/NVFC/nvapi.cpp
        i2c_smbus/Windows/i2c_smbus_amdadl.cpp
        i2c_smbus/Windows/i2c_smbus_nvapi.cpp
        i2c_smbus/Windows/i2c_smbus_pawnio.cpp
        super_io/super_io_pawnio.cpp
        scsiapi/scsiapi_windows.c
        serial_port/find_usb_serial_port_win.cpp
        wmi/wmi.cpp
        AutoStart/AutoStart-Windows.cpp
    )
elseif(APPLE)
    message(FATAL_ERROR "The service is built for Windows and Linux only")
else()
    set(OS_SOURCES
        dependencies/hueplusplus-1.2.0/src/LinHttpHandler.cpp
        dependencies/NVFC/nvapi.cpp
        i2c_smbus/Linux/i2c_smbus_linux.cpp
        scsiapi/scsiapi_linux.c
        serial_port/find_usb_serial_port_linux.cpp
        SPDAccessor/EE1004Accessor_Linux.cpp
        SPDAccessor/SPD5118Accessor_Linux.cpp
        super_io/super_io.cpp
        AutoStart/AutoStart-Linux.cpp
    )
endif()

add_executable(aethereal-devices
    ${CORE_SOURCES}
    ${OS_SOURCES}
    ${CONTROLLER_CPP}
    ${HUEPLUSPLUS_SOURCES}
)

target_include_directories(aethereal-devices PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CONTROLLER_INCLUDES}
    dependencies/CRCpp
    dependencies/hueplusplus-1.2.0/include
    dependencies/hueplusplus-1.2.0/include/hueplusplus
    dependencies/httplib
    dependencies/json
    dependencies/mdns
    dependencies/libe131/src
    dependencies/stb
    dmiinfo
    hidapi_wrapper
    i2c_smbus
    i2c_tools
    interop
    net_port
    pci_ids
    scsiapi
    serial_port
    super_io
    AutoStart
    KeyboardLayoutManager
    RGBController
    SPDAccessor
    SuspendResume
    aethereal
    qt                  # only for hsv.h (plain C++, used by some drivers)
)

target_compile_definitions(aethereal-devices PRIVATE
    VERSION_STRING="${SERVICE_VERSION_STRING}"
    BUILDDATE_STRING="${BUILD_DATE}"
    GIT_COMMIT_ID="${GIT_COMMIT_ID}"
    GIT_COMMIT_DATE="${GIT_COMMIT_DATE}"
    GIT_BRANCH="${GIT_BRANCH}"
    AETHEREAL_SERVICE=1
)

#-----------------------------------------------------------------------------------------------#
# Windows (x64, MSVC): OpenRGB's bundled libraries                                             #
#-----------------------------------------------------------------------------------------------#
if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Only 64-bit Windows builds are supported")
    endif()

    set(DEPS ${CMAKE_SOURCE_DIR}/dependencies)

    target_include_directories(aethereal-devices PRIVATE
        ${DEPS}/display-library/include
        ${DEPS}/hidapi-hotplug-win/include
        ${DEPS}/libusb-1.0.27/include
        ${DEPS}/mbedtls-3.2.1/include
        ${DEPS}/NVFC
        ${DEPS}/PawnIO
        i2c_smbus/Windows
        wmi
    )

    target_compile_definitions(aethereal-devices PRIVATE
        _MBCS
        WIN32
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        HID_HOTPLUG_ENABLED=1
    )

    if(MSVC)
        target_compile_options(aethereal-devices PRIVATE /utf-8 /MP /bigobj)
        # No console window of its own (it attaches to one when run from a terminal)
        set_target_properties(aethereal-devices PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_options(aethereal-devices PRIVATE /ENTRY:mainCRTStartup)
    endif()

    target_link_libraries(aethereal-devices PRIVATE
        ws2_32
        iphlpapi
        ${DEPS}/libusb-1.0.27/VS2019/MS64/dll/libusb-1.0.lib
        ${DEPS}/hidapi-hotplug-win/x64/hidapi-hotplug.lib
        ${DEPS}/mbedtls-3.2.1/lib/x64/mbedcrypto.lib
        ${DEPS}/mbedtls-3.2.1/lib/x64/mbedtls.lib
        ${DEPS}/mbedtls-3.2.1/lib/x64/mbedx509.lib
        ${DEPS}/PawnIO/PawnIOLib.lib
    )

    # What goes beside the .exe: the libraries, and PawnIO's modules (the small scripts it runs
    # for each kind of SMBus hardware)
    set(RUNTIME_FILES
        ${DEPS}/libusb-1.0.27/VS2019/MS64/dll/libusb-1.0.dll
        ${DEPS}/hidapi-hotplug-win/x64/hidapi-hotplug.dll
        ${DEPS}/PawnIO/PawnIOLib.dll
    )
    file(GLOB PAWNIO_MODULES "${DEPS}/PawnIO/modules/*.bin")
    foreach(file ${RUNTIME_FILES} ${PAWNIO_MODULES})
        add_custom_command(TARGET aethereal-devices POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${file} $<TARGET_FILE_DIR:aethereal-devices>)
    endforeach()
    install(FILES ${RUNTIME_FILES} ${PAWNIO_MODULES} DESTINATION .)
    install(TARGETS aethereal-devices RUNTIME DESTINATION .)

#-----------------------------------------------------------------------------------------------#
# Linux: system libraries (mbedtls 3; set MBEDTLS_ROOT if it isn't where the system keeps it)  #
#-----------------------------------------------------------------------------------------------#
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    pkg_check_modules(HIDAPI_HOTPLUG IMPORTED_TARGET hidapi-hotplug-hidraw)
    if(HIDAPI_HOTPLUG_FOUND)
        set(HIDAPI_TARGET PkgConfig::HIDAPI_HOTPLUG)
        target_compile_definitions(aethereal-devices PRIVATE HID_HOTPLUG_ENABLED=1)
    else()
        pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)
        set(HIDAPI_TARGET PkgConfig::HIDAPI)
    endif()

    if(MBEDTLS_ROOT)
        target_include_directories(aethereal-devices BEFORE PRIVATE ${MBEDTLS_ROOT}/include)
        target_link_directories(aethereal-devices PRIVATE ${MBEDTLS_ROOT}/lib)
    elseif(EXISTS /usr/include/mbedtls3)
        target_include_directories(aethereal-devices BEFORE PRIVATE /usr/include/mbedtls3)
        target_link_directories(aethereal-devices PRIVATE /usr/lib/mbedtls3)
    endif()

    target_include_directories(aethereal-devices PRIVATE dependencies/NVFC i2c_smbus/Linux)
    target_compile_options(aethereal-devices PRIVATE -Wno-implicit-fallthrough -Wno-psabi)

    target_link_libraries(aethereal-devices PRIVATE
        PkgConfig::LIBUSB
        ${HIDAPI_TARGET}
        mbedx509 mbedtls mbedcrypto
        dl
        pthread
    )

    install(TARGETS aethereal-devices RUNTIME DESTINATION bin)
endif()

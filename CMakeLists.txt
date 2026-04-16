cmake_minimum_required(VERSION 3.20)
project(XDFLSLReplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Detect static vs shared Qt to decide linking strategy
option(STATIC_BUILD "Build a fully static executable" OFF)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

# liblsl via FetchContent
include(FetchContent)

# For static builds, build liblsl as a static library
if(STATIC_BUILD)
    set(LSL_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    liblsl
    GIT_REPOSITORY https://github.com/sccn/liblsl.git
    GIT_TAG        v1.16.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(liblsl)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/XdfLoader.cpp
    src/LslReplayEngine.cpp
    src/StreamChartView.cpp
    src/TimelineWidget.cpp
    src/StreamSidebar.cpp
)

set(HEADERS
    src/MainWindow.h
    src/XdfLoader.h
    src/LslReplayEngine.h
    src/StreamChartView.h
    src/TimelineWidget.h
    src/StreamSidebar.h
)

# On Windows static builds, use WIN32 to avoid console window
if(WIN32 AND STATIC_BUILD)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
    lsl
)

# Static builds on Windows need extra system libs and Qt plugins linked in
if(STATIC_BUILD AND WIN32)
    # Link Windows system libraries needed by static Qt
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32 winmm iphlpapi dwmapi uxtheme
    )
    # Import the static Qt platform plugin
    target_compile_definitions(${PROJECT_NAME} PRIVATE QT_STATICPLUGIN)
    find_package(Qt6 COMPONENTS QWindowsIntegrationPlugin QUIET)
    if(TARGET Qt6::QWindowsIntegrationPlugin)
        target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::QWindowsIntegrationPlugin)
    endif()
elseif(NOT STATIC_BUILD)
    # Shared builds: copy DLLs next to executable
    if(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:lsl>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms
        )
    endif()
endif()

# macOS: create app bundle with proper RPATH
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.augcoglab.xdflslreplayer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin BUNDLE DESTINATION .)

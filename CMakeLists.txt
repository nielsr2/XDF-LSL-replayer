cmake_minimum_required(VERSION 3.20)
project(XDFLSLReplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

# liblsl via FetchContent
include(FetchContent)

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
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
    lsl
)

# Copy runtime dependencies next to executable (shared builds)
if(WIN32)
    # Copy lsl.dll
    if(TARGET lsl AND BUILD_SHARED_LIBS OR NOT DEFINED BUILD_SHARED_LIBS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:lsl>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
    # Copy Qt platform plugin (only needed for local dev; windeployqt handles CI)
    if(TARGET Qt6::QWindowsIntegrationPlugin)
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

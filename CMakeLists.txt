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

# Qt resource file (embeds icon into binary)
set(RESOURCES resources/resources.qrc)

# Generate Windows .rc with correct absolute path to .ico
if(WIN32)
    set(APP_ICO_PATH "${CMAKE_SOURCE_DIR}/resources/app.ico")
    string(REPLACE "/" "\\\\" APP_ICO_PATH_RC "${APP_ICO_PATH}")
    file(WRITE "${CMAKE_BINARY_DIR}/app.rc" "IDI_ICON1 ICON \"${APP_ICO_PATH_RC}\"\n")
    set(WIN_RC "${CMAKE_BINARY_DIR}/app.rc")
endif()

# On Windows, use WIN32 subsystem and embed .ico into the exe
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${RESOURCES}
        ${WIN_RC})
elseif(APPLE)
    set(MACOSX_ICON resources/icon_1024.png)
    set_source_files_properties(${MACOSX_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${RESOURCES}
        ${MACOSX_ICON})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "icon_1024.png"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.augcoglab.xdflslreplayer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})
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

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin BUNDLE DESTINATION .)

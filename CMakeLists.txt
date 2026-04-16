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

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
    lsl
)

# Copy lsl.dll next to the executable after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:lsl>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# Deploy Qt platform plugins next to the executable
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms
)

# Test loader (uses our XdfLoader, needs Qt for XML parsing)
add_executable(test_load src/test_load.cpp src/XdfLoader.cpp)
target_link_libraries(test_load PRIVATE Qt6::Core)

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

cmake_minimum_required(VERSION 3.20)
project(ai-photo-editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ────────────────────────────────────────────────────────────

# Dear ImGui via FetchContent
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a static library
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# GLFW
find_package(glfw3 3.3 REQUIRED)
target_link_libraries(imgui_lib PUBLIC glfw)

# OpenGL
find_package(OpenGL REQUIRED)
target_link_libraries(imgui_lib PUBLIC OpenGL::GL)

# OpenImageIO
find_package(OpenImageIO REQUIRED)

# ONNX Runtime with DirectML (pre-built package)
set(ONNXRT_DIR "${CMAKE_SOURCE_DIR}/libs/onnxruntime-directml")
if(EXISTS "${ONNXRT_DIR}/build/native/include/onnxruntime_cxx_api.h")
    message(STATUS "ONNX Runtime (DirectML) found – AI features enabled")
    set(HAS_ONNXRUNTIME TRUE)
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${ONNXRT_DIR}/runtimes/win-x64/native/onnxruntime.lib"
        IMPORTED_LOCATION "${ONNXRT_DIR}/runtimes/win-x64/native/onnxruntime.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRT_DIR}/build/native/include"
    )
else()
    message(WARNING "ONNX Runtime NOT found – AI features will use classical fallbacks")
    set(HAS_ONNXRUNTIME FALSE)
endif()

# ── Main Executable ─────────────────────────────────────────────────────────

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/app.cpp
    src/image.cpp
    src/viewport.cpp
    src/adjustments.cpp
    src/ai_processor.cpp
    src/crop_tool.cpp
    src/ui_panels.cpp
    src/ImGuiFileDialog.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui_lib
    glfw
    OpenGL::GL
    OpenImageIO::OpenImageIO
)

if(HAS_ONNXRUNTIME)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_ONNXRUNTIME=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE onnxruntime)
    # Copy ONNX Runtime DLLs next to executable
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRT_DIR}/runtimes/win-x64/native/onnxruntime.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRT_DIR}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# Copy models directory next to binary
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/models
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
)

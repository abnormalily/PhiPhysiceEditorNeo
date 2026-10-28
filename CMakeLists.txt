cmake_minimum_required(VERSION 3.20)
project(PhiPhysiceEditorNeo VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PHIPHYSICE_PLATFORM_SOURCES)
if(WIN32)
    enable_language(RC)
    list(APPEND PHIPHYSICE_PLATFORM_SOURCES src/app/app.rc)
endif()

add_executable(PhiPhysiceEditorNeo
    src/main.cpp
    src/app/App.cpp
    src/app/SelfTest.cpp
    src/app/Settings.cpp
    src/app/SingleInstance.cpp
    src/app/PlatformDialog.cpp
    src/physics/Physics.cpp
    src/model/NoteType.cpp
    src/model/Document.cpp
    src/model/Events.cpp
    src/io/ProjectIO.cpp
    src/ui/Ui.cpp
    src/ui/EditorUI.cpp
    src/ui/EditorLeft.cpp
    src/ui/EditorRight.cpp
    src/ui/EditorModals.cpp
    src/ui/EditorEvents.cpp
    ${PHIPHYSICE_PLATFORM_SOURCES}
)

target_include_directories(PhiPhysiceEditorNeo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(PhiPhysiceEditorNeo PRIVATE _CRT_SECURE_NO_WARNINGS)

if(WIN32)
    # Hide the console window on launch; it remains a normal GUI process.
    set_target_properties(PhiPhysiceEditorNeo PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(MSVC)
    target_compile_options(PhiPhysiceEditorNeo PRIVATE /utf-8 /W3 /permissive- /EHsc)
    # app.rc embeds the asInvoker manifest resource; disable the linker's own
    # manifest so the executable does not end up with two MANIFEST resources.
    # WIN32_EXECUTABLE uses the Windows subsystem (no command prompt window);
    # mainCRTStartup keeps the normal main() entry point.
    target_link_options(PhiPhysiceEditorNeo PRIVATE "/MANIFEST:NO" "/ENTRY:mainCRTStartup")
else()
    target_compile_options(PhiPhysiceEditorNeo PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- raylib -----------------------------------------------------------------
# Prebuilt raylib: configure with -DRAYLIB_ROOT=C:/path/to/raylib
# Otherwise CMake tries find_package(raylib) and finally fetches raylib 5.5.
set(RAYLIB_ROOT "" CACHE PATH "Root of a prebuilt raylib distribution")

set(PHIPHYSICE_RAYLIB_READY FALSE)
if(RAYLIB_ROOT AND EXISTS "${RAYLIB_ROOT}/include/raylib.h")
    find_library(RAYLIB_LIBRARY
        NAMES raylib
        PATHS "${RAYLIB_ROOT}/lib" "${RAYLIB_ROOT}/lib/msvc"
        NO_DEFAULT_PATH)
    if(RAYLIB_LIBRARY)
        target_include_directories(PhiPhysiceEditorNeo PRIVATE "${RAYLIB_ROOT}/include")
        target_link_libraries(PhiPhysiceEditorNeo PRIVATE "${RAYLIB_LIBRARY}")
        set(PHIPHYSICE_RAYLIB_READY TRUE)
        message(STATUS "raylib: ${RAYLIB_LIBRARY}")
    endif()
endif()

if(NOT PHIPHYSICE_RAYLIB_READY)
    find_package(raylib 5.0 QUIET)
    if(raylib_FOUND)
        target_link_libraries(PhiPhysiceEditorNeo PRIVATE raylib)
        set(PHIPHYSICE_RAYLIB_READY TRUE)
        message(STATUS "raylib: find_package")
    endif()
endif()

if(NOT PHIPHYSICE_RAYLIB_READY)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)
    target_link_libraries(PhiPhysiceEditorNeo PRIVATE raylib)
    message(STATUS "raylib: fetched from GitHub")
endif()

if(WIN32)
    target_link_libraries(PhiPhysiceEditorNeo PRIVATE winmm gdi32 opengl32 user32 shell32 ole32)
endif()
if(APPLE)
    target_link_libraries(PhiPhysiceEditorNeo PRIVATE "-framework Cocoa" "-framework IOKit"
        "-framework CoreVideo" "-framework OpenGL")
endif()
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(PhiPhysiceEditorNeo PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# --- data folder ------------------------------------------------------------
# The app resolves its data root from the executable directory. Copy the
# editable data folders next to the binary for development builds.
foreach(DATA_DIR components projects assets)
    add_custom_command(TARGET PhiPhysiceEditorNeo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/${DATA_DIR}"
            "$<TARGET_FILE_DIR:PhiPhysiceEditorNeo>/${DATA_DIR}"
        COMMENT "Copying ${DATA_DIR}/ next to the executable")
endforeach()

option(PHIPHYSICE_COPY_TO_ROOT "Copy the Windows executable to the source root" ON)
if(WIN32 AND PHIPHYSICE_COPY_TO_ROOT)
    add_custom_command(TARGET PhiPhysiceEditorNeo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PhiPhysiceEditorNeo>" "${CMAKE_CURRENT_SOURCE_DIR}/PhiPhysiceEditorNeo.exe"
        COMMENT "Copying PhiPhysiceEditorNeo.exe to the project root")
endif()

install(TARGETS PhiPhysiceEditorNeo RUNTIME DESTINATION .)
install(DIRECTORY components projects assets DESTINATION .)

cmake_minimum_required(VERSION 3.22.1)
project(polyworld LANGUAGES C CXX)

set(PW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src")
set(PW_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
set(LUA_SRC_DIR "${PW_ROOT_DIR}/libs/lua-5.4.7/src")
set(JOLTC_SRC_DIR "${PW_ROOT_DIR}/libs/joltc")

# --- JoltC + Jolt Physics ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_ASSERTS OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
add_subdirectory("${JOLTC_SRC_DIR}" "${CMAKE_BINARY_DIR}/joltc" EXCLUDE_FROM_ALL)

# --- Lua 5.4 (library sources only) ---
set(LUA_SRCS
    "${LUA_SRC_DIR}/lapi.c"
    "${LUA_SRC_DIR}/lauxlib.c"
    "${LUA_SRC_DIR}/lbaselib.c"
    "${LUA_SRC_DIR}/lcode.c"
    "${LUA_SRC_DIR}/lcorolib.c"
    "${LUA_SRC_DIR}/lctype.c"
    "${LUA_SRC_DIR}/ldblib.c"
    "${LUA_SRC_DIR}/ldebug.c"
    "${LUA_SRC_DIR}/ldo.c"
    "${LUA_SRC_DIR}/ldump.c"
    "${LUA_SRC_DIR}/lfunc.c"
    "${LUA_SRC_DIR}/lgc.c"
    "${LUA_SRC_DIR}/linit.c"
    "${LUA_SRC_DIR}/liolib.c"
    "${LUA_SRC_DIR}/llex.c"
    "${LUA_SRC_DIR}/lmathlib.c"
    "${LUA_SRC_DIR}/lmem.c"
    "${LUA_SRC_DIR}/loadlib.c"
    "${LUA_SRC_DIR}/lobject.c"
    "${LUA_SRC_DIR}/lopcodes.c"
    "${LUA_SRC_DIR}/loslib.c"
    "${LUA_SRC_DIR}/lparser.c"
    "${LUA_SRC_DIR}/lstate.c"
    "${LUA_SRC_DIR}/lstring.c"
    "${LUA_SRC_DIR}/lstrlib.c"
    "${LUA_SRC_DIR}/ltable.c"
    "${LUA_SRC_DIR}/ltablib.c"
    "${LUA_SRC_DIR}/ltm.c"
    "${LUA_SRC_DIR}/lundump.c"
    "${LUA_SRC_DIR}/lutf8lib.c"
    "${LUA_SRC_DIR}/lvm.c"
    "${LUA_SRC_DIR}/lzio.c"
)

add_library(polyworld SHARED
    "${PW_SRC_DIR}/android_boot.c"
    "${PW_SRC_DIR}/android_stubs.c"
    "${PW_SRC_DIR}/platform_android.c"
    "${PW_SRC_DIR}/main.c"
    "${PW_SRC_DIR}/input.c"
    "${PW_SRC_DIR}/texture.c"
    "${PW_SRC_DIR}/shader.c"
    "${PW_SRC_DIR}/shader_embed.c"
    "${PW_SRC_DIR}/log.c"
    "${PW_SRC_DIR}/font.c"
    "${PW_SRC_DIR}/auth.c"
    "${PW_SRC_DIR}/login_screen.c"
    "${PW_SRC_DIR}/avatar_editor.c"
    "${PW_SRC_DIR}/catalog_ui.c"
    "${PW_SRC_DIR}/renderer.c"
    "${PW_SRC_DIR}/physics.c"
    "${PW_SRC_DIR}/world_loader.c"
    "${PW_SRC_DIR}/scene.c"
    "${PW_SRC_DIR}/mesh_loader.c"
    "${PW_SRC_DIR}/mesh_primitives.c"
    "${PW_SRC_DIR}/brick_batch.c"
    "${PW_SRC_DIR}/camera.c"
    "${PW_SRC_DIR}/avatar.c"
    "${PW_SRC_DIR}/avatar_anim.c"
    "${PW_SRC_DIR}/vr_ik.c"
    "${PW_SRC_DIR}/accessory.c"
    "${PW_SRC_DIR}/skybox.c"
    "${PW_SRC_DIR}/chat.c"
    "${PW_SRC_DIR}/text_edit.c"
    "${PW_SRC_DIR}/emoji_shortcodes.c"
    "${PW_SRC_DIR}/social.c"
    "${PW_SRC_DIR}/game_menu.c"
    "${PW_SRC_DIR}/ui_theme.c"
    "${PW_SRC_DIR}/gfx_benchmark.c"
    "${PW_SRC_DIR}/math_types.c"
    "${PW_SRC_DIR}/part_material.c"
    "${PW_SRC_DIR}/place_bin.c"
    "${PW_SRC_DIR}/net_client.c"
    "${PW_SRC_DIR}/scripting.c"
    "${PW_SRC_DIR}/lua_pw_event.c"
    "${PW_SRC_DIR}/lua_pw_task.c"
    "${PW_SRC_DIR}/touch_controls.c"
    "${PW_SRC_DIR}/audio.c"
    "${PW_SRC_DIR}/vidactor.c"
    "${PW_SRC_DIR}/vr_session.c"
    "${PW_SRC_DIR}/vr_hub.c"
    "${PW_SRC_DIR}/vr_openxr.c"
    "${PW_SRC_DIR}/vr_openxr_quest.c"
    ${LUA_SRCS}
)

target_include_directories(polyworld PRIVATE
    "${PW_SRC_DIR}"
    "${PW_SRC_DIR}/../desktop"
    "${LUA_SRC_DIR}"
    "${JOLTC_SRC_DIR}"
)

option(PW_QUEST "Meta Quest OpenXR immersive build" OFF)

target_compile_definitions(polyworld PRIVATE
    PW_GLES=1
    __ANDROID__=1
    MA_NO_RUNTIME_LINKING=1
    MA_NO_AAUDIO=1
)

# Prefer -DCLIENT_VERSION from Gradle; fall back to current desktop release.
if(DEFINED CLIENT_VERSION)
    target_compile_definitions(polyworld PRIVATE "CLIENT_VERSION=\"${CLIENT_VERSION}\"")
else()
    target_compile_definitions(polyworld PRIVATE "CLIENT_VERSION=\"26.4.2\"")
endif()

target_compile_options(polyworld PRIVATE
    -Wall
    -Wextra
    -std=gnu11
)

# Lua is C89-ish; silence noisy pedantic warnings from the engine's -Wextra
set_source_files_properties(${LUA_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-compare")

find_package(game-activity REQUIRED CONFIG)

target_link_libraries(polyworld
    android
    log
    EGL
    GLESv3
    OpenSLES
    m
    c++_shared
    joltc
    Jolt
    game-activity::game-activity_static
)

if(PW_QUEST)
    target_compile_definitions(polyworld PRIVATE VR=1 PW_OPENXR=1 PW_QUEST=1)
    find_package(OpenXR REQUIRED CONFIG)
    target_link_libraries(polyworld OpenXR::openxr_loader)
endif()

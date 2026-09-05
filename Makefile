# PolyWorld client
#
# Build from this folder. Deps live here (src, libs, assets, web, android, desktop, tools).
#
#   make                 native desktop
#   make wasm
#   make android
#   make windows-native
#   make native-vr / native-debug / vidnative

CC       = gcc
EMCC     = emcc
CFLAGS   = -Wall -Wextra -std=gnu11 -O2

ODE_SRC        = libs/ode
ODE_WASM       = libs/ode-build-wasm
ODE_INC        = -I$(ODE_SRC)/include -I$(ODE_WASM)/include
ODE_LIB_WASM   = -L$(ODE_WASM) -lode -lm

JOLTC_SRC   = libs/joltc
JOLTC_BUILD = libs/joltc-build
JOLTC_INC   = -I$(JOLTC_SRC)
JOLTC_LIB   = -L$(JOLTC_BUILD) -ljoltc -L$(JOLTC_BUILD)/JoltPhysics/Build -lJolt

LUA_SRC     = libs/lua-5.4.7/src
LUA_INC     = -I$(LUA_SRC)
LUA_SRCS    = $(filter-out $(LUA_SRC)/lua.c $(LUA_SRC)/luac.c, $(wildcard $(LUA_SRC)/*.c))

SRC_DIR  = src
WEB_DIR  = web
OUT_DIR  = build
ASSETS   = assets

ENGINE_SRCS = $(filter-out $(SRC_DIR)/avatar_viewer.c $(SRC_DIR)/studio_wasm.c $(SRC_DIR)/android_boot.c $(SRC_DIR)/platform_android.c $(SRC_DIR)/viewer_brick_batch_stub.c, $(wildcard $(SRC_DIR)/*.c))
NATIVE_SRCS = $(filter-out $(SRC_DIR)/platform.c, $(ENGINE_SRCS))

EM_FLAGS = -sUSE_WEBGL2=1 \
           -sFULL_ES3=1 \
           -sALLOW_MEMORY_GROWTH=1 \
           -sSTACK_SIZE=524288 \
           -sEXPORTED_FUNCTIONS='["_main","_input_on_keydown","_input_on_keyup","_input_on_mousedown","_input_on_mouseup","_input_on_mousemove","_input_on_scroll","_input_set_mouse_pos","_resize_canvas","_chat_handle_key","_chat_handle_char","_player_respawn","_set_mobile_mode","_audio_music_cred_loaded"]' \
           -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","stringToUTF8"]' \
           --shell-file $(WEB_DIR)/shell.html \
           --preload-file $(ASSETS)@assets \
           $(ODE_INC)

WASM_CFLAGS = -Wall -Wextra -std=gnu11 -O2

DISCORD_DIR = libs/discord-rpc
DISCORD_INC = -I$(DISCORD_DIR)/include
DISCORD_LIB = -L$(DISCORD_DIR)/lib

GLFW_STATIC_DIR = libs/glfw-build
GLFW_STATIC_LIB = $(GLFW_STATIC_DIR)/lib/libglfw3.a
NATIVE_SHARED ?= 0

ifeq ($(NATIVE_SHARED),1)
  NATIVE_CFLAGS =
  NATIVE_LIBS = -lGL -lGLEW -lglfw -lm -lpthread -lstdc++ -lcurl -ldl -ldiscord-rpc
else ifneq ($(wildcard $(GLFW_STATIC_LIB)),)
  NATIVE_CFLAGS = -DGLEW_STATIC -I$(GLFW_STATIC_DIR)/include
  NATIVE_WAYLAND_LIBS := $(shell pkg-config --exists wayland-client wayland-cursor wayland-egl xkbcommon 2>/dev/null && pkg-config --libs wayland-client wayland-cursor wayland-egl xkbcommon)
  NATIVE_LIBS = -lGL $(GLFW_STATIC_LIB) -Wl,-Bstatic -lGLEW -Wl,-Bdynamic \
    -lX11 -lXrandr -lXi -lXcursor -lXinerama -lXext \
    $(NATIVE_WAYLAND_LIBS) \
    -lm -lpthread -lstdc++ -lcurl -ldl -ldiscord-rpc -lrt
else
  NATIVE_CFLAGS =
  NATIVE_LIBS = -lGL -lGLEW -lglfw -lm -lpthread -lstdc++ -lcurl -ldl -ldiscord-rpc
endif

OPENXR_CFLAGS := $(shell pkg-config --cflags openxr 2>/dev/null)
OPENXR_LIBS := $(shell pkg-config --libs openxr 2>/dev/null)
ifeq ($(OPENXR_CFLAGS)$(OPENXR_LIBS),)
  ifneq ($(wildcard /usr/include/openxr/openxr.h),)
    OPENXR_CFLAGS := -DPW_OPENXR
    OPENXR_LIBS := -lopenxr_loader
  endif
else
  OPENXR_CFLAGS := -DPW_OPENXR $(OPENXR_CFLAGS)
endif

.PHONY: all native native-vr native-debug vidnative wasm serve android android-clean windows-native viewer clean
all: native

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

native: $(OUT_DIR)/polyworld

$(OUT_DIR)/polyworld: $(NATIVE_SRCS) $(LUA_SRCS) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(NATIVE_CFLAGS) $(JOLTC_INC) $(LUA_INC) $(DISCORD_INC) $(NATIVE_SRCS) $(LUA_SRCS) -o $@ $(DISCORD_LIB) $(JOLTC_LIB) $(NATIVE_LIBS) -no-pie

native-vr: $(OUT_DIR)/polyworld_vr

$(OUT_DIR)/polyworld_vr: $(NATIVE_SRCS) $(LUA_SRCS) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(NATIVE_CFLAGS) -DVR $(OPENXR_CFLAGS) $(JOLTC_INC) $(LUA_INC) $(DISCORD_INC) $(NATIVE_SRCS) $(LUA_SRCS) -o $@ $(DISCORD_LIB) $(JOLTC_LIB) $(NATIVE_LIBS) $(OPENXR_LIBS) -no-pie

native-debug: $(OUT_DIR)/pw_debug

$(OUT_DIR)/pw_debug: $(NATIVE_SRCS) $(LUA_SRCS) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(NATIVE_CFLAGS) -DPW_DEBUG $(JOLTC_INC) $(LUA_INC) $(DISCORD_INC) $(NATIVE_SRCS) $(LUA_SRCS) -o $@ $(DISCORD_LIB) $(JOLTC_LIB) $(NATIVE_LIBS) -no-pie

vidnative: $(OUT_DIR)/vidpolyworld

$(OUT_DIR)/vidpolyworld: $(NATIVE_SRCS) $(LUA_SRCS) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(NATIVE_CFLAGS) -DVIDACTOR $(JOLTC_INC) $(LUA_INC) $(DISCORD_INC) $(NATIVE_SRCS) $(LUA_SRCS) -o $@ $(DISCORD_LIB) $(JOLTC_LIB) $(NATIVE_LIBS) -no-pie

wasm: $(OUT_DIR)/polyworld.html

$(OUT_DIR)/polyworld.html: $(ENGINE_SRCS) $(LUA_SRCS) $(WEB_DIR)/shell.html | $(OUT_DIR)
	$(EMCC) $(WASM_CFLAGS) $(EM_FLAGS) $(LUA_INC) $(ENGINE_SRCS) $(LUA_SRCS) $(ODE_LIB_WASM) -o $@

serve: wasm
	@echo "Serving at http://localhost:8080"
	python3 -m http.server 8080 --directory $(OUT_DIR)

android:
	@VER_STAMP="$(if $(filter-out 0.0,$(VER)),$(VER),26.3.12)"; \
	echo "=== Building Android APK (CLIENT_VERSION=$$VER_STAMP) ==="; \
	cd android && ./gradlew :app:assembleDebug -PCLIENT_VERSION=$$VER_STAMP

android-clean:
	cd android && ./gradlew :app:clean

windows-native:
	$(MAKE) -f Makefile.win

VIEWER_SRCS = $(SRC_DIR)/avatar_viewer.c $(SRC_DIR)/platform.c $(SRC_DIR)/renderer.c $(SRC_DIR)/scene.c $(SRC_DIR)/shader.c $(SRC_DIR)/math_types.c \
              $(SRC_DIR)/mesh_loader.c $(SRC_DIR)/texture.c $(SRC_DIR)/avatar_anim.c $(SRC_DIR)/mesh_primitives.c $(SRC_DIR)/camera.c $(SRC_DIR)/accessory.c \
              $(SRC_DIR)/log.c $(SRC_DIR)/viewer_brick_batch_stub.c

VIEWER_EM_FLAGS = -sUSE_WEBGL2=1 \
                  -sFULL_ES3=1 \
                  -sALLOW_MEMORY_GROWTH=1 \
                  -sEXPORTED_FUNCTIONS='["_main","_resize_canvas","_viewer_rotate","_viewer_zoom","_viewer_set_color","_viewer_set_mesh_flags","_viewer_load_slot_texture","_viewer_load_accessory","_viewer_unload_accessory","_viewer_set_accessories"]' \
                  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","stringToUTF8","lengthBytesUTF8"]' \
                  --shell-file $(WEB_DIR)/viewer_shell.html \
                  --preload-file $(ASSETS)@assets \
                  $(ODE_INC)

viewer: $(OUT_DIR)/avatar_viewer.html

$(OUT_DIR)/avatar_viewer.html: $(VIEWER_SRCS) $(WEB_DIR)/viewer_shell.html | $(OUT_DIR)
	$(EMCC) $(WASM_CFLAGS) $(VIEWER_EM_FLAGS) $(VIEWER_SRCS) $(ODE_LIB_WASM) -o $@

clean:
	rm -f $(OUT_DIR)/polyworld $(OUT_DIR)/polyworld_vr $(OUT_DIR)/pw_debug \
	      $(OUT_DIR)/vidpolyworld $(OUT_DIR)/polyworld.exe \
	      $(OUT_DIR)/polyworld.html $(OUT_DIR)/polyworld.js $(OUT_DIR)/polyworld.wasm $(OUT_DIR)/polyworld.data \
	      $(OUT_DIR)/avatar_viewer.html $(OUT_DIR)/avatar_viewer.js $(OUT_DIR)/avatar_viewer.wasm $(OUT_DIR)/avatar_viewer.data

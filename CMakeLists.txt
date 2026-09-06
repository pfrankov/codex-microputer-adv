cmake_minimum_required(VERSION 3.16)

# Single source of truth for the firmware version, read back in main.cpp
# through esp_app_get_description(); bump together with CHANGELOG.md.
set(PROJECT_VER "0.10.4")

if(NOT DEFINED ENV{M5CARDPUTER_DEMO_PATH})
    message(FATAL_ERROR "M5CARDPUTER_DEMO_PATH is not set; run ./tools/setup.sh")
endif()

set(EXTRA_COMPONENT_DIRS
    "$ENV{M5CARDPUTER_DEMO_PATH}/components/M5Unified"
    "$ENV{M5CARDPUTER_DEMO_PATH}/components/M5GFX"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(codex_microputer_adv)

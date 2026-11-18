cmake_minimum_required(VERSION 3.28)
project(poher VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The code has no C++ modules: skip the scan for them, which only slows the build down.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Builds that are sent to friends must run without anything installed.
option(POHER_STATIC "Link the programs statically" OFF)
if(POHER_STATIC)
  # Makes the library search below pick libsodium.a instead of the shared one.
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

# ---- Tox: only the core library, built from source, static -------------------

set(BUILD_TOXAV OFF CACHE BOOL "" FORCE)
set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
set(DHT_BOOTSTRAP OFF CACHE BOOL "" FORCE)
set(BOOTSTRAP_DAEMON OFF CACHE BOOL "" FORCE)
set(UNITTEST OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(toxcore
  URL https://github.com/TokTok/c-toxcore/releases/download/v0.2.24/c-toxcore-v0.2.24.tar.gz
  URL_HASH SHA256=c07658e6f4d195b659941aa8dcc20a8499435e6cdfd036bbb3f2ab2cbf03bd7c
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(toxcore)

# ---- FTXUI: the terminal (keys, window size, colours) -------------------------

set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_QUIET ON CACHE BOOL "" FORCE)

FetchContent_Declare(ftxui
  URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v7.0.3.tar.gz
  URL_HASH SHA256=e7c62ffe19009759821b4f0f8df7f2a6fb83784c3a9f1477d81f56d3ee723c88
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(ftxui)

find_package(Threads REQUIRED)

# ---- Our code ----------------------------------------------------------------

add_library(poher_options INTERFACE)
if(MSVC)
  target_compile_options(poher_options INTERFACE /W4 /utf-8)
else()
  target_compile_options(poher_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

# The rules of the game: cards, hand strength, one hand from blinds to payout.
add_library(poher_game STATIC
  src/game/cards.cpp
  src/game/equity.cpp
  src/game/hand.cpp
  src/game/hand_value.cpp
  src/game/outs.cpp
  src/game/table.cpp)
target_include_directories(poher_game PUBLIC src)
target_link_libraries(poher_game PRIVATE poher_options)

# The computer players.
add_library(poher_bots STATIC
  src/bots/brain.cpp
  src/bots/characters.cpp
  src/bots/preflop.cpp
  src/bots/ranges.cpp
  src/bots/simple_bot.cpp
  src/bots/talk.cpp
  src/bots/watch.cpp)
target_link_libraries(poher_bots PUBLIC poher_game PRIVATE poher_options)

# The host of a table: deals, paces the game, lets the bots move.
add_library(poher_host STATIC src/host/dealer.cpp)
target_link_libraries(poher_host PUBLIC poher_game poher_bots PRIVATE poher_options)

# The picture reader (third_party/stb), compiled apart so that its warnings stay out of ours.
add_library(poher_stb STATIC src/ui/stb.cpp)
target_include_directories(poher_stb PUBLIC src)
target_include_directories(poher_stb SYSTEM PUBLIC third_party/stb)

# What the game looks like: drawn on a grid of cells, independent of the terminal.
add_library(poher_ui STATIC
  src/ui/avatar.cpp
  src/ui/canvas.cpp
  src/ui/card_art.cpp
  src/ui/image.cpp
  src/ui/lang.cpp
  src/ui/table_draw.cpp)
target_link_libraries(poher_ui PUBLIC poher_game poher_host PRIVATE poher_options poher_stb)

# Playing with friends: the messages, the host's room and the guest's table.
# It sees the network only through src/net/network.hpp; Tox is plugged in by the program.
add_library(poher_online STATIC
  src/net/loop_network.cpp
  src/online/fair_deck.cpp
  src/online/guest_table.cpp
  src/online/host_room.cpp
  src/online/online.cpp
  src/online/protocol.cpp
  src/online/sha256.cpp)
target_include_directories(poher_online PUBLIC src)
target_link_libraries(poher_online PUBLIC poher_host PRIVATE poher_options)

# The game as the player sees it: menus, the table, the keys.
add_library(poher_app STATIC src/app/game.cpp src/app/game_friends.cpp src/app/paths.cpp src/app/profile.cpp)
target_link_libraries(poher_app PUBLIC poher_ui poher_host poher_online PRIVATE poher_options)

add_executable(poher src/app/main.cpp)
target_link_libraries(poher PRIVATE poher_options poher_app poher_net ftxui::component ftxui::dom ftxui::screen Threads::Threads)
if(POHER_STATIC AND NOT APPLE)
  target_link_options(poher PRIVATE -static)
endif()

# Makes the table of starting hands for the bots (see the file for how to run it).
add_executable(poher-preflop EXCLUDE_FROM_ALL tools/preflop_table.cpp)
target_link_libraries(poher-preflop PRIVATE poher_options poher_game)

# Bots against bots without pauses: `build/poher-sim 20000` (development only).
add_executable(poher-sim EXCLUDE_FROM_ALL tools/sim.cpp)
target_link_libraries(poher-sim PRIVATE poher_options poher_host poher_ui)

# Pictures of the screens without a terminal, for checking the looks while developing.
add_executable(poher-shot tools/shot.cpp)
target_link_libraries(poher-shot PRIVATE poher_options poher_app)

# The network layer. The rest of the program sees only src/net/network.hpp.
add_library(poher_net STATIC src/net/tox_network.cpp)
target_include_directories(poher_net PUBLIC src)
target_include_directories(poher_net SYSTEM PRIVATE ${toxcore_SOURCE_DIR})
target_link_libraries(poher_net PRIVATE poher_options toxcore_static)

# Network check: a small program friends run to see whether they can reach each other.
add_executable(poher-nettest src/nettest/main.cpp)
target_link_libraries(poher-nettest PRIVATE poher_options poher_net Threads::Threads)
if(POHER_STATIC AND NOT APPLE)
  target_link_options(poher-nettest PRIVATE -static)
endif()

# ---- Tests -------------------------------------------------------------------

enable_testing()
add_executable(poher-tests
  tests/main.cpp
  tests/bots_test.cpp
  tests/dealer_test.cpp
  tests/friends_test.cpp
  tests/hand_test.cpp
  tests/hand_value_test.cpp
  tests/online_test.cpp
  tests/profile_test.cpp
  tests/screen_test.cpp
  tests/table_test.cpp)
target_link_libraries(poher-tests PRIVATE poher_options poher_game poher_bots poher_host poher_online poher_app)
target_compile_definitions(poher-tests PRIVATE POHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME rules COMMAND poher-tests)

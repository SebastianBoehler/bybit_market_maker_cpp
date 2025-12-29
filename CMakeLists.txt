cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)
project(bybit_market_maker_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Allow nested dependencies that pin old policies (e.g., nlohmann_json) to configure.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# Disable upstream tests from bybit-cpp-client to speed up and avoid extra executables.
set(BYBIT_BUILD_TESTS OFF CACHE BOOL "Disable bybit-cpp-client tests" FORCE)
# Enable websocket support in bybit-cpp-client (requires ixwebsocket via FetchContent there).
set(BYBIT_ENABLE_WEBSOCKET ON CACHE BOOL "Enable websocket client" FORCE)

FetchContent_Declare(
  bybit_cpp_client
  GIT_REPOSITORY https://github.com/SebastianBoehler/bybit-cpp-client.git
  GIT_TAG main
)
FetchContent_MakeAvailable(bybit_cpp_client)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)

# --- Libraries ---
add_library(trading_helper
  src/trading_helper.cpp
)
target_include_directories(trading_helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(trading_helper PUBLIC bybit_client nlohmann_json::nlohmann_json)

add_library(ws_helper
  src/ws_helper.cpp
)
target_include_directories(ws_helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ws_helper PUBLIC bybit_client)

add_library(market_data_feed
  src/market_data_feed.cpp
)
target_include_directories(market_data_feed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(market_data_feed PUBLIC ws_helper nlohmann_json::nlohmann_json)

add_library(strategy
  src/strategy.cpp
)
target_include_directories(strategy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(strategy PUBLIC trading_helper)

# --- Executable ---
add_executable(market_maker_example src/main.cpp)
target_link_libraries(market_maker_example PRIVATE strategy trading_helper market_data_feed)

# --- Tests ---
enable_testing()
add_executable(live_data_smoke tests/live_data_smoke.cpp)
target_link_libraries(live_data_smoke PRIVATE trading_helper Catch2::Catch2WithMain)
add_test(NAME live_data_smoke COMMAND live_data_smoke)

add_executable(ws_live_smoke tests/ws_live_smoke.cpp)
target_link_libraries(ws_live_smoke PRIVATE ws_helper Catch2::Catch2WithMain)
add_test(NAME ws_live_smoke COMMAND ws_live_smoke)

add_executable(ws_feed_smoke tests/ws_feed_smoke.cpp)
target_link_libraries(ws_feed_smoke PRIVATE market_data_feed Catch2::Catch2WithMain)
add_test(NAME ws_feed_smoke COMMAND ws_feed_smoke)

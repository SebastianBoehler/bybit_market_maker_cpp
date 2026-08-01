cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
project(bybit_market_maker_cpp VERSION 0.1.0 LANGUAGES CXX)

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
  GIT_TAG 1120107d3981cb215d83255139d45174055c07cb
)
FetchContent_MakeAvailable(bybit_cpp_client)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
  URL_HASH SHA256=8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.4.tar.gz
  URL_HASH SHA256=b7754b711242c167d8f60b890695347f90a1ebc95949a045385114165d606dbb
)
FetchContent_MakeAvailable(Catch2)

# --- Libraries ---
add_library(json_value
  src/json_value.cpp
)
target_include_directories(json_value PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(json_value PUBLIC nlohmann_json::nlohmann_json)

add_library(trading_helper
  src/bybit_response.cpp
  src/trading_helper.cpp
)
target_include_directories(trading_helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(trading_helper PUBLIC bybit_client json_value nlohmann_json::nlohmann_json)

add_library(ws_helper
  src/ws_helper.cpp
)
target_include_directories(ws_helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ws_helper PUBLIC bybit_client)

add_library(market_data_feed
  src/market_state.cpp
  src/market_data_feed.cpp
)
target_include_directories(market_data_feed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(market_data_feed PUBLIC ws_helper json_value nlohmann_json::nlohmann_json)

add_library(app_config
  src/app_config.cpp
)
target_include_directories(app_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(strategy
  src/account_state.cpp
  src/decimal.cpp
  src/market_snapshot.cpp
  src/order_batch.cpp
  src/order_reconciler.cpp
  src/private_state.cpp
  src/private_state_access.cpp
  src/private_position.cpp
  src/strategy_runtime.cpp
  src/strategy_runtime_orders.cpp
  src/strategy_plan.cpp
  src/strategy.cpp
  src/long_only_strategy.cpp
)
target_include_directories(strategy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(strategy PUBLIC trading_helper json_value)

add_library(private_session
  src/private_session.cpp
)
target_include_directories(private_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(private_session PUBLIC strategy bybit_client nlohmann_json::nlohmann_json)

add_library(app_runtime
  src/dcp_state.cpp
  src/market_maker_app.cpp
)
target_include_directories(app_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(app_runtime PUBLIC app_config strategy trading_helper market_data_feed private_session)

# --- Executable ---
add_executable(market_maker_example src/main.cpp)
target_link_libraries(market_maker_example PRIVATE strategy trading_helper market_data_feed app_runtime private_session)
add_executable(market_maker_long_only src/main.cpp)
target_compile_definitions(market_maker_long_only PRIVATE DEFAULT_SIDE_MODE="long_only")
target_link_libraries(market_maker_long_only PRIVATE strategy trading_helper market_data_feed app_runtime private_session)

# --- Tests ---
enable_testing()
option(BYBIT_MM_NETWORK_TESTS "Register live Bybit network smoke tests with CTest" OFF)
add_executable(live_data_smoke tests/live_data_smoke.cpp)
target_link_libraries(live_data_smoke PRIVATE trading_helper Catch2::Catch2WithMain)

add_executable(ws_live_smoke tests/ws_live_smoke.cpp)
target_link_libraries(ws_live_smoke PRIVATE ws_helper nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

add_executable(ws_feed_smoke tests/ws_feed_smoke.cpp)
target_link_libraries(ws_feed_smoke PRIVATE market_data_feed Catch2::Catch2WithMain)

if(BYBIT_MM_NETWORK_TESTS)
  add_test(NAME live_data_smoke COMMAND live_data_smoke)
  add_test(NAME ws_live_smoke COMMAND ws_live_smoke)
  add_test(NAME ws_feed_smoke COMMAND ws_feed_smoke)
endif()

add_executable(strategy_plan_tests tests/strategy_plan_tests.cpp)
target_link_libraries(strategy_plan_tests PRIVATE strategy Catch2::Catch2WithMain)
add_test(NAME strategy_plan_tests COMMAND strategy_plan_tests)

add_executable(strategy_capacity_tests tests/strategy_capacity_tests.cpp)
target_link_libraries(strategy_capacity_tests PRIVATE strategy Catch2::Catch2WithMain)
add_test(NAME strategy_capacity_tests COMMAND strategy_capacity_tests)

add_executable(order_runtime_tests tests/order_runtime_tests.cpp)
target_link_libraries(order_runtime_tests PRIVATE strategy Catch2::Catch2WithMain)
add_test(NAME order_runtime_tests COMMAND order_runtime_tests)

add_executable(order_truth_tests tests/order_truth_tests.cpp)
target_link_libraries(order_truth_tests PRIVATE strategy Catch2::Catch2WithMain)
add_test(NAME order_truth_tests COMMAND order_truth_tests)

add_executable(bybit_response_tests tests/bybit_response_tests.cpp)
target_link_libraries(bybit_response_tests PRIVATE trading_helper Catch2::Catch2WithMain)
add_test(NAME bybit_response_tests COMMAND bybit_response_tests)

add_executable(market_state_tests tests/market_state_tests.cpp)
target_link_libraries(market_state_tests PRIVATE market_data_feed Catch2::Catch2WithMain)
add_test(NAME market_state_tests COMMAND market_state_tests)

add_executable(account_state_tests tests/account_state_tests.cpp)
target_link_libraries(account_state_tests PRIVATE strategy Catch2::Catch2WithMain)
add_test(NAME account_state_tests COMMAND account_state_tests)

add_executable(private_state_tests tests/private_state_tests.cpp)
target_link_libraries(private_state_tests PRIVATE strategy Catch2::Catch2WithMain)
add_test(NAME private_state_tests COMMAND private_state_tests)

add_executable(app_config_tests tests/app_config_tests.cpp)
target_link_libraries(app_config_tests PRIVATE app_config Catch2::Catch2WithMain)
add_test(NAME app_config_tests COMMAND app_config_tests)

add_executable(order_batch_tests tests/order_batch_tests.cpp)
target_link_libraries(order_batch_tests PRIVATE strategy Catch2::Catch2WithMain)
add_test(NAME order_batch_tests COMMAND order_batch_tests)

add_executable(dcp_state_tests tests/dcp_state_tests.cpp)
target_link_libraries(dcp_state_tests PRIVATE app_runtime Catch2::Catch2WithMain)
add_test(NAME dcp_state_tests COMMAND dcp_state_tests)

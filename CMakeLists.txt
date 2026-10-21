cmake_minimum_required(VERSION 3.21)
project(clint_irc VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

set(IRC_SOURCES
    src/irc/parser.cpp
    src/irc/encoder.cpp
    src/irc/ctcp.cpp
    src/irc/endpoint.cpp
    src/irc/connection.cpp
)

set(UI_SOURCES
    src/ui/chat_formatter.cpp
    src/ui/channel_widget.cpp
    src/ui/connect_dialog.cpp
    src/ui/fun_actions.cpp
    src/ui/history_line_edit.cpp
    src/ui/main_window.cpp
)

set(APP_SOURCES
    src/app/client_controller.cpp
    src/main.cpp
)

set(HEADERS
    src/irc/parser.hpp
    src/irc/encoder.hpp
    src/irc/ctcp.hpp
    src/irc/prefix.hpp
    src/irc/endpoint.hpp
    src/irc/connection.hpp
    src/irc/connection_config.hpp
    src/irc/message.hpp
    src/irc/result.hpp
    src/ui/chat_formatter.hpp
    src/ui/channel_widget.hpp
    src/ui/completion.hpp
    src/ui/connect_dialog.hpp
    src/ui/fun_actions.hpp
    src/ui/history_line_edit.hpp
    src/ui/input_history.hpp
    src/ui/main_window.hpp
    src/app/client_controller.hpp
)

add_executable(clint
    ${IRC_SOURCES}
    ${UI_SOURCES}
    ${APP_SOURCES}
    ${HEADERS}
)

target_include_directories(clint PRIVATE src)
target_link_libraries(clint PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)

# Pure-logic selftest (protocol + formatter + prefix + endpoint + history; no GUI).
add_executable(clint_selftest
    tests/test_pure.cpp
    src/irc/parser.cpp
    src/irc/encoder.cpp
    src/irc/ctcp.cpp
    src/irc/endpoint.cpp
    src/ui/chat_formatter.cpp
    src/ui/fun_actions.cpp
)

target_include_directories(clint_selftest PRIVATE src)
# Qt6::Network for QHostAddress (endpoint parsing of literal addresses).
target_link_libraries(clint_selftest PRIVATE Qt6::Core Qt6::Network)

# Settings persistence round-trip test (needs Qt Widgets; runs offscreen).
add_executable(clint_settings_test
    tests/test_settings.cpp
    src/ui/connect_dialog.cpp
    src/irc/endpoint.cpp
)
target_include_directories(clint_settings_test PRIVATE src)
target_link_libraries(clint_settings_test PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)

# Tab nick-completion test (needs Qt Widgets; runs offscreen).
add_executable(clint_completion_test
    tests/test_completion.cpp
    src/ui/history_line_edit.cpp
)
target_include_directories(clint_completion_test PRIVATE src)
target_link_libraries(clint_completion_test PRIVATE Qt6::Core Qt6::Widgets)

# Chat-view click semantics test: single click inserts a nick, double click
# emits nickActivated (needs Qt Widgets; runs offscreen).
add_executable(clint_nick_click_test
    tests/test_nick_click.cpp
    src/ui/channel_widget.cpp
    src/ui/history_line_edit.cpp
)
target_include_directories(clint_nick_click_test PRIVATE src)
target_link_libraries(clint_nick_click_test PRIVATE Qt6::Core Qt6::Widgets)

# Nick-list rebuild test: the selected nick and the scroll position survive the
# full rebuild that every join/part/quit triggers (needs Qt Widgets; offscreen).
add_executable(clint_nick_list_test
    tests/test_nick_list_scroll.cpp
    src/ui/channel_widget.cpp
    src/ui/history_line_edit.cpp
)
target_include_directories(clint_nick_list_test PRIVATE src)
target_link_libraries(clint_nick_list_test PRIVATE Qt6::Core Qt6::Widgets)

# Channel-tab extras test: the topic bar shows/hides with the topic, and a
# click on a "join:" anchor emits channelActivated (needs Qt Widgets; offscreen).
add_executable(clint_channel_extras_test
    tests/test_channel_extras.cpp
    src/ui/channel_widget.cpp
    src/ui/history_line_edit.cpp
)
target_include_directories(clint_channel_extras_test PRIVATE src)
target_link_libraries(clint_channel_extras_test PRIVATE Qt6::Core Qt6::Widgets)

# Session-layer test: ClientController against a fake in-process IRC server
# (topic tracking, MODE prefix updates, long-message splitting, 433 handling;
# Qt Core + Network, no GUI).
add_executable(clint_controller_test
    tests/test_client_controller.cpp
    src/app/client_controller.cpp
    src/irc/parser.cpp
    src/irc/encoder.cpp
    src/irc/ctcp.cpp
    src/irc/endpoint.cpp
    src/irc/connection.cpp
    src/ui/chat_formatter.cpp
)
target_include_directories(clint_controller_test PRIVATE src)
target_link_libraries(clint_controller_test PRIVATE Qt6::Core Qt6::Network)

# Connection-layer test: endpoint parsing + resolution + IPv6/family behaviour
# against real loopback servers (needs Qt Network; no GUI).
add_executable(clint_connect_test
    tests/test_connection.cpp
    src/irc/connection.cpp
    src/irc/endpoint.cpp
    src/irc/encoder.cpp
    src/irc/parser.cpp
)
target_include_directories(clint_connect_test PRIVATE src)
target_link_libraries(clint_connect_test PRIVATE Qt6::Core Qt6::Network)

enable_testing()
add_test(NAME clint_selftest COMMAND clint_selftest)
add_test(NAME clint_settings_test COMMAND clint_settings_test)
add_test(NAME clint_completion_test COMMAND clint_completion_test)
add_test(NAME clint_nick_click_test COMMAND clint_nick_click_test)
add_test(NAME clint_nick_list_test COMMAND clint_nick_list_test)
add_test(NAME clint_channel_extras_test COMMAND clint_channel_extras_test)
add_test(NAME clint_controller_test COMMAND clint_controller_test)
add_test(NAME clint_connect_test COMMAND clint_connect_test)
set_tests_properties(clint_settings_test clint_completion_test clint_nick_click_test
    clint_nick_list_test clint_channel_extras_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

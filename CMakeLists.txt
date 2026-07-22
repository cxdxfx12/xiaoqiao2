cmake_minimum_required(VERSION 3.20)
project(xiaoqiao_freight VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql Concurrent Network)

find_library(DUCKDB_LIBRARY duckdb REQUIRED)
find_path(DUCKDB_INCLUDE_DIR duckdb.hpp REQUIRED)

set(PROJECT_SOURCES
    src/main.cpp
    src/core/app_config.hpp
    src/core/app_config.cpp
    src/core/freight_types.hpp
    src/db/duckdb_manager.hpp
    src/db/duckdb_manager.cpp
    src/db/sqlite_rule_repository.hpp
    src/db/sqlite_rule_repository.cpp
    src/services/rule_service.hpp
    src/services/rule_service.cpp
    src/services/calc_service.hpp
    src/services/calc_service.cpp
    src/services/history_service.hpp
    src/services/history_service.cpp
    src/ui/main_window.hpp
    src/ui/main_window.cpp
    src/ui/icon_manager.hpp
    src/ui/icon_manager.cpp
    src/ui/dialogs/single_calc_dialog.hpp
    src/ui/dialogs/single_calc_dialog.cpp
    src/ui/dialogs/batch_calc_dialog.hpp
    src/ui/dialogs/batch_calc_dialog.cpp
    src/ui/dialogs/rule_setting_dialog.hpp
    src/ui/dialogs/rule_setting_dialog.cpp
    src/ui/dialogs/template_edit_dialog.hpp
    src/ui/dialogs/template_edit_dialog.cpp
    src/ui/dialogs/customer_setting_dialog.hpp
    src/ui/dialogs/customer_setting_dialog.cpp
    src/ui/dialogs/system_setting_dialog.hpp
    src/ui/dialogs/system_setting_dialog.cpp
    src/ui/dialogs/about_dialog.hpp
    src/ui/dialogs/about_dialog.cpp
    src/ui/dialogs/history_dialog.hpp
    src/ui/dialogs/history_dialog.cpp
    src/ui/dialogs/compare_dialog.hpp
    src/ui/dialogs/compare_dialog.cpp
    src/ui/dialogs/header_mapping_dialog.hpp
    src/ui/dialogs/header_mapping_dialog.cpp
    resources/resources.qrc
)

add_executable(xiaoqiao_freight WIN32 MACOSX_BUNDLE ${PROJECT_SOURCES})

target_include_directories(xiaoqiao_freight PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${DUCKDB_INCLUDE_DIR}
)

target_link_libraries(xiaoqiao_freight PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    ${DUCKDB_LIBRARY}
)

if(APPLE)
    set_target_properties(xiaoqiao_freight PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "小乔运费结算"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hbdxm.xiaoqiao"
    )
endif()

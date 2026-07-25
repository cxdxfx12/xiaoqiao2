cmake_minimum_required(VERSION 3.20)
project(xiaoqiao_freight VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql Concurrent Network Charts PrintSupport)

find_library(DUCKDB_LIBRARY duckdb REQUIRED)
find_path(DUCKDB_INCLUDE_DIR duckdb.hpp REQUIRED)

set(PROJECT_SOURCES
    src/main.cpp
    src/core/app_config.hpp
    src/core/app_config.cpp
    src/core/freight_types.hpp
    src/core/license_manager.hpp
    src/core/license_manager.cpp
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
    src/services/dashboard_service.hpp
    src/services/dashboard_service.cpp
    src/services/template_recognizer.hpp
    src/services/template_recognizer.cpp
    src/ui/main_window.hpp
    src/ui/main_window.cpp
    src/ui/icon_manager.hpp
    src/ui/icon_manager.cpp
    src/ui/ux_helper.hpp
    src/ui/ux_helper.cpp
    src/ui/dialogs/single_calc_dialog.hpp
    src/ui/dialogs/single_calc_dialog.cpp
    src/ui/dialogs/batch_calc_dialog.hpp
    src/ui/dialogs/batch_calc_dialog.cpp
    src/ui/dialogs/rule_setting_dialog.hpp
    src/ui/dialogs/rule_setting_dialog.cpp
    src/ui/dialogs/template_edit_dialog.hpp
    src/ui/dialogs/template_edit_dialog.cpp
    src/ui/dialogs/courier_template_manager_dialog.hpp
    src/ui/dialogs/courier_template_manager_dialog.cpp
    src/ui/dialogs/courier_template_edit_dialog.hpp
    src/ui/dialogs/courier_template_edit_dialog.cpp
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
    src/ui/dialogs/fee_breakdown_dialog.hpp
    src/ui/dialogs/fee_breakdown_dialog.cpp
    src/ui/dialogs/dashboard_dialog.hpp
    src/ui/dialogs/dashboard_dialog.cpp
    src/ui/dialogs/diff_report_dialog.hpp
    src/ui/dialogs/diff_report_dialog.cpp
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
    Qt6::Charts
    Qt6::PrintSupport
    ${DUCKDB_LIBRARY}
)

if(APPLE)
    set_target_properties(xiaoqiao_freight PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "小乔运费结算"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hbdxm.xiaoqiao"
        MACOSX_BUNDLE_ICON_FILE "xiaoqiao.icns"
    )
    set(APP_ICON_FILE "${CMAKE_SOURCE_DIR}/resources/icons/xiaoqiao.icns")
    if(EXISTS ${APP_ICON_FILE})
        set_source_files_properties(${APP_ICON_FILE} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(xiaoqiao_freight PRIVATE ${APP_ICON_FILE})
    endif()

    set(DEPLOY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/deploy_mac.sh)
    if(EXISTS ${DEPLOY_SCRIPT})
        add_custom_command(TARGET xiaoqiao_freight POST_BUILD
            COMMAND bash ${DEPLOY_SCRIPT} $<TARGET_BUNDLE_DIR:xiaoqiao_freight>
            COMMENT "Deploying Qt frameworks and fixing signatures..."
        )
    endif()
endif()

# 授权生成器工具 (GUI版)
add_executable(license_generator MACOSX_BUNDLE
    tools/license_generator/main.cpp
    tools/license_generator/license_generator_widget.hpp
    tools/license_generator/license_generator_widget.cpp
    src/core/license_manager.hpp
    src/core/license_manager.cpp
)

target_include_directories(license_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(license_generator PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

if(APPLE)
    set_target_properties(license_generator PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "授权生成器"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hbdxm.license-generator"
    )

    set(LIC_DEPLOY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/deploy_mac.sh)
    if(EXISTS ${LIC_DEPLOY_SCRIPT})
        add_custom_command(TARGET license_generator POST_BUILD
            COMMAND bash ${LIC_DEPLOY_SCRIPT} $<TARGET_BUNDLE_DIR:license_generator>
            COMMENT "Deploying Qt frameworks for license generator..."
        )
    endif()
endif()

# 批量命令行计算工具（console 版，无 GUI）
add_executable(batch_runner
    tools/batch_runner/main.cpp
    src/core/app_config.hpp
    src/core/app_config.cpp
    src/core/freight_types.hpp
    src/core/license_manager.hpp
    src/core/license_manager.cpp
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
)
target_include_directories(batch_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${DUCKDB_INCLUDE_DIR}
)
target_link_libraries(batch_runner PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    ${DUCKDB_LIBRARY}
)
if(APPLE)
    set_target_properties(batch_runner PROPERTIES
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# T7 功能7 Headless 接口测试
add_executable(t7_test
    tools/t7_test/main.cpp
    src/core/app_config.hpp
    src/core/app_config.cpp
    src/core/freight_types.hpp
    src/core/license_manager.hpp
    src/core/license_manager.cpp
    src/services/template_recognizer.hpp
    src/services/template_recognizer.cpp
    src/services/calc_service.hpp
    src/services/calc_service.cpp
    src/services/rule_service.hpp
    src/services/rule_service.cpp
    src/services/history_service.hpp
    src/services/history_service.cpp
    src/db/sqlite_rule_repository.hpp
    src/db/sqlite_rule_repository.cpp
    src/db/duckdb_manager.hpp
    src/db/duckdb_manager.cpp
)
target_include_directories(t7_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${DUCKDB_INCLUDE_DIR}
)
target_link_libraries(t7_test PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    ${DUCKDB_LIBRARY}
)
if(APPLE)
    set_target_properties(t7_test PROPERTIES
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# Logo / App Icon 生成工具：输出 xiaoqiao.icns + png/logo_XX.png 到 resources/icons/
add_executable(gen_icon tools/gen_icon/main.cpp)
target_link_libraries(gen_icon PRIVATE Qt6::Core Qt6::Gui)
if(APPLE)
    set_target_properties(gen_icon PROPERTIES
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# 计费参数回归测试（console 版）
add_executable(billing_params_test
    tools/billing_params_test/main.cpp
    src/core/app_config.hpp
    src/core/app_config.cpp
    src/core/freight_types.hpp
    src/core/license_manager.hpp
    src/core/license_manager.cpp
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
)
target_include_directories(billing_params_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${DUCKDB_INCLUDE_DIR}
)
target_link_libraries(billing_params_test PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    ${DUCKDB_LIBRARY}
)
if(APPLE)
    set_target_properties(billing_params_test PROPERTIES
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

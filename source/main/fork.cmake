# 포크가 추가한 소스와 웹 파일. main/CMakeLists.txt(업스트림)에는 include 한 줄만 둔다.
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/boost.c"
    "${CMAKE_CURRENT_LIST_DIR}/boost_ctrl.c"
    "${CMAKE_CURRENT_LIST_DIR}/choc_map.c"
    "${CMAKE_CURRENT_LIST_DIR}/choc_exec.c"
    "${CMAKE_CURRENT_LIST_DIR}/fork_settings.c"
    "${CMAKE_CURRENT_LIST_DIR}/display_fork.c"
    "${CMAKE_CURRENT_LIST_DIR}/display_fork_widgets.c"
    "${CMAKE_CURRENT_LIST_DIR}/display_fork_setup.c"
    "${CMAKE_CURRENT_LIST_DIR}/display_fork_config.c"
    "${CMAKE_CURRENT_LIST_DIR}/usb_desc_info.c"
    "${CMAKE_CURRENT_LIST_DIR}/wifi_fork.c"
    "${CMAKE_CURRENT_LIST_DIR}/ota_update.c"
    "${CMAKE_CURRENT_LIST_DIR}/ota_tag_scan.c"
    "${CMAKE_CURRENT_LIST_DIR}/pitch_yin.c"
)

target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/web/fork.js" TEXT)

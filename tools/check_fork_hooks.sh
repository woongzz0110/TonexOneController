#!/bin/sh
# 업스트림 병합 뒤 포크 연결 지점(hook)이 업스트림 파일에 모두 남아 있는지 확인한다.
# 하나라도 빠지면 그 기능이 조용히 꺼지므로, 병합 직후 빌드 전에 돌린다. 목록 설명은 docs/UPSTREAM_SYNC.md.
cd "$(dirname "$0")/.." || exit 1

missing=0
check() {
    file="$1"; pattern="$2"; expected="${3:-1}"
    count=$(grep -cF -- "$pattern" "$file" 2>/dev/null || true)
    [ -z "$count" ] && count=0
    if [ "$count" -lt "$expected" ]; then
        echo "MISSING  $file: '$pattern' (found $count, expected $expected)"
        missing=$((missing + 1))
    fi
}

M=source/main
check $M/CMakeLists.txt         'include(${CMAKE_CURRENT_LIST_DIR}/fork.cmake)'
check $M/control.c              'fork_settings_load();'
check $M/control.c              'boost_ctrl_on_sync_complete();'
check $M/usb_comms.c            'boost_ctrl_on_usb_disconnected();'
check $M/usb_comms.c            'usb_desc_info_capture('
check $M/usb_tonex_one.c        'boost_ctrl_on_volume_report();'
check $M/usb_tonex_one_plus.c   'boost_ctrl_on_volume_report();'
check $M/usb_tonex_plug.c       'boost_ctrl_on_volume_report();'
check $M/usb_tonex.c            'boost_ctrl_on_volume_report();'
check $M/midi_control.c         'choc_exec_on_ble_connecting('
check $M/midi_control.c         'choc_exec_on_ble_disconnected();' 2
check $M/midi_control.c         'choc_exec_handle_ble_midi('
check $M/display.c              'display_fork_init();'
check $M/display.c              'display_fork_on_parameters_refreshed();'
check $M/display.c              'display_fork_handle_update('
check $M/display.c              'void UI_PostForkUpdate('
check $M/wifi_config.c          'wifi_fork_register_handlers('
check $M/wifi_config.c          'wifi_fork_handle_ws_command('
check $M/wifi_config.c          'wifi_fork_build_push('
check $M/wifi_config.c          'void wifi_request_fork_push('
check $M/wifi_config.c          'WIFI_FORK_URI_HANDLERS'
check $M/web/index.html         'src="fork.js"'

# 스킨 파티션은 업스트림(10M)보다 작다(OTA 슬롯 2개 때문에). 업스트림이 스킨을 키우면 여기서 걸린다.
skins_part=$(awk -F, '$1 ~ /^skins/ {gsub(/ /, "", $5); print $5}' source/partitions_16MB_ota.csv)
skins_file=$(wc -c < source/skin_images/16bit/skins.bin | tr -d ' ')
if [ -z "$skins_part" ] || [ "$skins_file" -gt $((skins_part)) ]; then
    echo "SKINS    source/skin_images/16bit/skins.bin ($skins_file bytes) does not fit the skins partition ($skins_part) in partitions_16MB_ota.csv"
    missing=$((missing + 1))
fi

if [ "$missing" -ne 0 ]; then
    echo "$missing problem(s) found"
    exit 1
fi

echo "all fork hooks present ($(git grep -c 'fork:' -- source/main ':!source/main/*fork*' 2>/dev/null | awk -F: '{s+=$2} END {print s}') 'fork:' markers in upstream files)"

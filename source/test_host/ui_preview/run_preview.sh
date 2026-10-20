#!/bin/sh
# 4.3B 보드 화면(메인 화면 배지·동작 알림, Setup 화면 탭들)을 호스트에서 그려 PNG로 남긴다.
# 사용: source/test_host/ui_preview/run_preview.sh [출력 폴더] [보드 빌드 폴더]
#   보드 빌드 폴더 기본값 source/build/fork_ws43b (먼저 IDF_PRESET=WS43B_FORK idf.py build 로 sdkconfig.h를 만들어 둔다)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SOURCE=$(cd "$HERE/../.." && pwd)
OUT=${1:-${TMPDIR:-/tmp}/tonex_ui_preview}
BUILD=${2:-$SOURCE/build/fork_ws43b}
CONFIG="$BUILD/config/sdkconfig.h"
OBJ="$OUT/obj"
CC=${CC:-cc}

[ -f "$CONFIG" ] || { echo "$CONFIG 가 없습니다. 먼저 보드 빌드를 하세요."; exit 1; }
mkdir -p "$OUT" "$OBJ"

LVGL="$SOURCE/components/lvgl__lvgl"
UI="$SOURCE/main/ui_generated_800x480land"
CFLAGS="-std=gnu11 -O1 -w -I$HERE/stubs -I$LVGL -I$UI -I$SOURCE/main -I$SOURCE/managed_components/espressif__usb/include \
 -DSDKCONFIG_REAL=\"$CONFIG\" -DLV_CONF_KCONFIG_EXTERNAL_INCLUDE=\"sdkconfig.h\""

# 생성 UI의 이벤트 핸들러는 화면을 그리는 데 필요 없으므로 빈 함수로 채운다
sed -n 's/^extern void \(action_[a-z_0-9]*\)(lv_event_t \* e);/void \1(lv_event_t * e) { (void)e; }/p' "$UI/actions.h" > "$OBJ/actions_stub.c"
printf '#include "lvgl.h"\n' | cat - "$OBJ/actions_stub.c" > "$OBJ/actions_stub_full.c"

# LVGL은 한 번 빌드해 두고 재사용한다
if [ ! -f "$OBJ/liblvgl.a" ] || [ "$CONFIG" -nt "$OBJ/liblvgl.a" ]; then
    echo "building LVGL..."
    rm -rf "$OBJ/lvgl" && mkdir -p "$OBJ/lvgl"
    count=0
    for f in $(find "$LVGL/src" -name '*.c'); do
        o="$OBJ/lvgl/$(echo "$f" | sed "s|$LVGL/src/||; s|/|_|g").o"
        $CC $CFLAGS -c "$f" -o "$o" &
        count=$((count + 1))
        [ $((count % 4)) -eq 0 ] && wait
    done
    wait
    ar rcs "$OBJ/liblvgl.a" "$OBJ"/lvgl/*.o
fi

echo "building preview..."
$CC $CFLAGS -o "$OBJ/preview" \
    "$HERE/preview_main.c" "$HERE/stubs.c" "$OBJ/actions_stub_full.c" \
    "$SOURCE/main"/display_fork*.c "$SOURCE/main/usb_desc_info.c" "$SOURCE/main/boost.c" "$SOURCE/main/choc_map.c" \
    "$UI"/*.c "$OBJ/liblvgl.a" -lz -lm

"$OBJ/preview" "$OUT"

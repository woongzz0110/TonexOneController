#!/bin/sh
# 펌웨어의 하드웨어 무관 규칙(초콜릿 판별, 볼륨 부스트, OTA 태그 검색, 음정 검출)을 호스트 컴파일러로 검사한다.
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/tonex_host_tests"
mkdir -p "$OUT"
CC="${CC:-cc}"
FLAGS="-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../main"

run() {
    name="$1"; shift
    $CC $FLAGS "$@" -lm -o "$OUT/$name"
    printf '%s: ' "$name"
    "$OUT/$name"
}

run test_choc_boost test_choc_boost.c ../main/choc_map.c ../main/boost.c
run test_ota_tag_scan test_ota_tag_scan.c ../main/ota_tag_scan.c
if [ -f test_pitch_yin.c ]; then
    run test_pitch_yin test_pitch_yin.c ../main/pitch_yin.c
fi

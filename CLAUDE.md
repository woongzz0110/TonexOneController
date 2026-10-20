# TonexOneController 개인 포크

Builty/TonexOneController 포크. 원조 TONEX One(ONE+ 아님) + Waveshare ESP32-S3-Touch-LCD-4.3B + M-Vave Chocolate 조합 전용으로 기능을 추가한다.
업스트림으로 PR을 보내지 않는다. 대신 업스트림 갱신을 계속 이어받는다(`docs/UPSTREAM_SYNC.md`).

작업 계획과 요구사항은 `docs/FIRMWARE_PLAN.md`, 구현 설계·현황은 `docs/IMPLEMENTATION_NOTES.md`에 있다. 작업 전에 반드시 읽는다.

## 빌드

- ESP-IDF **v6.0.2** (업스트림 CI 기준. `FirmwareDevelopment.md`의 5.x 표기는 오래된 내용)
- 실제 보드에 올리는 프리셋: **`WS43B_FORK`** (`source/CMakeUserPresets.json`) = 업스트림 `WS43B` 설정 + `sdkconfig.fork.ws43B`(OTA 파티션·Wi-Fi 업데이트)
- 업스트림 프리셋 `WS43B`도 계속 빌드돼야 한다(OTA만 빠지고 나머지 포크 기능은 같다).

```bash
cd source
IDF_PRESET=WS43B_FORK idf.py build     # 결과: source/build/fork_ws43b/
IDF_PRESET=WS43B idf.py build          # 업스트림 프리셋 확인용: source/build_ws43b/
python3 ../tools/flash_fork_ws43b.py /dev/cu.usbmodemXXXX   # USB 설치 파이썬 도구 (보드 Boot 버튼을 누른 채 연결)
python3 ../tools/web_installer/build_site.py build/fork_ws43b /tmp/tonex_site   # 크롬 설치 페이지를 로컬로 (python3 -m http.server -d /tmp/tonex_site 8000)
./test_host/run_tests.sh               # 하드웨어 없이 도는 규칙 테스트
./test_host/ui_preview/run_preview.sh [출력 폴더]   # 4.3B 화면(메인 화면 배지·알림, Setup 화면 탭)을 PNG로 (WS43B_FORK 빌드 뒤)
```

- 펌웨어 업데이트 방법(USB 첫 설치, 이후 Wi-Fi): `docs/FIRMWARE_UPDATE.md`. USB 설치는 크롬 설치 페이지(https://woongzz0110.github.io/TonexOneController/)가 우선이고,
  이 페이지는 main 푸시 때 `fork-ws43b.yaml`이 `tools/web_installer/`로 만들어 GitHub Pages에 올린다(루트 `index.html`은 업스트림 설치 페이지라 고치지 않는다).
- 맥 USB 전원으로만 켜면 4.3B 화면이 깨진다. 동작 확인은 9V(1A 이상) 전원으로 한다.
- 보드 USB-C는 하나뿐이고 평소엔 TONEX One에 연결된다. TONEX One과 연결된 상태에서는 시리얼 로그를 볼 수 없으므로, 디버그 정보는 화면(토스트/라벨/Setup 화면 Info 탭)이나 웹에 띄운다.

## 코드 구조에서 지킬 것

- **업스트림 파일은 연결(hook)만 고친다.** 기능 코드는 포크 전용 새 파일에 둔다. 업스트림 파일에 넣는 줄에는 `fork:` 주석을 붙인다
  (`git grep -n "fork:" -- source` 로 전부 찾을 수 있어야 한다). 연결 지점 목록은 `docs/UPSTREAM_SYNC.md`.
- 포크 파일: `boost.c`(부스트 규칙) · `boost_ctrl.c`(부스트 실행) · `choc_map.c`(초콜릿 신호 판별) · `choc_hold.c`(길게 누름 규칙) · `choc_exec.c`(동작 실행) ·
  `fork_settings.c`(포크 설정 저장) · `display_fork.c`(배지·토스트·Setup 화면 여는 자리) · `display_fork_setup.c`(Setup 화면 Boost·Choc·Info 탭) ·
  `display_fork_config.c`(Setup 화면의 업스트림 설정 탭) · `display_fork_widgets.c`(포크 화면 공용 위젯) · `wifi_fork.c`(웹 명령) · `ota_update.c`/`ota_tag_scan.c`(Wi-Fi 업데이트) ·
  `usb_desc_info.c`(USB 디스크립터 요약) · `pitch_yin.c`(음정 검출 엔진) · `web/fork.js`(웹 페이지) · `fork.cmake` · `components/tonex_fork_config/Kconfig` ·
  튜너: `tuner_logic.c`(출처 고르기·페달 후보 시험 판단, 순수) · `tuner_ctrl.c`(실행·기록·진단) · `tuner_audio.c`(USB 녹음 수신) · `tuner_analyzer.c`/`uac_stream.c`/`tuner_proto.c`(순수 계층) ·
  `display_fork_tuner.c`(튜너 화면 감지·아이콘·출처 표시·Setup Tuner 탭)
- 초콜릿 신호 번역은 `midi_control.c`의 BLE Central 수신 경로에서만 한다. `midi_helper*.c`의 CC/PC 의미는 바꾸지 않는다.
- 번역된 동작은 CC로 다시 만들어 넘기지 않고, 기존 control/usb 함수를 직접 호출한다.
- 포크 설정은 업스트림 `control.c` 구조체가 아니라 `fork_settings.c`가 자체 NVS 키(`boostconf`, `chocconf`, `tunerconf`)에 저장한다(병합 충돌 방지).
  구조체를 바꿀 때는 뒤에만 필드를 추가하고 `Version`을 올린다(`docs/IMPLEMENTATION_NOTES.md` 설정 저장 포맷).
- 4.3B 화면 추가 요소는 EEZ 생성 코드(`ui_generated_800x480land/`)나 `.eez-project`를 고치지 않고 `display_fork*.c`가 실행 중에 LVGL 객체로 만든다
  (업스트림이 EEZ 프로젝트를 다시 생성해도 충돌이 없다). 업스트림 화면(탭뷰 등) 안에는 넣지 않고 포크 Setup 화면에 둔다. 보드 폰트에 한글이 없으므로 보드 화면 문구는 영어로 쓴다.
- 보드에서 업스트림 컨트롤러 설정을 바꿀 때는 업스트림 공개 함수 `control_get/set_config_item_int()`만 쓰고, 웹처럼 모아서 저장·재부팅한다.
- 웹 UI는 `web/fork.js`가 업스트림 `index.html`/`script.js`에 덧붙인다(업스트림 `script.js`는 고치지 않는다). 서버 쪽은 `wifi_fork.c`.
- 다른 하드웨어 플랫폼 빌드를 깨뜨리지 않는다. 4.3B 전용 코드는 `CONFIG_TONEX_FORK_UI_800X480`, 튜너는 `CONFIG_TONEX_FORK_TUNER`, OTA는 `CONFIG_TONEX_FORK_OTA_UPDATE` 아래에 둔다.
- `main`은 `-Werror`로 빌드된다(경고도 실패). 블록 주석 안에 `/*`를 쓰지 않는다.

## 작업 순서와 현황

1. 볼륨 부스트 설정·상태 판정 (계획서 2-2) — 구현됨
2. 초콜릿 번역 계층 + 웹 설정 페이지 (2, 2-1) — 구현됨(상단 메뉴 Boost, Config > Chocolate), 초콜릿 실물 신호 확인 필요
3. 메인 화면 BOOST 배지·볼륨 표시 (3) — 구현됨
4. 보드 화면 설정 (4-2) — 구현됨(상단 상태 아이콘으로 여는 Setup 화면: Boost·Choc·Tuner·BT·MIDI·Misc·Wi-Fi·Info)
5. 튜너 (1) — 구현됨: 페달 튜너 메시지(One 번호 체계 후보) → USB 녹음(UAC1/UAC2) 순으로 되는 쪽을 쓴다. 둘 다 실물 미확인(구현 노트 3-5, 5장)
6. Wi-Fi 펌웨어 업데이트 — 구현됨(`WS43B_FORK`)

각 단계마다 `WS43B_FORK`와 `WS43B` 빌드, `test_host/run_tests.sh`가 통과해야 다음으로 넘어간다.

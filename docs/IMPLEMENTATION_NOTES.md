# 구현 노트

`docs/FIRMWARE_PLAN.md`(계획서)를 현재 코드에 대조한 결과와, 포크가 구현한 설계·설정 포맷·검증 방법을 정리한다.
기준: 업스트림 main `9fd1156` (V2.5.0.2 이후 26커밋, 웹 화면 표기 V3.0.0.2), ESP-IDF v6.0.2.

## 0. 현황

| 계획서 항목 | 상태 | 실물 확인 필요 |
| --- | --- | --- |
| 2-2 볼륨 부스트 (설정·상태 판정·토글·부팅 시 OFF) | 구현 | TONEX One 볼륨 왕복값(3장 참고) |
| 2 초콜릿 번역 계층 | 구현 | 초콜릿 신호(5장) |
| 2-1 웹 설정 페이지 | 구현 (상단 메뉴 Boost, Config > Chocolate) | — |
| 3 메인 화면 BOOST 배지·볼륨·동작 알림 | 구현 | 화면 배치 눈으로 확인 |
| 4-2 보드 화면 설정 | 구현 (Setup 화면: 상단 상태 아이콘을 누르면 열림. Boost·Choc·BT·MIDI·Misc·Wi-Fi·Info 탭) | 터치 조작감 |
| 1 튜너 | 1단계(USB 구성 화면/웹 덤프) + 음정 검출 엔진 | USB 구성 덤프 결과로 2단계 결정 |
| (추가) 펌웨어 업데이트 | Wi-Fi 업데이트 + USB 설치 도구 | 첫 USB 설치 1회 |

빌드 확인: `WS43B_FORK`, `WS43B`, `WS7B`, `Pirate43B`, `WS35B`, `JC3248W`, `PirateMaxV2`, `WS169`, `DEVKITC_N8R2`, `WSZERO` 통과.
`JC4880P4`(ESP32-P4)는 로컬에 RISC-V 툴체인이 없어 업스트림 CI로 확인한다. 하드웨어 없이 도는 규칙 테스트: `source/test_host/run_tests.sh`.

## 1. 계획서 대조 (기준 커밋 9fd1156의 위치)

| 계획서 표기 | 실제 위치 | 판정 |
| --- | --- | --- |
| `midi_control.c` 약 1082행 BLE Central 수신 | `gattc_profile_a_event_handler()`의 `ESP_GATTC_NOTIFY_EVT` (1077~1083), 호출은 1082행 | 일치 |
| 연결 기기가 `FootCtrl`/`FootCtrlPlus`인지 | 이름 목록 `InitDeviceList()` (1436~1468, 1445·1449행). 스캔 매칭 1340~1360 | **연결된 기기 이름을 저장하지 않음** → 포크가 연결 시점에 기록(`choc_exec_on_ble_connecting`) |
| `midi_helper_process_incoming_data()` | `midi_helper.c:144~260`. PC는 `enable_CC`와 무관하게 `control_get_pc_map()` → `control_request_preset_index()` (199~215), CC는 `midi_helper_adjust_param_via_midi()` (217~250) | 일치. BLE-MIDI 헤더·타임스탬프 해석도 이 함수가 함 |
| 기존 CC 처리 함수 (탭템포 CC10, 프리셋 CC86/87) | `midi_helper_tonex.c`: CC10 `control_trigger_tap_tempo()` (236), CC86 `control_request_preset_down()` (958), CC87 `control_request_preset_up()` (967) | 일치. 효과 On/Off는 CC2 딜레이(113)·CC14 게이트(272)·CC18 컴프(305)·CC32 모듈(417)·CC75 리버브(871) → 공통 끝단 `usb_modify_parameter()` (1286) |
| `TONEX_GLOBAL_MASTER_VOLUME`, min -40 / max 3 | enum `tonex_params.h:194`, 표 `tonex_params.c:182` `{0, -40, 3, "MVOL"}` | 일치 |
| dB = -40 + (값/127)×43 | `midi_helper_scale_midi_to_float()` `midi_helper.c:52` (CC122 경로 `midi_helper_tonex.c:1255`) | 일치 |
| `tonex_params_get_locked_access()` | `tonex_params.c:226` (뮤텍스, 타임아웃 시 실패) | 일치 |
| 원조 One의 글로벌 볼륨 | `usb_tonex_one.c` 송신 734~752, 수신 `usb_tonex_one_parse_param_changed()` 862~906 | **One은 0~10 값을 쓰고 컨트롤러가 -40~+3으로 선형 환산**한다. 화면·웹의 "dB"는 이 환산값이다 |
| 볼륨 읽기 후 화면 갱신 | `usb_tonex_one_process_single_message()`에 `TYPE_PARAM_CHANGED` 분기 없음 | **읽어도 UI·웹 갱신이 없음** → 포크가 연결(`boost_ctrl_on_volume_report`) |
| `control.c` 설정 구조체 | `tConfigData` 240~251, NVS 키 52~61, `LoadUserData()` 2337~2481, `SaveUserData()` 2312 | 일치 |
| 레거시 설정 마이그레이션 | `MigrateUserData()` 2194~2303: 옛 단일 blob `userdata`를 섹션별 키로 한 번 옮기고 지움 | 새 설정에 쓸 "마이그레이션"은 없음. 실제 패턴은 "섹션별 NVS 키, 없으면 기본값 저장, 로드 후 값 검사" |
| `control_save_user_data(0/1)` | `control.c:1344` → control 태스크에서 `SaveUserData()`(전 섹션), 1이면 재부팅 | 일치 |
| 객체 설정 API | `control.h:361/365`에 `control_set/get_config_item_object()` **선언만 있고 구현 없음** | 포크는 쓰지 않음(`fork_settings.c` 사용) |
| `wifi_config.c` 웹소켓 | `ws_handler()` 772, `CMD` 문자열 분기 836~1430, 서버 푸시 `wifi_request_sync()` 279 → `wifi_send_ws_async()` 175 | 일치. 웹 저장은 대부분 `control_save_user_data(1)`(재부팅) |
| `display.c` 튜너 | `UI_SetTunerFrequencies()` 1032, 튜너 화면 전환은 ONE+일 때만 (1486~1500), 튜너 아이콘도 ONE+만 (1336~1340) | 일치 |
| `display.c` 토스트 | `UI_ShowToast()` 1069 → `ui_show_toast()` 1801: 화면 중앙, 3초 고정 | 계획서의 "하단 1초"와 다름 → 포크가 별도 알림 추가 |
| UI 업데이트 큐 | `ui_update_queue` → `display_task()` 1853 → `update_ui_element()` 1309 | 일치 |
| `ui_settings_tab_view` (Gate·Comp·Amp·EQ·Mod·Dly·Rev·Glob) | `ui_generated_800x480land/screens.c:607`, 탭 637/830/1018/1257/1460/1768/2065/2327 | 일치 |
| 글로벌 볼륨 UI | Glob 탭에 이미 볼륨 슬라이더 있음 (`display_tonex.c:871~873`, 2648~2658) | 계획서 "메인 화면에 볼륨 정보 없음"은 맞음(설정 창에만 있음) |
| `find_midi_streaming()` 주석 처리된 인터페이스 로그 | `usb_comms.c:152~195` (로그 170~178) | 일치 |
| `usb_tonex_one_request_tuner()` 주석, 커밋 95e80f0 | `usb_tonex_one.c:369` "doesn't work on the One", 95e80f0 "Added Tuner display for the One Plus." | 일치 |
| ONE+ 튜너 메시지 0x0310 | `usb_tonex_one_plus.c:1190` | 일치 |
| 스킨 영역 10MB | `partitions_16MB.csv` `skins` 10M, 실제 `skins.bin` 5,867,098바이트 | 일치 (여유가 큼 → OTA 파티션 배치에 사용) |
| USB FIFO 기본 Balanced | `CONFIG_USB_HOST_HW_BUFFER_BIAS_BALANCED=y` | 일치 |

## 2. 구조

업스트림 파일은 연결만 고치고 기능은 포크 파일에 둔다(업스트림 병합 충돌 최소화, `docs/UPSTREAM_SYNC.md`).

| 포크 파일 | 역할 |
| --- | --- |
| `boost.c/.h` | 부스트 규칙(순수): 상태 판정 ±0.2dB, 토글 목표, 설정 검사 |
| `boost_ctrl.c/.h` | 부스트 실행: 현재 볼륨 읽기, OFF/ON 적용, 부팅 시 OFF, 볼륨 수신 알림 |
| `choc_map.c/.h` | 초콜릿 신호 판별(순수): BLE-MIDI 패킷 → 버튼 A~F, 동작 enum, 기본 배치 |
| `choc_exec.c/.h` | 동작 실행: control/usb 함수 직접 호출, 알림·웹 이벤트, 연결 기기 추적 |
| `fork_settings.c/.h` | 포크 설정 저장(NVS `boostconf`, `chocconf`), 즉시 적용 |
| `display_fork.c/.h` | 메인 화면 배지·볼륨 라벨, 하단 1초 알림, Setup 화면 여는 자리, 다른 태스크가 부르는 화면 갱신 API |
| `display_fork_setup.c/.h` | Setup 화면: Boost·Choc·Info 탭, 닫기 |
| `display_fork_config.c/.h` | Setup 화면의 업스트림 설정 탭(BT·MIDI·Misc·Wi-Fi), 저장 후 재부팅 |
| `display_fork_widgets.c/.h` | 포크 화면 공용 위젯(라벨·스위치·버튼·드롭다운·스테퍼·부스트 배지) |
| `wifi_fork.c/.h` | 웹 명령(부스트·초콜릿 설정, 펌웨어·USB 정보), 브라우저 푸시, `/fork.js` |
| `ota_update.c/.h`, `ota_tag_scan.c/.h` | Wi-Fi 업데이트(`POST /update`), 플랫폼 표식 검색 |
| `usb_desc_info.c/.h` | USB Configuration Descriptor 요약(튜너 1단계) |
| `pitch_yin.c/.h` | 음정 검출 엔진(튜너 2단계 준비) |
| `web/fork.js` | 웹 Boost·Chocolate·Firmware 페이지 |
| `fork.cmake`, `components/tonex_fork_config/Kconfig` | 빌드 목록, 포크 Kconfig(`TONEX_FORK_*`) |
| `CMakeUserPresets.json`, `sdkconfig.fork.ws43B`, `partitions_16MB_ota.csv` | `WS43B_FORK` 프리셋(OTA 파티션) |

신호 흐름:

```
초콜릿 BLE notify ─ midi_control.c ─ choc_exec_handle_ble_midi()
   ├─ 번역 켜짐 + 초콜릿 연결 + TONEX 계열 + 초콜릿 신호(PC, CC2, CC3)
   │     → choc_map_decode() → 버튼 A~F → run_action()
   │          ├─ 부스트: boost_ctrl → usb_modify_parameter(TONEX_GLOBAL_MASTER_VOLUME, dB)
   │          ├─ 탭템포/프리셋: control_trigger_tap_tempo(), control_request_preset_up/down/index()
   │          └─ 효과 토글: tonex_params 현재 값 반전 → usb_modify_parameter()
   │     → UI_ShowActionToast(), UI_NotifyChocButton(), 웹 CHOCEVENT
   └─ 그 외(번역 꺼짐, 커스텀 모드 CC 등) → midi_helper_process_incoming_data() (기존 그대로)

TONEX 볼륨 수신(usb_tonex*.c) → boost_ctrl_on_volume_report() → 배지 갱신 + 웹 BOOSTSTATE
```

## 3. 단계별 설계

### 3-1. 볼륨 부스트 (계획서 2-2)

- 설정 `tBoostConfig`: OFF/ON 값(0.1dB 정수, -40~+3dB, 0.5dB 단위, OFF < ON), 부팅 시 OFF. 기본값 OFF -2.0 / ON +3.0 / 부팅 시 OFF 켜짐.
- 상태 판정 `boost_state_for_volume()`: ON 값 ±0.2dB → ON, OFF 값 ±0.2dB → OFF, 그 외 CUSTOM. 판정은 매번 `TONEX_GLOBAL_MASTER_VOLUME`의 현재 값으로 한다.
  TONEX에서 볼륨을 한 번이라도 읽기 전에는 UNKNOWN(배지 "BOOST --")이다(그 전 값은 `tonex_params` 기본값 0dB라 판정에 쓰면 틀린다).
- 토글 `boost_ctrl_toggle()`: ON이면 OFF 값, OFF·CUSTOM·UNKNOWN이면 ON 값. 볼륨은 MIDI 값을 거치지 않고 dB(float)로 `usb_modify_parameter()`에 보낸다.
- 부팅 시 OFF: `control_set_sync_complete()`(프리셋 동기화 완료)에서 USB 연결당 한 번 OFF 값을 보낸다. 이 함수는 프리셋이 바뀔 때마다 불리므로 연결당 1회 플래그로 막고, USB가 끊기면 다시 한 번 적용한다.
  계획서 정의대로 초콜릿 연결·번역 사용과 무관하게 TONEX가 연결될 때마다 적용된다(끄려면 "부팅 시 OFF로 시작"을 끈다).
  One 계열은 볼륨을 전용 명령으로 보내고 바로 되읽으므로 순서 문제가 없다. 빅 TONEX는 전역 설정 전체를 보내는 방식이라,
  동기화 직후 요청한 전역 설정 응답보다 OFF 전송이 먼저 처리되면 빈 전역 설정을 보낼 수 있다(실물 항목). 고치려면 첫 전역 설정 수신 뒤로 미뤄야 하는데,
  그 수신 지점은 파라미터 락을 쥔 USB 파싱 경로라 거기서 전송하면 교착 위험이 생겨 지원 범위 밖 장비를 위해 넣지 않았다.
- 빅 TONEX(`AMP_MODELLER_TONEX`)는 볼륨을 보낸 뒤 되읽지 않으므로, `boost_ctrl_set()`이 보낸 값을 보드가 아는 볼륨에 바로 반영한다(One 계열은 페달이 다시 알려준 값을 쓴다).
- 변경 파일: `boost.c/.h`, `boost_ctrl.c/.h`, `fork_settings.c/.h`(새 파일), 연결: `control.c`(설정 로드·동기화 완료), `usb_comms.c`(끊김), `usb_tonex_one.c`·`usb_tonex_one_plus.c`·`usb_tonex_plug.c`·`usb_tonex.c`(볼륨 수신).

### 3-2. 초콜릿 번역 계층 + 웹 (계획서 2, 2-1)

- 판별 `choc_map_decode()`: BLE-MIDI 헤더·타임스탬프·러닝 스테이터스를 해석한다(`midi_helper`와 같은 헤더 규칙).
  PC n → 버튼 `n % 4` (A~D), CC2 값 5 → E, CC3 값 5 → F. 채널은 보지 않는다(이 경로는 이미 초콜릿 연결로 한정).
  - CC2·CC3은 값이 5가 아니어도 소비한다(뒤로 넘기면 CC2가 딜레이 On/Off로 해석됨).
  - 같은 패킷에 동시 누르기(E/F)와 PC가 함께 오면 PC는 버린다.
  - PC·CC2·CC3이 하나도 없는 패킷(예: CubeSuite 커스텀 모드의 CC122)은 기존 MIDI 처리로 그대로 보낸다. 지금 쓰는 커스텀 모드도 번역을 켠 채로 계속 동작한다.
- 동작(`enum ChocActions`, 저장값이라 순서 고정): 없음, 부스트 토글/켜기/끄기, 탭템포, 프리셋 업/다운, 프리셋 직접 선택(1~N, 보드가 보여주는 프리셋 순서 기준), 딜레이·공간계·모듈레이션·컴프·노이즈게이트 On/Off.
- 기본 배치: A 부스트 토글, B 탭템포, C 딜레이, D 공간계, E 프리셋 다운, F 프리셋 업. 번역 기본값 켜짐.
- 번역은 보드의 "Midi CC on BT" 설정과 무관하다. Valeton GP5가 연결돼 있으면 번역하지 않고 기존 처리로 보낸다(효과·볼륨 파라미터 번호가 TONEX 전용).
  웹 "실행" 버튼도 같은 이유로 TONEX 계열이 연결돼 있을 때만 동작을 실행한다(그 외에는 행 강조와 "NO TONEX" 알림만).
- 웹(`web/fork.js`). 부스트는 초콜릿 전용 기능이 아니므로(웹 버튼으로도 켠다) 페이지를 나눴다.
  - 상단 메뉴 **Boost**(Globals 옆, TONEX 계열일 때만): 배지·현재 볼륨·토글 버튼, OFF/ON 값·부스트 양, OFF로 시작.
    ON > +3dB를 넣으면 같은 부스트 양을 내는 OFF/ON 값을 안내하고 저장하지 않는다.
  - Config > **Chocolate**: 연결 상태(안 붙었으면 업스트림 Bluetooth 설정의 Central·Chocolate Series를 확인하라는 안내), 버튼 매핑 사용,
    버튼 A~F 동작 드롭다운(CC 번호 없음), 프리셋 직접 선택일 때만 번호 칸, 누른 버튼 행 1.5초 강조, 행마다 "실행"(그 버튼을 누른 것과 같은 동작).
  - 두 페이지 모두 저장은 재부팅 없이 즉시 적용된다. 휴대폰 폭(480px 이하)에서는 상단 메뉴가 두 줄로 넘어간다(Boost가 붙어 Config가 화면 밖으로 밀리지 않게).
- "버튼 매핑 사용"(`TranslateEnable`)을 끄면 초콜릿 신호를 번역하지 않고 기존 MIDI 처리(PC = 프리셋 선택, CC = Midi 설정)로 보낸다.
  초콜릿을 다른 모드로 업스트림 방식 그대로 쓰거나, 문제가 생겼을 때 원래 동작과 비교하려고 둔다.
- 웹소켓 명령(서버 `wifi_fork.c`): `GETFORKCONFIG`, `SETFORKCONFIG`(메시지에 있는 항목만 바꾼다 → `FORKCONFIGRESULT` 0=OK, 1=부스트 범위, 2=단위, 3=OFF≥ON, 4=버튼 동작·프리셋 번호 범위),
  `CHOCTEST`, `SETBOOST`(0/1/2=토글), 푸시 `GETFORKCONFIG`(설정·초콜릿 연결이 바뀌면 모든 화면에), `BOOSTSTATE`, `CHOCEVENT`.
  `GETFORKCONFIG`에는 초콜릿 연결 상태 `CHOC_STATE`(0 없음, 1 연결 중 = 스캔에서 이름을 찾아 여는 중, 2 연결됨 = 열기 성공)와
  업스트림 Bluetooth 설정 `BT_CHOC_EN`·`BT_MODE`도 실어 연결 안내에 쓴다. 번역은 연결 중부터 켠다(notify는 연결된 뒤에만 온다).
- 웹 저장 결과는 보낸 순서대로 받아 그 페이지에만 붙이고, 성공하면 설정을 한 번 더 받아 "보드에서 바뀌었다" 판단 기준을 맞춘다.
- 변경 파일: `choc_map.c/.h`, `choc_exec.c/.h`, `wifi_fork.c/.h`, `web/fork.js`(새 파일), 연결: `midi_control.c`(연결 추적·수신), `wifi_config.c`(명령·푸시·핸들러 등록), `web/index.html`(스크립트 1줄).
  연결 지점 전체 목록은 `docs/UPSTREAM_SYNC.md`, 빠짐 확인은 `tools/check_fork_hooks.sh`.

### 3-3. 메인 화면 표시 (계획서 3)

- 배지는 메인 화면 왼쪽 Bank 상자 아래 빈 자리(x 6, y 214, 148×38)에, 볼륨 라벨은 그 아래(y 254)에 둔다.
  BOOST ON = 주황 배경, BOOST OFF = 회색 배경, CUSTOM = 주황 테두리, 볼륨 미확인 = 회색 테두리 "BOOST --", TONEX 미연결·Valeton이면 숨김.
- 갱신은 배지와 라벨만 바꾼다(스킨 재그리기 없음). 트리거: 볼륨 수신, 부스트 설정 변경, 전체 파라미터 갱신(빅 TONEX는 상태 패킷으로 볼륨이 옴), USB 끊김.
- 동작 알림: 화면 하단(효과 아이콘 위)에 1초. 예 "BOOST ON  +3.0 dB", "TAP TEMPO", "DELAY OFF", "PRESET 5". 다른 화면 크기에서는 기존 토스트(중앙 3초)를 쓴다.
- **계획서와 다른 점**: EEZ 프로젝트에 객체를 추가하지 않고 `display_fork.c`가 실행 중에 LVGL 객체를 만든다.
  EEZ Studio 없이 작업할 수 있고, 업스트림이 EEZ 프로젝트를 다시 생성해도 충돌이 없다. 생성 코드는 고치지 않는다.
  보드 폰트(Montserrat)에 한글 글리프가 없어 보드 문구는 영어다.
- 변경 파일: `display_fork*.c/.h`(새 파일), 연결: `display.c`(헤더, 요소 enum, 큐 전달 `UI_PostForkUpdate()`, 분기, 파라미터 갱신, 초기화).

### 3-4. 보드 Setup 화면 (계획서 4-2)

- 업스트림 기어 화면은 TONEX 이펙트 파라미터 편집 화면(Gate~Glob 탭)이라 탭을 더 붙일 자리가 없다. 포크는 별도 **Setup 화면**을 만든다.
  - 여는 곳: 메인 화면 상단 오른쪽 Wi-Fi·BT·USB 아이콘 자리(누르면 주황으로 표시). 스와이프로 끝난 터치는 열지 않는다.
  - 닫기: 업스트림 설정 화면과 같은 자리의 체크 표시. 탭은 위 버튼으로만 바꾼다(목록을 끌다가 옆 탭으로 넘어가지 않게).
  - 업스트림 설정 화면의 탭에는 아무것도 붙이지 않는다(병합 안전).
- 탭: Boost · Choc · BT · MIDI · Misc · Wi-Fi · Info.
  - **Boost**: 배지·현재 볼륨·토글 버튼, OFF/ON 값 스테퍼(0.5dB), 부스트 양, Start with boost OFF. 범위를 벗어나면 바꾸지 않고 사유를 보여준다(ON은 +3dB가 최대 → OFF를 내리라고 안내).
  - **Choc**: "M-Vave Chocolate" 제목과 연결 상태(안 붙었으면 BT 탭에서 켤 항목 안내), Button mapping 스위치와 켜고 끌 때의 차이,
    버튼 A~F(2열) 동작 드롭다운(동작 13개) + 프리셋 직접 선택일 때만 번호 스테퍼, 마지막으로 누른 버튼. 초콜릿을 밟으면 해당 라벨이 1.5초 강조된다.
  - 드롭다운 목록은 높이 300px로 묶고 넘치면 끌어서 스크롤한다. 프리셋 번호는 목록에 넣지 않고 스테퍼로 고른다(빅 TONEX 150개도 목록이 길어지지 않게). 스테퍼는 길게 누르면 계속 움직인다.
  - **BT · MIDI · Misc · Wi-Fi**: 업스트림 컨트롤러 설정 중 보드에서 바꿀 만한 것(`display_fork_config.c` 표). 업스트림 공개 함수
    `control_get/set_config_item_int()`를 쓰므로 업스트림 파일은 고치지 않는다. 웹처럼 바꾼 값을 모았다가 **Save & Reboot**(확인 창)에서
    항목을 보내고 250ms 뒤 `control_save_user_data(1)`. 저장하지 않고 닫으면 저장·버리기를 묻는다.
    글자 입력(SSID·암호·BT 이름·mDNS)은 현재 값만 보여주고 웹에서 바꾼다. 풋스위치 CC 표, Preset/PC Map, LCD 타이밍(잘못 넣으면 보드로 되돌릴 수 없음)은 웹 전용.
    모르는 선택지 값(손상, 롤백 전 새 펌웨어가 쓴 값)은 "Other"로 보이고 고르지 않으면 그대로 남는다.
    SSID 없이 Station을 고르면 경고만 띄운다(웹과 같이 막지 않음, 재부팅 뒤 보드 Wi-Fi 탭으로 되돌릴 수 있다).
  - **Info**: 펌웨어 버전·빌드·실행 슬롯·업데이트 방법, USB 디스크립터 요약(튜너 1단계).
- Boost·Choc 값은 바로 적용되고(재부팅 없음) NVS 저장은 마지막 변경 1초 뒤 한 번 한다(+/- 연타가 플래시 쓰기 연타가 되지 않게, 화면이 멈추지 않게).
  웹과 같은 설정(`fork_settings`)을 공유하고 서로 바뀌면 상대 화면도 갱신된다.

### 3-5. 튜너 (계획서 1)

- 1단계(구현): TONEX가 연결될 때 Configuration Descriptor를 요약해 보드 Setup 화면 Info 탭과 웹 Config > Firmware > USB 정보에 보여준다.
  장치 줄 바로 다음 `TUNER CHECK:` 줄에 등시성 오디오 IN 후보를 정리하고, 그 아래에 인터페이스(번호·alt·클래스/서브클래스/프로토콜, UAC2 표시), IAD,
  엔드포인트(방향·종류·동기화·패킷 크기·간격), AC 헤더 bcdADC, AS 일반·포맷(채널·비트·샘플레이트)을 적는다. 웹에서는 텍스트로 복사할 수 있다.
- 음정 검출 엔진(구현, 미연결): `pitch_yin.c` — 48kHz → 12kHz 다운샘플(31탭 FIR), YIN(임계 0.12) + 포물선 보간, 그리고 같은 구간 48kHz 원신호에서 주기 근처 5개 지연만 다시 계산하는 정밀화.
  합성 기타 신호(배음 10개, 약한 기음, 잡음) 기준 최악 오차: 73~659Hz 0.4센트 이하, 1318Hz 부근 1.1센트 이하 (`test_host/test_pitch_yin.c`).
- 2단계(미구현, 실물 덤프 후): USB 오디오 수신. UAC1이면 SET_CUR(샘플레이트), UAC2면 클럭 소스 요청 후 인터페이스 claim → alt 선택 → 등시성 IN 전송 반복.
  튜너 화면은 `UI_SetTunerFrequencies()`에 값을 넣고 튜너 아이콘 조건(ONE+ 한정)을 푼다. 튜닝 중 음소거(-40dB)는 녹음 신호가 같이 작아지는지 확인 후.

### 3-6. 펌웨어 업데이트 (추가)

- `WS43B_FORK` 프리셋은 OTA 파티션 표를 쓴다: `ota_0`/`ota_1` 각 0x4B0000(4.6875MB), `skins` 0x970000부터 0x690000. NVS 위치(0x9000)는 그대로라 설정이 유지된다.
  슬롯 여유는 빌드 끝의 `TonexController.bin binary size ... (NN%) free` 줄로 확인한다(업스트림 코드가 늘면 줄어든다).
- 웹 Config > Firmware에서 `TonexController.bin`을 올리면 다음 슬롯에 쓰고 재부팅한다. 받기 전에 이미지 헤더(매직·칩·프로젝트 이름)를, 쓰는 동안 플랫폼 표식
  `TNXCTL-OTA-PLATFORM=WS43B;`을 확인한다. 표식은 OTA를 켠 빌드에만 들어가므로 업스트림 이미지·다른 플랫폼 이미지는 거부된다.
- 롤백: 부트로더 롤백을 켰다. 새 이미지는 웹 서버를 띄운 뒤 30초 동안 리셋되지 않으면 확정되고, 그 전에 리셋되면 이전 이미지로 돌아간다.
  확정 조건은 "30초 안에 리셋되지 않음"뿐이다(멈춤은 리셋을 일으키지 않는다). 웹 요청 처리 등으로 조건을 강화하면 업데이트 뒤 웹을 열지 않을 때 다음 리셋에서 되돌아가므로 지금 조건을 쓴다.
- OTA 빌드는 httpd 스택을 8KB로 늘린다(`esp_ota_end()` 이미지 검증이 httpd 태스크에서 돈다). 브라우저가 올리기 전에 같은 헤더·표식 검사를 해서, 잘못 고른 파일은 업로드 없이 사유를 보여준다.
- 첫 설치(파티션 표가 바뀜)는 USB로 한 번 한다. 절차는 `docs/FIRMWARE_UPDATE.md`.

## 4. 설정 저장 포맷과 마이그레이션

포크 설정은 업스트림 `control.c`의 `tConfigData`에 넣지 않고 `fork_settings.c`가 같은 NVS 네임스페이스(`storage`)의 별도 키에 저장한다.
업스트림 설정 구조체·`MigrateUserData()`는 그대로이므로 업스트림 병합과 업스트림 펌웨어로의 복귀에 영향이 없다(복귀하면 포크 키만 남고 무시된다).

| NVS 키 | 구조체 | 바이트 | 필드 |
| --- | --- | --- | --- |
| `boostconf` | `tBoostConfig` (packed) | 6 | `Version`(u8, 현재 1), `OffDb10`(i16, -400~30, 5 단위), `OnDb10`(i16), `OffAtBoot`(u8) |
| `chocconf` | `tChocConfig` (packed) | 14 | `Version`(u8, 현재 1), `TranslateEnable`(u8), `Buttons[6]` = {`Action`(u8, `ChocActions`), `Param`(u8, 프리셋 번호 1~150)} |

로드 규칙 (`fork_settings_load()`, `control_load_config()` 끝에서 호출):

- 키가 없음(포크 첫 설치) → 기본값으로 저장.
- 저장된 blob이 구조체보다 짧음(예전 레이아웃) → 앞부분만 읽히고 뒤에 추가된 필드는 기본값으로 남는다. 로드할 때 다시 저장하지는 않고,
  다음에 설정을 바꿀 때 현재 레이아웃·`Version`으로 저장된다.
- 저장된 blob이 더 김(새 펌웨어가 쓴 뒤 OTA 롤백 등으로 예전 펌웨어가 됨) → 아는 앞부분만 쓰고 NVS는 덮지 않는다. 다시 새 펌웨어로 올리면 그대로 돌아온다.
  (예전 펌웨어에서 그 설정을 바꿔 저장하면 짧은 blob으로 바뀌고, 새 펌웨어는 새 필드를 기본값으로 읽는다.)
- 값이 잘못됨 → 부스트는 기본값, 버튼은 항목별 보정(`choc_config_sanitize()`). 새 버전이 쓴 blob이면 메모리에서만 고치고 NVS는 덮지 않는다.
- `Version`은 setter가 쓸 때 현재 값으로 정해진다. 로드할 때 부스트는 `Version` 0을 무효(손상)로 보고, 버튼 설정은 `Version`을 보지 않는다.
  그 밖의 값은 모두 그대로 받는다(값 검사는 필드별로 한다).
- 저장은 `fork_settings_set_*()`가 메모리에 바로 반영한 뒤 전용 태스크(`ForkSave`)가 마지막 변경 1초 뒤 한 번 한다. 그 1초 안에 전원이 꺼지면 마지막 변경만 사라진다.

구조체를 바꿀 때: 필드는 뒤에만 추가하고 `*_CONFIG_VERSION`을 올린다. `ChocActions`는 뒤에만 추가한다(숫자가 NVS와 웹 프로토콜에 저장됨).

## 5. 구현 전 확인할 것 — 답

### 코드로 답한 것

| 항목 | 답 |
| --- | --- |
| MIDI 채널 | 번역 경로는 채널을 보지 않는다. 초콜릿 채널이 보드 설정과 달라도 동작한다(번역을 끄면 기존처럼 `CONFIG_ITEM_MIDI_CHANNEL`로 거른다). |
| 뱅크가 끝에서 처음으로 돌아가는지 | 판별이 `n % 4`라 뱅크 위치와 무관하다. 확인 불필요. |
| E/F와 함께 오는 PC | 같은 BLE 패킷이면 PC를 버린다. 별도 패킷으로 먼저 오면 A/B(C/D) 동작이 먼저 실행된다 → 실물에서 이 경우가 나오면 PC를 수십 ms 늦춰 판정하는 처리가 필요하다(아래 실물 항목). |
| V3.0.0.2 알파 기준 포크 여부 | 기준 main `9fd1156`은 V2.5.0.2 이후 26커밋으로, 4.3B 깜빡임 수정(`3816ae1`, 픽셀 클럭·포치를 웹 Misc에서 설정)이 이미 들어 있다. 별도 결정 불필요. |
| (선택) 본체 풋스위치 튜너 시 미확인 메시지 | `usb_tonex_one_parse()`의 default 분기가 시리얼 로그(`Unknown structure`)만 남긴다. 화면 표시는 아직 없다. 2단계에서 Setup 화면 Info 탭에 붙일 수 있다. |

### 실물 확인이 필요한 것

**초콜릿 신호** (맥 MIDI Monitor, 또는 웹 Chocolate 페이지·보드 Setup > Choc 탭의 강조로 확인)

- [ ] Program change A에서 A+B / C+D를 눌렀을 때 CC 2 / CC 3(값 5) 외에 PC도 같이 나오는지, 나온다면 같은 패킷인지
- [ ] 단일 버튼의 PC가 누를 때 나오는지, 뗄 때 나오는지 (탭템포 정확도)
- [ ] Program change B나 Advanced custom mode에 더 나은 동시 누르기 신호가 있는지

**TONEX One USB (튜너용)** — 보드 Setup 화면 Info 탭 또는 웹 Firmware > USB 정보

- [ ] Full-Speed 연결에서 Audio Streaming 인터페이스와 등시성 IN 엔드포인트가 있는지 (`TUNER CHECK:` 줄)
- [ ] UAC 버전(1 또는 2), 최대 패킷 크기, 샘플레이트
- [ ] 녹음 채널이 처리된 신호인지 DI(원음)인지
- [ ] 글로벌 볼륨 -40dB에서 USB 녹음 신호도 작아지는지

**볼륨 부스트**

- [ ] 부스트 ON/OFF 후 배지 아래 dB 값이 설정값과 0.2dB 이내인지 (TONEX One이 0~10 값을 내부에서 반올림하면 판정이 CUSTOM으로 나온다 → 허용 범위 조정)
- [ ] "dB"가 실제 음량 변화와 맞는지 (One의 0~10을 선형 환산한 값이라 실제 dB와 다를 수 있다)
- [ ] (빅 TONEX를 쓸 때만) 연결 직후 부팅 시 OFF가 적용되고 다른 전역 설정(튜닝 기준음 등)이 바뀌지 않는지

**기타**

- [ ] 보드 전원을 9V 1A 이상으로 교체

## 6. 실물 검증 순서

1. USB로 `WS43B_FORK` 설치(`docs/FIRMWARE_UPDATE.md`) → 9V 전원으로 켜고 TONEX One 연결 → 프리셋 동기화 후 배지가 "BOOST OFF -2.0 dB"인지(부팅 시 OFF).
2. 메인 화면 상단 상태 아이콘을 눌러 Setup 화면이 열리는지, 메인 화면 좌우 스와이프가 그대로인지. Info 탭 사진 → 튜너 2단계 결정.
3. CubeSuite를 Program change A로 바꾸고 A~D, E(A+B), F(C+D)를 밟으며 Setup > Choc 강조·웹 Chocolate 페이지 행 강조·보드 하단 알림 확인.
4. 부스트 토글 → 배지 ON/OFF, 웹 볼륨 표시, 실제 음량. 웹 Globals의 볼륨을 바꿔 CUSTOM이 나오는지.
5. 딜레이·공간계 토글이 실제 상태를 따라가는지(웹이나 보드에서 먼저 켠 뒤 초콜릿으로 끄기).
6. Setup > Misc에서 값 하나를 바꿔 Save & Reboot → 재부팅 뒤 웹 Config > Misc에도 같은 값인지. 드롭다운 목록을 끌어 스크롤되는지.
7. 웹 Firmware에서 같은 `TonexController.bin`을 다시 올려 Wi-Fi 업데이트와 재부팅 확인.
8. 초콜릿 반응 지연이 Wi-Fi 켜짐 여부에 따라 달라지는지(계획서 4-1).

## 7. 검증한 것

| 대상 | 방법 | 결과 |
| --- | --- | --- |
| 초콜릿 판별 | 호스트 테스트: PC 0~127 전부, CC2/CC3, 러닝 스테이터스, 잘린·깨진 패킷, 무작위 20만 패킷(ASan·UBSan) | 통과 |
| 부스트 규칙 | 호스트 테스트: 판정·허용 범위·토글·검증, One 0~10 왕복 환산 전 범위 | 통과 |
| OTA 플랫폼 표식 검색 | 호스트 테스트: 1바이트~4KB 무작위 조각, 거의 같은 문자열, 처음/끝 위치 | 통과 |
| 음정 검출 | 호스트 테스트: 10개 음 × 5개 디튠 × 3가지 파형, 무음·잡음 | 통과 (3-5 수치) |
| 웹 페이지 | 가짜 웹소켓 보드로 PC·휴대폰 폭 렌더링, Boost·Chocolate 따로 저장·검증·실행, 업로드(확정 대기·연속 클릭·읽기 실패)·USB 정보 | 통과 |
| 4.3B 화면 배치 | `test_host/ui_preview`: 업스트림 생성 UI + `display_fork*.c`를 맥에서 실행해 메인 화면(배지 4상태·알림·Setup 여는 자리)과 Setup 화면 7개 탭·드롭다운·저장 확인 창을 PNG로 | 겹침 없음 (스킨 그림·실제 LCD 색감·터치는 보드 필요) |
| 펌웨어 빌드 | ESP32-S3 프리셋 23개 + `WS43B_FORK` (0장) | 통과 |
| 보드 화면·BLE·USB·OTA 실동작 | 실물 필요 | 미검증 |

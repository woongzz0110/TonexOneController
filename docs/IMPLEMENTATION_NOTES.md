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
| 4-2 보드 화면 설정 | 구현 (Setup 화면: 상단 상태 아이콘을 누르면 열림. Boost·Choc·Tuner·BT·MIDI·Misc·Wi-Fi·Info 탭) | 터치 조작감 |
| 1 튜너 | 구현(희망 1·2 둘 다, 3-5). 업스트림 튜너 화면을 원조 One에서도 열고, 초콜릿 Tuner On/Off로 여닫음 | 페달 응답·FS 녹음 스트림이 실제로 있는지(5·6장) |
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
| `usb_tonex_one_request_tuner()` 주석, 커밋 95e80f0 | `usb_tonex_one.c:369` "doesn't work on the One". 이 주석은 95e80f0이 아니라 같은 날 39d6aaf에서 들어갔고, 요청 바이트는 ONE+ 것을 그대로 복사했다(업스트림 이슈 #448: One은 무시) | 일부 다름 |
| ONE+ 튜너 메시지 0x0310 | `usb_tonex_one_plus.c:1190` | 일치 |
| 스킨 영역 10MB | `partitions_16MB.csv` `skins` 10M, 실제 `skins.bin` 5,867,098바이트 | 일치 (여유가 큼 → OTA 파티션 배치에 사용) |
| USB FIFO 기본 Balanced | `CONFIG_USB_HOST_HW_BUFFER_BIAS_BALANCED=y` | 일치 |

## 2. 구조

업스트림 파일은 연결만 고치고 기능은 포크 파일에 둔다(업스트림 병합 충돌 최소화, `docs/UPSTREAM_SYNC.md`).

| 포크 파일 | 역할 |
| --- | --- |
| `boost.c/.h` | 부스트 규칙(순수): 상태 판정 ±0.2dB, 토글 목표, 설정 검사 |
| `boost_ctrl.c/.h` | 부스트 실행: 현재 볼륨 읽기, OFF/ON 적용, 부팅 시 OFF, 볼륨 수신 알림 |
| `choc_map.c/.h` | 초콜릿 신호 판별(순수): BLE-MIDI 패킷 → 버튼 A~F와 누름 종류(PC 모드 TAP / 누름 / 뗌), 동작 enum, 기본 배치 |
| `choc_hold.c/.h` | 길게 누름 규칙(순수): 누름·뗌·타이머 만료 → 원래 동작 / 튜너 여닫기 |
| `choc_exec.c/.h` | 동작 실행: control/usb 함수 직접 호출, 알림·웹 이벤트, 연결 기기 추적 |
| `fork_settings.c/.h` | 포크 설정 저장(NVS `boostconf`, `chocconf`, `tunerconf`), 즉시 적용 |
| `display_fork.c/.h` | 메인 화면 배지·볼륨 라벨, 하단 1초 알림, Setup 화면 여는 자리, 다른 태스크가 부르는 화면 갱신 API |
| `display_fork_setup.c/.h` | Setup 화면: Boost·Choc·Info 탭, 닫기 |
| `display_fork_tuner.c/.h` | 튜너 화면 진입·나가기 감지, 원조 One 튜너 아이콘, 튜너 화면 출처 표시, 초콜릿 여닫기, Setup 화면 Tuner 탭 |
| `display_fork_config.c/.h` | Setup 화면의 업스트림 설정 탭(BT·MIDI·Misc·Wi-Fi), 저장 후 재부팅 |
| `display_fork_widgets.c/.h` | 포크 화면 공용 위젯(라벨·스위치·버튼·드롭다운·스테퍼·부스트 배지) |
| `wifi_fork.c/.h` | 웹 명령(부스트·초콜릿·튜너 설정, 펌웨어·USB 정보, 튜너 진단), 브라우저 푸시, `/fork.js` |
| `ota_update.c/.h`, `ota_tag_scan.c/.h` | Wi-Fi 업데이트(`POST /update`), 플랫폼 표식 검색 |
| `usb_desc_info.c/.h` | USB Configuration Descriptor 요약(Setup > Info, 웹) |
| `pitch_yin.c/.h` | 음정 검출 엔진(YIN + 원신호 정밀화, 순수) |
| `tuner_analyzer.c/.h` | 녹음 샘플 → 20ms마다 음 높이, 흔들림 줄이기(순수) |
| `uac_stream.c/.h` | USB Audio 디스크립터에서 녹음 스트림 찾기·고르기, 패킷 → 모노 샘플(순수) |
| `tuner_proto.c/.h` | 원조 One 튜너 요청 번호 후보, 응답 해석, 튜너 설정 구조체(순수) |
| `tuner_logic.c/.h` | 튜너 출처 고르기·페달 후보 시험 상태 기계(순수, 시간을 인자로) |
| `tuner_audio.c/.h` | USB 녹음 수신(자체 USB Host 클라이언트, 등시성 IN)과 분석 태스크 |
| `tuner_ctrl.c/.h` | `tuner_logic` 실행(락·요청 보내기·저장·화면 전달), 페달 메시지 기록, 진단 글 |
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
  - **Manufacturer Control 모드**도 받는다(MOD 포럼 실측 기록): A~D = CC 20~23(누름 127, 뗌 0), E = CC 24, F = CC 25.
    E·F를 뗄 때는 마지막에 뗀 버튼의 CC 20~23 = 0이 온다. 이 CC 20~25도 번역이 켜져 있으면 소비한다.
    CubeSuite CC 모드의 "Visual Behavior"(누를 때마다 127/0 번갈아)는 누름·뗌으로 잘못 읽히므로 쓰지 않는다.
- **길게 눌러 튜너**(`choc_hold.c`, 순수): Program change 모드는 뗄 때 신호가 없어서 길게 누름을 알 수 없다 → Manufacturer Control 모드에서만 된다.
  고른 버튼(`TunerHoldButton`)은 누를 때 타이머(`esp_timer`, 기본 0.8초, 0.3~2.0초)를 걸고, 그때까지 누르고 있으면 튜너를 여닫고 뗌은 무시한다.
  그 전에 떼면 떼는 순간 원래 동작을 한다. 다른 버튼은 누르는 순간 실행(탭템포가 늦어지지 않게)하고 뗌은 무시한다.
  길게 누름 버튼이 A·B인데 E보다 먼저 따로 눌림으로 왔으면 그 대기는 E에 흡수한다. 길게 누름 버튼이 아닌 A·B가 따로 먼저 오면 그 동작은 실행된다
  (초콜릿이 그렇게 보내는지는 실물 미확인, 5장. 그래서 화면 안내는 길게 누름 버튼으로 A~D를 권한다).
  A·B를 새로 누르면 남아 있던 E 눌림을 지운다(E 뗌을 놓쳐도 다음 A 뗌을 E 뗌으로 읽어 튜너가 열리지 않게). E가 길게 누름 대기 중이었으면 실행 없이 끝낸다.
  C·D와 F도 같다. 뗌을 못 받은 채 BLE가 끊기면 눌림 상태와 신호 종류를 지운다.
  마지막으로 받은 신호 종류(`choc_exec_get_input_mode()`: Program change / 누름·뗌)를 Setup > Choc 탭과 웹에 보여, PC 모드라서 안 되는 경우를 바로 알 수 있게 했다.
- 동작(`enum ChocActions`, 저장값이라 순서 고정): 없음, 부스트 토글/켜기/끄기, 탭템포, 프리셋 업/다운, 프리셋 직접 선택(1~N, 보드가 보여주는 프리셋 순서 기준), 딜레이·공간계·모듈레이션·컴프·노이즈게이트 On/Off, 튜너 열기/닫기.
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
- 웹소켓 명령(서버 `wifi_fork.c`): `GETFORKCONFIG`, `SETFORKCONFIG`(메시지에 있는 항목만 바꾼다 → `FORKCONFIGRESULT` 0=OK, 1=부스트 범위, 2=단위, 3=OFF≥ON, 4=버튼 동작·프리셋 번호·길게 누름 범위),
  길게 누름은 `TUNER_HOLD`(-1 없음, 0~5 = A~F)·`HOLD_MS`(300~2000, 100 단위), 신호 종류 `CHOC_INPUT`(0 아직 없음, 1 Program change, 2 누름·뗌, `CHOCEVENT`에도 실림),
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
- 탭: Boost · Choc · Tuner · BT · MIDI · Misc · Wi-Fi · Info. (Tuner 탭은 3-5)
  - **Boost**: 배지·현재 볼륨·토글 버튼, OFF/ON 값 스테퍼(0.5dB), 부스트 양, Start with boost OFF. 범위를 벗어나면 바꾸지 않고 사유를 보여준다(ON은 +3dB가 최대 → OFF를 내리라고 안내).
  - **Choc**: "M-Vave Chocolate" 제목과 연결 상태(안 붙었으면 BT 탭에서 켤 항목 안내), Button mapping 스위치와 켜고 끌 때의 차이,
    버튼 A~F(2열) 동작 드롭다운(`ChocActions` 전부) + 프리셋 직접 선택일 때만 번호 스테퍼, 마지막으로 누른 버튼. 초콜릿을 밟으면 해당 라벨이 1.5초 강조된다.
    맨 아래 줄: Hold for tuner(Off/A~F)·누르는 시간 스테퍼, 초콜릿 신호 모드에 따른 안내(3-2).
  - 드롭다운 목록은 높이 300px로 묶고 넘치면 끌어서 스크롤한다. 프리셋 번호는 목록에 넣지 않고 스테퍼로 고른다(빅 TONEX 150개도 목록이 길어지지 않게). 스테퍼는 길게 누르면 계속 움직인다.
  - **BT · MIDI · Misc · Wi-Fi**: 업스트림 컨트롤러 설정 중 보드에서 바꿀 만한 것(`display_fork_config.c` 표). 업스트림 공개 함수
    `control_get/set_config_item_int()`를 쓰므로 업스트림 파일은 고치지 않는다. 웹처럼 바꾼 값을 모았다가 **Save & Reboot**(확인 창)에서
    항목을 보내고 250ms 뒤 `control_save_user_data(1)`. 저장하지 않고 닫으면 저장·버리기를 묻는다.
    글자 입력(SSID·암호·BT 이름·mDNS)은 현재 값만 보여주고 웹에서 바꾼다. 풋스위치 CC 표, Preset/PC Map, LCD 타이밍(잘못 넣으면 보드로 되돌릴 수 없음)은 웹 전용.
    모르는 선택지 값(손상, 롤백 전 새 펌웨어가 쓴 값)은 "Other"로 보이고 고르지 않으면 그대로 남는다.
    SSID 없이 Station을 고르면 경고만 띄운다(웹과 같이 막지 않음, 재부팅 뒤 보드 Wi-Fi 탭으로 되돌릴 수 있다).
  - **Info**: 펌웨어 버전·빌드·실행 슬롯·업데이트 방법, USB 디스크립터 요약(장치가 알려준 구성 그대로).
- Boost·Choc 값은 바로 적용되고(재부팅 없음) NVS 저장은 마지막 변경 1초 뒤 한 번 한다(+/- 연타가 플래시 쓰기 연타가 되지 않게, 화면이 멈추지 않게).
  웹과 같은 설정(`fork_settings`)을 공유하고 서로 바뀌면 상대 화면도 갱신된다.

### 3-5. 튜너 (계획서 1)

원조 One이 튜너 값을 주는지, FS에서 녹음을 받을 수 있는지 둘 다 확인된 적이 없다. 그래서 두 경로를 모두 넣고 되는 쪽을 쓴다.
알려진 사실(외부 조사, 출처는 계획서 1장 참고문헌):
- 업스트림 제작자가 ONE+ 튜너 요청을 그대로 One에 보냈을 때 무시됐다(업스트림 이슈 #448). One용 앱·원격 튜너는 IK가 내놓지 않았다.
- One의 녹음은 UAC2, 44.1kHz, 32비트 칸 24비트 스테레오(처리된 소리, DI 없음)다. 공개된 HS 덤프에서 녹음 EP `0x84`의 패킷 크기는 56바이트다.
  FS에서도 56이면 1ms에 360바이트(async 여유 포함 368)가 필요해 담을 수 없다. ESP32-S3로 시도한 다른 사람도 "HS가 필요해 보인다"고 결론 냈다.

**화면·조작**
- 업스트림 튜너 화면(`objects.tuner`)을 그대로 쓴다. 업스트림은 ONE+에서만 메인 화면 튜너 아이콘을 보이는데,
  포크 감시 타이머(100ms, `display_fork_tuner.c`)가 원조 One·ONE+면 아이콘을 보이고 그 밖의 장치면 숨긴다.
  화면이 열리고 닫히는 것은 두 곳에서 안다: 업스트림이 열 때·닫을 때 보내는 튜너 요청(One이면 연결점에서 바로)과 감시 타이머.
  요청 쪽이 먼저라, 열자마자 온 페달 응답을 "닫힌 화면에서 켜진 튜너"로 오해해 끄는 일이 없다. 감시 타이머는 실제 화면 상태를 매 주기 알려,
  큐에서 늦게 소비된 열기·닫기 요청(닫은 뒤의 열기 등)이 남긴 어긋남을 다음 주기에 되돌린다. 업스트림 `display.c`는 고치지 않았다.
- 초콜릿 동작 `Tuner On/Off`(13번, 뒤에 추가): 튜너 화면이면 닫고 아니면 연다(ONE+는 업스트림 튜너). Setup 화면에 저장 안 한 변경이 있으면 기존 확인 창을 먼저 띄운다.
  일반 이펙터처럼 버튼을 길게 눌러 여닫는 것은 Setup > Choc 탭(웹 Chocolate)의 "Hold for tuner"로 정한다(3-2, 초콜릿 Manufacturer Control 모드 필요).
- 튜너 화면 원형 게이지 오른쪽(x 592, y 150)에 지금 쓰는 출처를 보인다(페달 후보 묻는 중 n/4 + "play a string", 확인된 후보를 켜고 값을 기다리는 중,
  Pedal only에서 시험 무응답, USB 녹음 레이트·레벨, 녹음 패킷이 모자람, 출처 없음 → Setup > Tuner).

**희망 1: 페달 튜너 메시지** (판단 `tuner_logic.c`(순수, 호스트 테스트), 실행 `tuner_ctrl.c`, 업스트림 연결 `usb_tonex_one.c` 2줄)
- 같은 기능의 메시지 번호가 모델마다 다르다: 상태 0x0306/0x0304, 값 변경 0x0309/0x0306, 마스터 볼륨 0x030D/0x030A(One/ONE+), 즉 0x0306 이상에서 One = ONE+ + 3.
  업스트림 One 튜너 요청은 ONE+ 번호(0x030F)를 그대로 복사한 것이라, One 번호 체계로 옮긴 후보를 가능성 순으로 보낸다. 근거 없는 추정이다.
  - 자동(튜너를 처음 열 때) 4개: 0x0312/0x030B, 0x0312/0x030F, 0x0312/0x0319, 업스트림과 같은 0x030F/0x0319.
  - 수동 "Probe all"에서만 2개 더: 0x0311/0x030B, 0x030F/0x030B. 같은 +3 규칙으로 보면 One의 현재 프리셋(ONE+ 0x030E)·슬롯 선택(ONE+ 0x030C)일 수 있어
    시험 중 프리셋·슬롯이 바뀔 수 있다(웹은 확인 창, 보드는 안내 문구).
- 포크가 보내는 튜너 요청은 상태 바이트에 후보를 싣는다(`0x80 | 후보 << 1 | 상태`). 연결점이 그것을 풀어 요청 바이트를 바꾸므로,
  업스트림 큐가 요청을 버리거나(뒤따른 프리셋 변경·같은 번호 파라미터 변경, `usb_tonex_one.c` 입력 큐 병합) 늦게 처리해도 요청과 후보의 짝이 어긋나지 않는다.
- 요청은 큐를 거쳐 늦게 나갈 수 있어 "지금 켜져 있어야 할 후보"와 "켜기가 실제로 나간 후보"를 따로 둔다. 늦게 나가려는 요청은 그사이 바뀐 것이면 버리고
  (켜기는 지금 후보가 아니면, 끄기는 다시 지금 후보가 됐으면), 켜져 있으면 안 되는 후보는 50ms 주기가 끈다(끄기가 1.5초 안에 안 나가면 다시 보낸다).
  감시 타이머의 매 주기 알림과 함께, 닫기와 다음 후보 켜기가 엇갈리거나 닫기·열기 요청이 늦게 소비돼도 켜진 채 남는 후보가 없다.
- 열린 화면에서 페달 요청이 빠지면(열기 요청이 큐에서 버려짐, USB를 다시 꽂음, USB 녹음만에서 되돌림) 50ms 주기가 열기와 같은 규칙으로 한 번 묻는다.
  열기 요청 유실은 1.5초 기다린 뒤 판단한다. 원조 One이 첫 상태 메시지를 보내기 전(업스트림이 요청을 처리하지 않는 동기화 중)에는 요청을 만들지 않는다.
- 후보 하나를 켠 요청이 실제로 나간 뒤 1초 동안 튜너 값이 없으면 끄고 다음 후보를 켠다. 켜기 요청이 1.5초 안에 안 나가면 한 번 다시 보내고, 그래도 안 나가면
  시험을 멈추고 결과는 "모름"으로 둔다(다 보내 보지 못했으니 "없음"으로 결론 내지 않는다). 원조 One이 빠지면 시험·켜진 후보를 정리한다.
- 모르는 형식의 메시지가 ONE+ 튜너 배치(기준 주파수 float @10이 400~480, 센트 float @15가 ±100 안, 음 @19)로 그럴듯하게 읽히면 튜너 값으로 본다.
  지금 후보의 켜기가 나간 뒤 새로 흐르기 시작한 값(0.4초 넘게 끊겼다가 다시 온 값)만 그 후보 덕으로 보고 NVS에 기억한다(`tunerconf`).
  앞 후보·페달 본체 튜너에서 이어지던 값은 아니다. 다만 후보가 바뀌는 수 ms 안에 앞 후보가 처음 만든 값은 다음 후보 몫이 될 수 있다
  (켠 직후를 빼면 응답이 빠른 진짜 후보를 놓친다. 페달 응답 지연을 모르므로 그대로 둔다).
- 다시 시험할 때(Setup > Tuner > Probe)는 켜 둔 후보를 먼저 끄고 0.4초 틈을 둔 뒤 첫 후보를 켠다. 저장된 후보는 새 후보를 찾았을 때만 바뀐다
  (연주 없이 누른 시험이 되던 결과를 지우지 않게). 못 찾으면 열린 화면은 저장된 후보로 돌아간다.
- 자동 후보 4개가 모두 나갔는데 값이 없으면 이번 부팅 동안만 기억하고 USB 녹음만 쓴다. 연주를 안 해서 값이 없었을 수도 있으니 저장하지 않는다(다음 부팅에 다시 묻는다).
  Pedal only는 열 때마다 시험한다.
- 화면을 닫은 채 시험하다(Setup > Tuner > Probe) 페달 튜너가 켜지면 확인만 하고 끈다(소리가 꺼진 채 두지 않는다).
  화면을 닫으면(닫기 요청이 큐에서 버려져 감시 타이머만 닫힘을 알아도) 시험을 그만두고 켠 후보를 끈다. 시험 중 신호 출처를 USB 녹음만으로 바꿔도 같다.
- 디버깅용 기록: 모르는 메시지 최근 8개(형식·길이·앞 40바이트), 후보별 결과(무응답·켜져 있던 시간·첫 응답 형식과 기대 형식·첫 응답 앞 24바이트·튜너 값 형식),
  보낸 요청·재전송·모양 바뀜·확정에 안 쓴 값 횟수, 시험 중단(큐 유실 / 닫기·뽑기·Source 변경),
  일부러 보내지 않은 요청의 사유별 횟수(USB 녹음만·이번 부팅 응답 없음·시험 중·켜진 후보 없음·늦은 요청),
  상태 메시지(0x0306)가 바뀐 바이트 위치(최대 8개), 부팅 후 시각, 튜너 태스크 3개의 스택 여유.
  페달 본체 튜너(풋스위치 길게)를 켰다 끄면 상태 메시지에서 어느 바이트가 바뀌는지 볼 수 있다.

**희망 2: USB 녹음** (`uac_stream.c`, `tuner_audio.c`, `tuner_analyzer.c`)
- 장치를 열 때(`usb_desc_info_capture`) 구성 디스크립터에서 UAC1/UAC2 등시성 IN 스트림을 모두 찾고 하나를 고른다:
  이 연결 속도에서 한 패킷에 44.1kHz를 담을 수 있는 것(최소 = 올림 프레임, 여유 = async +1 프레임) → 수신 FIFO 추정치(S3 Balanced 408바이트, Bias IN 600바이트) 안 → 작은 패킷.
  최소에도 못 미치면 고르기만 하고 열지 않는다(진단 "TOO SMALL", 화면 "too small at Full Speed"). 최소~여유 사이는 "TIGHT"(대개 되지만 넘칠 수 있다).
- 녹음 태스크(USB 클라이언트·분석)와 버퍼는 원조 One에서 처음 튜너를 열 때 만든다(스트림 버퍼는 PSRAM). 튜너를 쓰지 않으면 녹음 쪽 메모리는 쓰지 않는다.
  판단 태스크(`ForkTuner`, 스택 3KB, 50ms 주기)만 4.3B에서 부팅 때부터 돈다.
- 튜너 화면이 열린 동안만 받는다: 자체 USB Host 클라이언트로 장치를 열고 → 인터페이스 claim → SET_INTERFACE(alt)를 직접 보낸다(claim은 보내지 않는다)
  → UAC2는 클럭 소스 현재 레이트를 그대로 쓰고(같은 값 SET_CUR, CLOCK_VALID 확인), UAC1은 엔드포인트에 SET_CUR → 8패킷짜리 등시성 전송 3개를 돌린다.
  닫으면 alt 0으로 되돌리고 인터페이스를 놓고 전송 버퍼를 푼다. 놓기·닫기가 실패하면 핸들을 버리지 않고 남겨 두었다가 다시 시도한다(버리면 그 장치가 해제되지 않아 다시 꽂아도 열거되지 않는다).
  제어 요청이 끝나지 않으면(STALL 대신 NAK만) 콜백이 올 때까지 기다리고, 끝내 안 오면 그 장치에서는 더 시도하지 않는다. 파이프 오류로 전송이 모두 끝나면 한 번 다시 연다.
- 페달 쪽이 우선이다: 페달 값이 오면 녹음을 멈추고, 한 번 온 뒤에는 5초 끊겨야 녹음으로 넘어간다. 확인된 후보가 있으면 녹음은 1.5초 기다려 본 뒤 시작한다.
  그래서 페달 경로가 되면 녹음 경로는 열리지 않는다(녹음 경로를 따로 보려면 신호 출처를 USB 녹음만으로).
- 분석: 32~48kHz는 4분의 1, 88.2~96kHz는 두 번 줄여 YIN(창 약 53ms → 최저 약 38Hz, 베이스 E까지, 20ms마다), 원신호(또는 중간 신호)로 정밀화.
  RMS -50dBFS 아래는 무음. 주파수 3개 중앙값(옥타브 튐 제거), 같은 음이면 센트 평활, 40ms 이하 끊김은 앞 값 유지,
  지금 음에서 ±58센트를 넘어야 음 이름을 바꾼다(두 음 한가운데서 흔들리지 않게). 기준 주파수는 TONEX 설정(415~465Hz)을 따른다.
- 합성 소리 시험(`test_host/test_tuner.c`): A2, E2+10c, 베이스 E1, E4-23c, B5, 44.1kHz D3, 96kHz G3, 기준 432Hz가 ±1~2센트 안. 엔진 자체 정확도는 `test_pitch_yin.c`
  (73~659Hz 0.4센트 이하, 1318Hz 부근 1.1센트 이하).
- 진단: 고른 스트림(IF·alt·EP·UAC·채널·비트·mps·간격·동기·클럭), 걸러진 이유(쓸 스트림이 없을 때), 패킷 크기 판정(최소·여유), FIFO 경고(S3만),
  성공한 단계(open·claim·alt·rate-get·rate-set·clock-valid·submit), 레이트·클럭 유효, 패킷(정상·빈·건너뜀·오류)·버린 샘플·재시작, 채널별 피크,
  최근 음 높이·명료도, 실패 단계와 오류 코드(패킷이 다시 오면 "no packets" 류는 지운다), 내부 RAM 여유.
- 튜닝 중 소리 끄기는 넣지 않았다(녹음 신호가 같이 작아지는지 모름). 페달 튜너가 응답하면 페달이 스스로 끄거나 통과시킨다(본체 튜너 Mute/Thru).

진단 글은 보드 Setup > Tuner 탭(0.5초마다 갱신)과 웹 Config > Firmware > 튜너(1초마다, 복사 버튼)에 같은 내용이 나온다.

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
| `chocconf` | `tChocConfig` (packed) | 16 | `Version`(u8, 현재 2), `TranslateEnable`(u8), `Buttons[6]` = {`Action`(u8, `ChocActions`), `Param`(u8, 프리셋 번호 1~150)}, (2부터) `TunerHoldButton`(u8, 0~5, 0xFF 없음), `HoldTime`(u8, 10ms 단위 30~200, 10 단위) |
| `tunerconf` | `tTunerConfig` (packed) | 4 | `Version`(u8, 현재 1), `Mode`(u8, `TunerModes`), `Channel`(u8, `UacChannelModes`), `PedalCandidate`(u8, 튜너 값이 온 후보 번호 / 0xFF 모름. 번호가 저장되므로 후보 표는 뒤에만 추가) |

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
| (선택) 본체 풋스위치 튜너 시 미확인 메시지 | 포크 연결점이 모르는 메시지와 상태 메시지 변화를 기록해 Setup > Tuner 탭·웹 튜너 보고서에 보인다(3-5). |

### 실물 확인이 필요한 것

**초콜릿 신호** (맥 MIDI Monitor, 또는 웹 Chocolate 페이지·보드 Setup > Choc 탭의 강조로 확인)

- [ ] Program change A에서 A+B / C+D를 눌렀을 때 CC 2 / CC 3(값 5) 외에 PC도 같이 나오는지, 나온다면 같은 패킷인지
- [ ] 단일 버튼의 PC가 누를 때 나오는지, 뗄 때 나오는지 (탭템포 정확도)
- [ ] Program change B나 Advanced custom mode에 더 나은 동시 누르기 신호가 있는지
- [ ] Manufacturer Control 모드에서 A~D가 CC 20~23(127/0), E·F가 CC 24/25로 오는지, E·F를 뗄 때 무엇이 오는지(웹 Chocolate 페이지 안내가 "누름·뗌"으로 바뀌는지)
- [ ] 길게 눌러 튜너: 고른 버튼을 0.8초 누르면 튜너가 열리고 다시 길게 누르면 닫히는지, 짧게 누르면 원래 동작만 하는지
- [ ] E·F를 누른 채로 있을 때 A~D 눌림(CC 20~23 = 127)이 뒤따라 오지 않는지(오면 E·F 길게 누름 대기가 "E 뗌 유실"로 읽혀 취소된다)
- [ ] Manufacturer Control 모드에서 E(A+B)·F(C+D)를 밟을 때 길게 누름 버튼이 아닌 A~D 동작이 같이 실행되지 않는지.
      실행되면 초콜릿이 조합 전에 A~D 누름을 따로 보낸다는 뜻이다 → E·F 동작이 있는 쌍의 단일 누름을 수십 ms 늦춰 조합을 기다리는 처리가 필요하다(탭템포가 그만큼 늦어짐)

**튜너** — 보드 Setup > Tuner 탭 또는 웹 Config > Firmware > 튜너 보고서(복사해서 전달)

- [ ] 희망 1: 튜너 화면을 열자마자 줄을 계속 튕길 때(화면에 Asking pedal이 보이는 약 4초) 후보 줄에 `TUNER VALUES`가 나오는지.
      아니면 `reply XXXX`(무슨 응답이든 온 후보)와 `first reply` 원문이 있는지. 연주가 늦었으면 Setup > Tuner > Probe pedal을 누르고 바로 친다
- [ ] 자동 4개가 모두 안 되면 Setup > Tuner > Probe all(6개)을 한 번. 시험 중 프리셋·슬롯이 바뀌는지와 `State updates` 변화를 같이 본다
- [ ] `Requests` 줄의 `resent`, `Probes stopped` 줄의 `queue lost`가 0인지(0이 아니면 업스트림 큐가 요청을 버림 → 프리셋을 바꾸지 말고 다시 시험.
      `closed/unplugged/source changed`는 시험 중 닫기·뽑기·Source 변경 횟수다)
- [ ] 페달 본체 튜너(풋스위치 길게)를 켰다 끌 때 `State updates`의 바뀐 바이트나 `Unknown frames`가 생기는지
- [ ] 희망 2: Source를 USB audio only로 바꾸고 튜너를 연다(페달 경로가 되면 Auto에서는 녹음이 열리지 않는다).
      `USB audio` 절의 `Stream` 줄(FS에서 녹음 EP가 있는지, mps)과 `Packet` 판정(OK / TOO SMALL)
- [ ] 스트림이 열리면 `Packets ok`가 늘고 `Peak`가 기타 소리에 따라 움직이는지, `Pitch`가 맞는 음인지(채널 1/2/평균 비교)
- [ ] `FIFO` 경고나 `Error: claim`이 나오면 `sdkconfig.fork.ws43B`에 `CONFIG_USB_HOST_HW_BUFFER_BIAS_IN=y`를 넣고 다시 빌드
- [ ] 튜너를 쓰는 동안과 닫은 뒤 프리셋 변경·볼륨 등 기존 USB 제어가 그대로인지
- [ ] 녹음을 한동안 연 뒤 `Stack free bytes` 줄의 값이 300바이트 아래로 내려가지 않는지(내려가면 그 태스크 스택을 늘린다)
- [ ] 글로벌 볼륨 -40dB에서 USB 녹음 신호도 작아지는지(튜닝 중 소리 끄기를 넣을지 판단)

**볼륨 부스트**

- [ ] 부스트 ON/OFF 후 배지 아래 dB 값이 설정값과 0.2dB 이내인지 (TONEX One이 0~10 값을 내부에서 반올림하면 판정이 CUSTOM으로 나온다 → 허용 범위 조정)
- [ ] "dB"가 실제 음량 변화와 맞는지 (One의 0~10을 선형 환산한 값이라 실제 dB와 다를 수 있다)
- [ ] (빅 TONEX를 쓸 때만) 연결 직후 부팅 시 OFF가 적용되고 다른 전역 설정(튜닝 기준음 등)이 바뀌지 않는지

**기타**

- [ ] 보드 전원을 9V 1A 이상으로 교체

## 6. 실물 검증 순서

1. USB로 `WS43B_FORK` 설치(`docs/FIRMWARE_UPDATE.md`) → 9V 전원으로 켜고 TONEX One 연결 → 프리셋 동기화 후 배지가 "BOOST OFF -2.0 dB"인지(부팅 시 OFF).
2. 메인 화면 상단 상태 아이콘을 눌러 Setup 화면이 열리는지, 메인 화면 좌우 스와이프가 그대로인지. Info 탭 사진.
2-1. 튜너: 메인 화면 튜너 아이콘(또는 Setup > Tuner > Open tuner)으로 열자마자 기타 줄을 계속 튕긴다(Asking pedal 표시 동안). 웹 튜너 보고서를 복사한다.
     페달 본체 튜너를 켰다 끈 뒤 한 번 더 복사한다. Setup > Tuner의 Source를 USB audio only로 바꿔 다시 열고 한 번 더 복사한다(희망 2).
     Source를 Auto로 되돌리고, Setup > Choc에서 버튼 하나를 Tuner On/Off로 바꿔 초콜릿으로 열고 닫는다.
     초콜릿을 Manufacturer Control 모드로 바꾸고 Hold for tuner를 정해 길게 눌러 열고 닫는다(나머지 버튼 동작도 그대로인지).
3. CubeSuite를 Program change A로 바꾸고 A~D, E(A+B), F(C+D)를 밟으며 Setup > Choc 강조·웹 Chocolate 페이지 행 강조·보드 하단 알림 확인.
4. 부스트 토글 → 배지 ON/OFF, 웹 볼륨 표시, 실제 음량. 웹 Globals의 볼륨을 바꿔 CUSTOM이 나오는지.
5. 딜레이·공간계 토글이 실제 상태를 따라가는지(웹이나 보드에서 먼저 켠 뒤 초콜릿으로 끄기).
6. Setup > Misc에서 값 하나를 바꿔 Save & Reboot → 재부팅 뒤 웹 Config > Misc에도 같은 값인지. 드롭다운 목록을 끌어 스크롤되는지.
7. 웹 Firmware에서 같은 `TonexController.bin`을 다시 올려 Wi-Fi 업데이트와 재부팅 확인.
8. 초콜릿 반응 지연이 Wi-Fi 켜짐 여부에 따라 달라지는지(계획서 4-1).

## 7. 검증한 것

| 대상 | 방법 | 결과 |
| --- | --- | --- |
| 초콜릿 판별 | 호스트 테스트: PC 0~127 전부, CC2/CC3, 러닝 스테이터스, 잘린·깨진 패킷, 무작위 20만 패킷(ASan·UBSan), Manufacturer CC 20~25 누름·뗌 | 통과 |
| 길게 눌러 튜너 | 호스트 테스트: 짧게/길게, 만료 한 번, 늦은 만료, 다른 버튼 즉시, 누름 없는 뗌, A+B(먼저 온 A 흡수·B=0이 E 뗌), E 길게, E 뗌 유실 뒤 A·B 누름, PC 모드 무시, 설정 검사 | 통과 |
| 부스트 규칙 | 호스트 테스트: 판정·허용 범위·토글·검증, One 0~10 왕복 환산 전 범위 | 통과 |
| OTA 플랫폼 표식 검색 | 호스트 테스트: 1바이트~4KB 무작위 조각, 거의 같은 문자열, 처음/끝 위치 | 통과 |
| 음정 검출 | 호스트 테스트: 10개 음 × 5개 디튠 × 3가지 파형, 무음·잡음 | 통과 (3-5 수치) |
| 튜너 순수 계층 | 호스트 테스트: UAC2(선택기 포함)·UAC1·CDC만·잘린 디스크립터·형식 디스크립터 여러 개, TONEX One HS 덤프 재현(HS 56바이트 = 여유와 같음, FS는 최소 360·여유 368 필요), 24/16비트 변환, 스트리밍 음 판정 8종, 평활·옥타브 튐·음 경계, 요청 바이트 바꾸기, 페달 메시지 해석·값 검사 | 통과 |
| 튜너 출처·후보 시험 | 호스트 테스트(`tuner_logic.c`): 열자마자 온 값(끄지 않음), 자동 시험 진행·이번 부팅 한정 "없음"(저장 안 함, Pedal only는 매번), 요청 유실 시 재전송·중단, 연결 끊김, 시험 중 닫기, 닫기 뒤 늦은 켜기 버림, 다시 연 뒤 늦은 끄기 버림, 값이 흐르는 중 재시험(먼저 끄고 틈 뒤 같은 후보 다시 찾음), 재시험 실패 시 저장값 유지·복귀, 재시험 첫 단계 중단, 흐르던 값 미귀속, 화면 닫힌 수동 시험(확인 뒤 끄기), 수동 시험 중 열기, 시험 중 USB 녹음만 전환, 닫기 요청 유실(감시 타이머 닫힘·끄기 재전송), 닫은 뒤 늦은 열기(감시 타이머가 되돌림), 열린 화면 재요청(재연결·USB 녹음만에서 복귀·열기 요청 유실), 튜너 값 형식과 첫 응답 구분, 32비트 시각 넘김, USB 녹음만, 녹음 유예·페달 유지·복귀, 모르는 응답 원문, 후보 번호 범위·인코딩. 셀프리뷰 중 무작위 시뮬레이터(업스트림 큐 병합·멈춤·감시 타이머·빠른 조작·재연결, 저장소에 넣지 않음)로 20만 회 | 통과 (시뮬레이터 실패 0) |
| 웹 페이지 | 가짜 웹소켓 보드로 PC·휴대폰 폭 렌더링, Boost·Chocolate 따로 저장·검증·실행, 업로드(확정 대기·연속 클릭·읽기 실패)·USB 정보 | 통과 |
| 4.3B 화면 배치 | `test_host/ui_preview`: 업스트림 생성 UI + `display_fork*.c`를 맥에서 실행해 메인 화면(배지 4상태·알림·Setup 여는 자리·원조 One 튜너 아이콘), 튜너 화면 출처 표시 3가지, Setup 화면 8개 탭·드롭다운·저장 확인 창·길게 누름 안내, 페달 튜너 대기 표시를 PNG 27장으로 | 겹침 없음 (스킨 그림·실제 LCD 색감·터치는 보드 필요) |
| 펌웨어 빌드 | ESP32-S3 프리셋 23개 + `WS43B_FORK` (경고 0) | 통과 |
| 보드 화면·BLE·USB·OTA 실동작 | 실물 필요 | 미검증 |

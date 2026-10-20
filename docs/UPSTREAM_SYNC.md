# 업스트림 따라가기

이 포크는 Builty/TonexOneController에 PR을 보내지 않는다. 대신 업스트림 main을 계속 병합해 이어받는다.
그래서 업스트림 파일에는 연결(hook) 몇 줄만 넣고 기능은 포크 전용 파일에 둔다.

## 리모트와 브랜치

- `origin` = 개인 포크, `upstream` = Builty/TonexOneController
- 포크 `main` = 업스트림 main + 포크 커밋. 기능 작업은 `feature/etc_*` 브랜치에서 하고 `main`에 합친다.
- 업스트림 이력을 다시 쓰지 않도록 rebase 대신 merge 한다(푸시한 이력이 유지된다).

## 병합 절차

```bash
git fetch upstream
git checkout main
git merge upstream/main          # 충돌은 아래 연결 지점에서만 나야 정상
tools/check_fork_hooks.sh        # 연결 지점이 하나라도 빠지면 실패
cd source
./test_host/run_tests.sh
IDF_PRESET=WS43B_FORK idf.py build
IDF_PRESET=WS43B idf.py build
```

통과하면 `git push origin main`. main 푸시로 Actions가 크롬 설치 페이지(GitHub Pages)도 새 빌드로 갱신한다.
보드에는 Wi-Fi 업데이트로 올린다(`docs/FIRMWARE_UPDATE.md`). 업스트림이 스킨 이미지를 바꿨으면 USB 설치가 필요하다.

## 연결 지점

업스트림 파일에 넣은 줄에는 모두 `fork:` 주석이 있다. `git grep -n "fork:" -- source` 로 전부 볼 수 있다.

| 파일 | 연결 | 하는 일 | 업스트림이 이 부분을 바꾸면 |
| --- | --- | --- | --- |
| `main/CMakeLists.txt` | 끝에 `include(fork.cmake)` | 포크 소스·`fork.js` 빌드 | 줄만 다시 붙인다 |
| `main/control.c` | `control_load_config()` 끝 `fork_settings_load()` | 포크 설정 로드(NVS 초기화 뒤여야 함) | NVS 초기화 뒤 위치로 옮긴다 |
| `main/control.c` | `control_set_sync_complete()` 안 `boost_ctrl_on_sync_complete()` | 부팅 시 OFF 볼륨 | "프리셋 동기화 완료" 지점을 다시 찾는다 |
| `main/usb_comms.c` | 장치 오픈 뒤 `usb_desc_info_capture()` | Setup > Info 디스크립터 요약 | 설정 디스크립터를 얻은 직후로 |
| `main/usb_comms.c` | 장치 제거 시 `boost_ctrl_on_usb_disconnected()` | 부스트 상태 초기화 | `control_set_usb_status(0)` 옆으로 |
| `main/usb_tonex_one.c`, `usb_tonex_one_plus.c`, `usb_tonex_plug.c`, `usb_tonex.c` | 글로벌 볼륨 저장 뒤 `boost_ctrl_on_volume_report()` | 배지·웹 갱신 | `TONEX_GLOBAL_MASTER_VOLUME` 값을 쓰는 곳 뒤로 |
| `main/usb_tonex_one.c` | `usb_tonex_one_request_tuner()`의 `request[13] = state;` 뒤 `tuner_ctrl_prepare_one_request()`, `usb_tonex_one_parse()`의 형식 읽기 직후 `tuner_ctrl_on_one_frame()` | 원조 One 튜너 요청 번호 후보·응답·모르는 메시지 기록 | 요청 배열이 14바이트(`b9 03 81 형식 82 길이 80 셋째 b9 01 상태`)가 아니게 바뀌면 포크는 요청을 보내지 않는다(튜너 보고서 `layout changed`). 해석 함수에서 `type`을 읽는 줄 바로 뒤로 |
| `main/midi_control.c` | 스캔 매칭 `choc_exec_on_ble_connecting()`, 열기 성공 `choc_exec_on_ble_opened()`, 열기 실패·연결 끊김 `choc_exec_on_ble_disconnected()` | 초콜릿 연결 추적(연결 중/연결됨 표시) | 연결 대상 이름을 아는 지점, `ESP_GATTC_OPEN_EVT` 성공·실패 지점으로 |
| `main/midi_control.c` | `ESP_GATTC_NOTIFY_EVT`에서 `choc_exec_handle_ble_midi()` 실패 시에만 `midi_helper_process_incoming_data()` | 초콜릿 번역 | 업스트림 수신 호출을 감싸는 형태를 유지 |
| `main/display.c` | `UI_ELEMENT_FORK`, `UI_PostForkUpdate()`, 분기, 파라미터 갱신 뒤 `display_fork_on_parameters_refreshed()`, `display_init()`의 `display_fork_init()` | 배지·알림·Setup 화면 | `ui_init()` 뒤에 `display_fork_init()`이 와야 함 |
| `main/wifi_config.c` | `EVENT_SYNC_FORK`·`wifi_request_fork_push()`, 명령 분기 끝 `else` → `wifi_fork_handle_ws_command()`, `http_server_init()`의 핸들러 수·스택 증가와 `wifi_fork_register_handlers()` | 웹 명령·푸시·`/fork.js`·`/update` | 핸들러 등록은 와일드카드 `/*`보다 먼저 |
| `main/web/index.html` | `<script src="fork.js" defer>` | 웹 페이지 | `script.js` 뒤에 둔다 |

업스트림 파일이 아닌 것(포크 전용, 충돌 없음): `CLAUDE.md`, `docs/*`, `tools/*`(크롬 설치 페이지 `tools/web_installer/` 포함), `.github/workflows/fork-ws43b.yaml`, `source/CMakeUserPresets.json`,
`source/sdkconfig.fork.ws43B`, `source/partitions_16MB_ota.csv`, `source/components/tonex_fork_config/`, `source/test_host/`, `source/main/fork.cmake`, 2장의 포크 소스 파일.

## 업스트림 변경 유형별 확인

- **EEZ UI 재생성** (`ui_generated_800x480land/`): 생성 코드는 그대로 받는다. 메인 화면 배치가 바뀌면 `display_fork.c`의 배지 좌표(Bank 상자 아래)가 겹치지 않는지 본다.
  보드 없이 먼저 보려면 `WS43B_FORK` 빌드 뒤 `source/test_host/ui_preview/run_preview.sh [출력 폴더]`를 돌린다.
  업스트림 생성 UI와 `display_fork*.c`를 맥에서 그대로 실행해 메인 화면과 Setup 화면 탭들을 PNG로 남긴다(스킨 그림은 빈 채로 나온다).
  미리보기만 빌드가 실패하면 대개 업스트림 헤더가 새 ESP 헤더를 끌어온 것이므로 `ui_preview/stubs/`에 빈 헤더를 더한다(펌웨어 문제가 아니다).
  `objects.ui_touch_gesture_container`, `objects.screen1`, `img_tick` 이름이 바뀌면 빌드가 실패하므로 바로 드러난다.
  튜너는 `objects.tuner`, `objects.ui_tuner_button`, `action_tuner_pressed()`/`action_tuner_close()`(`actions.h`)에 기대고, 튜너 화면 출처 표시는
  원형 게이지 오른쪽(x 592, y 150) 빈 곳에 둔다. 튜너 화면 배치가 바뀌면 미리보기 20~22·27번으로 확인한다.
  상단 상태 아이콘 자리(Setup 화면을 여는 곳, `display_fork.c` `SETUP_ZONE_*`)는 좌표로 잡으므로 아이콘이 옮겨지면 미리보기 06번으로 확인한다.
- **업스트림 웹 Config(BT·Midi·Misc·WiFi) 변경**: 보드 Setup 화면의 같은 탭은 `display_fork_config.c` 표가 업스트림 `CONFIG_ITEM_*`과
  웹 `<option value>` 순서를 그대로 쓴다. 업스트림이 항목을 추가·삭제하거나 선택지 순서를 바꾸면 이 표도 맞춘다(항목 이름이 바뀌면 빌드가 실패한다).
- **웹 `script.js`**: `fork.js`는 전역 `processReturnCmd`, `sendWS`, `setToast`, `modellerType`, 메뉴의 "Config" 드롭다운 문구, Globals 메뉴의 `data-target="Global"`에 기대고 있다.
  이름이 바뀌면 웹 Boost·Chocolate·Firmware 메뉴가 사라진다. `index.html`을 브라우저로 열어 Globals 옆 Boost와 Config의 두 항목이 있는지 본다.
  또 `fork.js`는 "업스트림이 `GETMODELLERDATA`를 웹소켓이 열릴 때(`onOpen`)만 요청한다"를 재연결 신호로 쓴다(저장 결과 대기열 비우기, 업로드 뒤 버튼 해제).
  업스트림이 연결 중에도 이 요청을 보내게 바뀌면 빌드·메뉴는 멀쩡한데 저장 결과가 버려지므로, `script.js`의 `GETMODELLERDATA` 요청 지점을 확인한다.
- **설정 구조체·NVS** (`control.c`): 포크 설정은 별도 키라 영향이 없다. 업스트림이 `boostconf`/`chocconf`라는 키를 새로 쓰면 이름을 바꾼다.
- **파티션** (`partitions_16MB.csv`): 업스트림이 이 파일을 바꾸면 `partitions_16MB_ota.csv`에 같은 변경(특히 NVS·스킨 크기)을 반영한다. 스킨 이미지가 커지면 스킨 파티션(0x690000)이 충분한지 본다.
  크롬 설치 페이지와 파이썬 도구는 쓰기 주소를 빌드의 `flasher_args.json`에서 읽으므로 따로 고칠 곳이 없다.
- **`sdkconfig.ws43B`**: `WS43B_FORK`는 이 파일 위에 `sdkconfig.fork.ws43B`를 얹으므로 업스트림 변경을 자동으로 따른다. 파티션 관련 키가 추가되면 덮어쓰는지 확인한다.
- **저장소 루트 `index.html`**: 업스트림 바이너리와 업스트림 파티션 표 기준의 업스트림 크롬 설치 페이지다. 고치지 않고 그대로 받는다.
  포크 GitHub Pages는 이 파일이 아니라 Actions가 `tools/web_installer/`로 만든 사이트를 올린다.
- **ESP-IDF 버전**: 업스트림 `ci.yaml`의 `esp_idf_version`이 바뀌면 `fork-ws43b.yaml`과 로컬 환경도 같이 올린다.
- **업스트림 튜너 코드**: 포크는 업스트림이 튜너 화면을 열 때 보내는 요청(`control_request_tuner` → `usb_request_tuner` → `usb_tonex_one_request_tuner`)과
  `UI_SetTunerFrequencies(센트, 기준 주파수, MIDI 음 번호 / 0x80 = 음 없음)`의 뜻에 기댄다(화면 큐가 3개 넘게 차 있으면 값을 버린다).
  포크는 `usb_request_tuner()`의 상태 바이트에 후보를 실어(`0x80 | 후보 << 1 | 상태`) 보내고, `usb_tonex_one_request_tuner()`가 그 바이트를 요청 끝(`request[13]`)에 그대로 넣는다는 것에 기댄다.
  업스트림 입력 큐는 다음 메시지가 프리셋 변경이거나 같은 번호의 파라미터 변경이면 앞 메시지를 버린다. 포크는 버려진 요청을 시간 초과로 알아차린다(재전송 1회 → 중단).
  상태 바이트(0x80 이상)가 파라미터 번호와 겹치지 않아야 해서, `TONEX_GLOBAL_LAST`가 0x80을 넘으면 `tuner_ctrl.c`의 `_Static_assert`가 빌드를 멈춘다.
  포크는 업스트림이 원조 One의 첫 상태 메시지(0x0306)를 받은 뒤에만 입력 큐를 처리한다(READY)는 것에 기대어, 그 전에는 요청을 만들지 않는다.
  전송 실패(`tonex_common_transmit` 시간 초과)는 알 수 없어 보낸 것으로 본다(업스트림 다른 명령도 다시 보내지 않는다).
  입력 큐는 장치를 바꿔 꽂아도 비우지 않는다. 원조 One이 동기화 중일 때 쌓인 포크 요청이 남은 채 ONE+·Plug로 바꾸면 그 장치에 0x8X 상태로 나갈 수 있다(업스트림 메시지도 같은 성질).
  One의 수신 형식 목록(`usb_tonex_one_parse()`의 `case`)이 늘면 `tuner_proto.c`의 `tuner_is_known_one_type()`에도 넣는다.
  `tools/check_fork_hooks.sh`가 두 목록을 비교한다(넣지 않으면 그 메시지가 "모르는 메시지"로 진단에 쌓인다).
- **업스트림이 같은 기능을 추가**(예: 원조 One 튜너, 볼륨 표시): 포크 기능을 걷어낼지 비교한다.

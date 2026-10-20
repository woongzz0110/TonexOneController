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

통과하면 `git push origin main`. 보드에는 Wi-Fi 업데이트로 올린다(`docs/FIRMWARE_UPDATE.md`). 업스트림이 스킨 이미지를 바꿨으면 USB 설치가 필요하다.

## 연결 지점

업스트림 파일에 넣은 줄에는 모두 `fork:` 주석이 있다. `git grep -n "fork:" -- source` 로 전부 볼 수 있다.

| 파일 | 연결 | 하는 일 | 업스트림이 이 부분을 바꾸면 |
| --- | --- | --- | --- |
| `main/CMakeLists.txt` | 끝에 `include(fork.cmake)` | 포크 소스·`fork.js` 빌드 | 줄만 다시 붙인다 |
| `main/control.c` | `control_load_config()` 끝 `fork_settings_load()` | 포크 설정 로드(NVS 초기화 뒤여야 함) | NVS 초기화 뒤 위치로 옮긴다 |
| `main/control.c` | `control_set_sync_complete()` 안 `boost_ctrl_on_sync_complete()` | 부팅 시 OFF 볼륨 | "프리셋 동기화 완료" 지점을 다시 찾는다 |
| `main/usb_comms.c` | 장치 오픈 뒤 `usb_desc_info_capture()` | USB 탭 디스크립터 요약 | 설정 디스크립터를 얻은 직후로 |
| `main/usb_comms.c` | 장치 제거 시 `boost_ctrl_on_usb_disconnected()` | 부스트 상태 초기화 | `control_set_usb_status(0)` 옆으로 |
| `main/usb_tonex_one.c`, `usb_tonex_one_plus.c`, `usb_tonex_plug.c`, `usb_tonex.c` | 글로벌 볼륨 저장 뒤 `boost_ctrl_on_volume_report()` | 배지·웹 갱신 | `TONEX_GLOBAL_MASTER_VOLUME` 값을 쓰는 곳 뒤로 |
| `main/midi_control.c` | 스캔 매칭 `choc_exec_on_ble_connecting()`, 열기 실패·연결 끊김 `choc_exec_on_ble_disconnected()` | 초콜릿 연결 추적 | 연결 대상 이름을 아는 지점으로 |
| `main/midi_control.c` | `ESP_GATTC_NOTIFY_EVT`에서 `choc_exec_handle_ble_midi()` 실패 시에만 `midi_helper_process_incoming_data()` | 초콜릿 번역 | 업스트림 수신 호출을 감싸는 형태를 유지 |
| `main/display.c` | `UI_ELEMENT_FORK`, `UI_PostForkUpdate()`, 분기, 파라미터 갱신 뒤 `display_fork_on_parameters_refreshed()`, `display_init()`의 `display_fork_init()` | 배지·알림·FSW/USB 탭 | `ui_init()` 뒤에 `display_fork_init()`이 와야 함 |
| `main/wifi_config.c` | `EVENT_SYNC_FORK`·`wifi_request_fork_push()`, 명령 분기 끝 `else` → `wifi_fork_handle_ws_command()`, `http_server_init()`의 핸들러 수·스택 증가와 `wifi_fork_register_handlers()` | 웹 명령·푸시·`/fork.js`·`/update` | 핸들러 등록은 와일드카드 `/*`보다 먼저 |
| `main/web/index.html` | `<script src="fork.js" defer>` | 웹 페이지 | `script.js` 뒤에 둔다 |

업스트림 파일이 아닌 것(포크 전용, 충돌 없음): `CLAUDE.md`, `docs/*`, `tools/*`, `.github/workflows/fork-ws43b.yaml`, `source/CMakeUserPresets.json`,
`source/sdkconfig.fork.ws43B`, `source/partitions_16MB_ota.csv`, `source/components/tonex_fork_config/`, `source/test_host/`, `source/main/fork.cmake`, 2장의 포크 소스 파일.

## 업스트림 변경 유형별 확인

- **EEZ UI 재생성** (`ui_generated_800x480land/`): 생성 코드는 그대로 받는다. 메인 화면 배치가 바뀌면 `display_fork.c`의 배지 좌표(Bank 상자 아래)가 겹치지 않는지 보드에서 본다.
  `objects.ui_touch_gesture_container`, `objects.ui_settings_tab_view` 이름이 바뀌면 빌드가 실패하므로 바로 드러난다.
- **웹 `script.js`**: `fork.js`는 전역 `processReturnCmd`, `sendWS`, `setToast`, `modellerType`과 메뉴의 "Config" 드롭다운 문구에 기대고 있다. 이름이 바뀌면 웹 Footswitch·Firmware 메뉴가 사라진다.
  `index.html`을 브라우저로 열어 Config 메뉴에 두 항목이 있는지 본다.
- **설정 구조체·NVS** (`control.c`): 포크 설정은 별도 키라 영향이 없다. 업스트림이 `boostconf`/`chocconf`라는 키를 새로 쓰면 이름을 바꾼다.
- **파티션** (`partitions_16MB.csv`): 업스트림이 이 파일을 바꾸면 `partitions_16MB_ota.csv`에 같은 변경(특히 NVS·스킨 크기)을 반영한다. 스킨 이미지가 커지면 스킨 파티션(0x690000)이 충분한지 본다.
- **`sdkconfig.ws43B`**: `WS43B_FORK`는 이 파일 위에 `sdkconfig.fork.ws43B`를 얹으므로 업스트림 변경을 자동으로 따른다. 파티션 관련 키가 추가되면 덮어쓰는지 확인한다.
- **ESP-IDF 버전**: 업스트림 `ci.yaml`의 `esp_idf_version`이 바뀌면 `fork-ws43b.yaml`과 로컬 환경도 같이 올린다.
- **업스트림이 같은 기능을 추가**(예: 원조 One 튜너, 볼륨 표시): 포크 기능을 걷어낼지 비교한다.

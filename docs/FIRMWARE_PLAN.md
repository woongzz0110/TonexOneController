# TONEX 컨트롤러 보드 펌웨어 개조 계획

2026-10-20 · 차준웅

## 전제와 범위

Builty/TonexOneController를 포크해 세 가지를 추가한다: 원조 TONEX One용 튜너, 초콜릿 신호 번역 계층(웹 UI로 설정), 메인 화면 상태 표시. 기준 코드는 main 브랜치 9fd1156(2026-10-14), 펌웨어 V2.5.0.2다.

| 항목 | 내용 |
| --- | --- |
| 앰프 모델러 | TONEX One (원조, ONE+ 아님) |
| 컨트롤러 보드 | Waveshare ESP32-S3-Touch-LCD-4.3B (800×480, 빌드 설정 `sdkconfig.ws43B`) |
| 풋스위치 | M-Vave Chocolate (블루투스, CubeSuite · Program change A 모드 기준) |
| 연결 | 보드 USB-C ↔ TONEX One USB-C (제어 + TONEX One 전원 공급), 초콜릿 → 보드는 BLE (보드가 Central) |
| 전원 | 9V, 1A 이상 권장 (보드가 TONEX One까지 공급) |

목표는 페달보드를 TONEX One + 보드 + 초콜릿 세 개로 줄이는 것이다. 튜너 페달과 부스터 페달을 펌웨어 기능으로 대체한다.

## 1. 튜너 구현

TONEX One의 USB 오디오(녹음 채널)를 보드가 받아 직접 음정을 측정하고, 이미 있는 ONE+용 튜너 화면에 띄운다. Full-Speed USB에서 오디오 인터페이스가 나오는지가 유일한 관문이다.

### 왜 이 방식인가

- 원조 TONEX One은 튜너 데이터를 USB로 보내지 않는다. 제작자가 ONE+ 튜너 요청을 One에도 보내봤지만 반응이 없었다(`usb_tonex_one_request_tuner()` 주석 커밋 39d6aaf, 업스트림 이슈 #448).
  다만 보낸 요청은 ONE+ 메시지 번호 그대로였고, 같은 기능의 번호가 모델마다 다르다(구현 노트 3-5). One 번호 체계로 옮긴 요청은 아무도 시도하지 않았다.
- ONE+의 튜너 스트림(메시지 타입 `0x0310`)은 모바일 앱용이다. 원조 One은 모바일 앱이 없고 PC 에디터는 튜너를 다루지 않아서, One 펌웨어에 해당 기능이 있을 가능성은 낮다.
- 튜너 UI는 이미 있다. `display.c`의 `UI_SetTunerFrequencies(cents, ref_freq, midi_note)`에 값만 넣으면 원형 게이지와 음이름이 그려진다. 튜너 아이콘은 ONE+일 때만 보이므로 이 조건만 푼다.

### 가능성 근거

| 항목 | 내용 | 판단 |
| --- | --- | --- |
| TONEX One USB 구성 | 복합 장치, 인터페이스 5개: CDC + Audio Class 2.0. 녹음은 IF 3 alt 1, EP 0x84, 클럭 ID 7(공개된 HS 덤프) | 녹음 스트림 존재 |
| 오디오 형식 | 44.1kHz, 32비트 칸 24비트, 스테레오(처리된 소리, DI 없음) | 1ms에 360~368바이트 필요 |
| USB 하드웨어 FIFO | 기본 Balanced = 수신 약 408바이트, Bias IN = 약 600바이트 | 368바이트 패킷 수용 |
| CDC와 동시 사용 | ESP-IDF USB Host는 한 장치의 다른 인터페이스를 여러 클라이언트가 각각 사용 가능 | 구조상 가능 |
| **Full-Speed에서 오디오 제공 여부** | HS 덤프의 녹음 패킷은 56바이트(125µs용). FS에서도 56이면 담을 수 없다. ESP32-S3로 시도한 사람은 "HS가 필요해 보인다"고 결론 | **가능성 낮음, 실물로 확인 필요** |

### 구현 개요

1. 연결 시 Configuration Descriptor를 화면에 덤프한다. `usb_comms.c`의 `find_midi_streaming()`에 주석 처리된 인터페이스 로그가 있다. 4.3B는 USB가 하나라 시리얼 로그 대신 LCD 출력이 현실적이다.
2. Audio Streaming 인터페이스(클래스 0x01, 서브클래스 0x02)의 등시성 IN 엔드포인트를 찾는다. 없으면 이 방식은 폐기한다.
3. 드라이버: Espressif `usb_host_uac`는 UAC 1.0만 지원한다. UAC2로 나오면 최소 구현을 직접 쓴다: 인터페이스 claim → alt setting 선택 → 등시성 IN 전송 반복. 녹음만 하므로 수백 줄 규모다.
4. 음정 검출: 12kHz로 다운샘플 → YIN + 포물선 보간, 창 약 40ms·50% 겹침. 화면과 다른 코어에서 실행한다.
5. 튜닝 중 음소거: 진입 시 글로벌 볼륨 -40dB, 종료 시 원복. 단, USB 녹음 신호까지 작아지는지 확인 필요.

### 예상 성능

| 항목 | 예상치 | 비고 |
| --- | --- | --- |
| 정확도 | ±0.5~1센트 | PolyTune ±0.5센트, 스트로브 ±0.1센트 수준과 비교. 클럭 오차(±50ppm)는 0.1센트 미만 |
| 갱신 주기 | 약 20ms | 40ms 창을 절반씩 겹침 |
| 반응 지연 | 약 50~100ms | USB 1~5ms + 분석 창 + 화면 갱신. 일반 페달 튜너와 비슷 |
| 계산 부하 | 1~2ms/회 | 12kHz 다운샘플 기준 |

### 리스크

- Full-Speed에서 오디오 인터페이스가 없으면 불가능하다.
- 하이게인 프리셋 신호는 배음이 많아 옥타브 오인식 가능. 녹음 채널 중 DI(원음)가 있으면 그걸 쓴다.
- 컨트롤러 업스트림에서 ONE+ 튜너도 4.3B에서 음정이 안 나오는 이슈(#476)가 열려 있다. 튜너 화면 쪽 변경을 주시한다.

## 2. 초콜릿 신호 번역 계층

초콜릿에서 들어오는 신호만 가로채서 "버튼 A~F"로 판별하고, 사용자가 고른 동작을 펌웨어 내부 함수로 직접 실행한다. CC를 다른 CC로 바꿔 넘기지 않으므로, 나머지 펌웨어와 다른 MIDI 입력의 CC 의미는 그대로다.

### 신호 흐름

```
초콜릿 (BLE 전송)
  → BLE 수신 (midi_control.c, BLE Central)
  → [연결 기기가 초콜릿이고 번역 ON?]
       ├─ 예   → 버튼 판별 A~F (choc_map.c) → 동작 실행 (부스트·탭·프리셋·토글) → TONEX One (USB 제어)
       └─ 아니오 → 기존 MIDI 처리 (midi_helper.c) → TONEX One

다른 MIDI 입력 (유선·USB·웹) → 기존 MIDI 처리 (midi_helper.c) → TONEX One
tonex.local Footswitch 설정 화면 → (배치 읽기) → 버튼 판별 A~F
TONEX One 상태 → (상태 반영) → 보드 화면 (BOOST·볼륨 표시)

새로 추가하는 부분: tonex.local Footswitch 설정, choc_map.c(버튼 판별), 동작 실행, 보드 화면 표시
```

초콜릿 입력만 버튼 판별과 동작 실행을 거치고, 유선·USB·웹 입력은 기존 MIDI 처리를 그대로 탄다.

### 신호 규약 (CubeSuite Program change A 모드 기준)

| 버튼 | 초콜릿이 보내는 신호 | 보드의 판별 방법 |
| --- | --- | --- |
| A | PC n (초콜릿 내부 뱅크에 따라 n이 바뀜) | n % 4 == 0 |
| B | PC n | n % 4 == 1 |
| C | PC n | n % 4 == 2 |
| D | PC n | n % 4 == 3 |
| E (A+B) | CC 2, 값 5 (초콜릿 고정값) | 초콜릿 입력일 때만 E로 해석 |
| F (C+D) | CC 3, 값 5 (초콜릿 고정값) | 초콜릿 입력일 때만 F로 해석 |

- A~D는 PC 기준이다. 초콜릿 뱅크가 바뀌어도 나머지 4 계산으로 같은 버튼으로 판별된다.
- E/F는 Program change A 모드에서 CC로만 나온다(초콜릿 펌웨어 고정). 단, 이 CC는 번역 계층 안에서만 버튼 신호로 소비되고 뒤로 넘어가지 않는다. 그래서 CC 2가 딜레이 On/Off라는 의미는 바뀌지 않는다.
- 초콜릿에서 들어오는 여분의 신호(예: E/F와 함께 오는 PC)가 있으면 번역 계층에서 버린다. MIDI Monitor로 확인 후 확정한다.

### 동작 목록 (추상화된 액션)

| 동작 | 내부 처리 | 설정값 |
| --- | --- | --- |
| 없음 | 무시 | – |
| 부스트 토글 | 현재 볼륨이 부스트 ON 값이면 OFF 값으로, 아니면 ON 값으로 | 부스트 OFF·ON 값 (2-2) |
| 부스트 켜기 / 끄기 | 볼륨을 부스트 ON 값 / OFF 값으로 | 위와 공유 |
| 탭템포 | 기존 CC 10 처리 함수 호출 | – |
| 프리셋 업 / 다운 | 기존 CC 87 / 86 처리 함수 호출 | – |
| 프리셋 직접 선택 | 해당 번호로 변경 | 프리셋 번호 1~20 |
| 딜레이 On/Off | 현재 값을 읽어 반전 | – |
| 공간계(리버브) On/Off | 현재 값을 읽어 반전 | – |
| 모듈레이션 / 컴프 / 노이즈게이트 On/Off | 현재 값을 읽어 반전 | – |
| 튜너 켜기/끄기 | 1번 작업 완료 후 | – |

토글 동작은 모두 **보드가 알고 있는 실제 상태를 읽고** 반전한다. 초콜릿의 토글 상태와 실제 볼륨이 어긋나던 문제가 구조적으로 사라진다.

### 기본 배치 (제안)

| 버튼 | 동작 |
| --- | --- |
| A | 부스트 토글 |
| B | 탭템포 |
| C | 딜레이 On/Off |
| D | 공간계 On/Off |
| E (A+B) | 프리셋 다운 |
| F (C+D) | 프리셋 업 |

### 구현 위치

| 위치 | 변경 |
| --- | --- |
| `midi_control.c` 약 1082행 (BLE Central 수신) | 연결 기기가 초콜릿(`FootCtrl` / `FootCtrlPlus`)이고 번역이 켜져 있으면 `choc_map_process()`로, 아니면 기존 `midi_helper_process_incoming_data()`로 |
| 신규 `choc_map.c/.h` | 버튼 판별, 동작 enum, 동작 실행(기존 control/usb 함수 호출) |
| `control.c` 설정 구조체 | 버튼 6개의 동작·파라미터, 부스트 OFF/ON 값, 번역 사용 여부 추가. 기존 레거시 설정 마이그레이션 패턴을 따름 |
| `tonex_params` | 토글 시 `tonex_params_get_locked_access()`로 현재 값 읽기 |

번역이 켜져 있으면 보드의 Midi CC on BT 설정과 무관하게 동작한다. 동작을 내부 함수로 직접 실행하기 때문이다.

## 2-1. tonex.local 설정 화면

Config 메뉴에 **Footswitch** 페이지를 추가하고, 버튼 여섯 개에 동작 이름을 드롭다운으로 고른다. CC 번호나 값은 화면에 나오지 않는다.

### 화면 구성

| 행 | 화면 표시 | 입력 |
| --- | --- | --- |
| A | 버튼 A | 동작 드롭다운 |
| B | 버튼 B | 동작 드롭다운 |
| C | 버튼 C | 동작 드롭다운 |
| D | 버튼 D | 동작 드롭다운 |
| E | A + B 동시 | 동작 드롭다운 |
| F | C + D 동시 | 동작 드롭다운 |
| 공통 | 부스트 설정 | 부스트 OFF 값(dB), 부스트 ON 값(dB) |
| 공통 | 번역 사용 | 켜기/끄기 (끄면 원래 동작) |

- 드롭다운 항목은 위 **동작 목록**의 이름 그대로다: 없음, 부스트 토글, 부스트 켜기, 부스트 끄기, 탭템포, 프리셋 업, 프리셋 다운, 프리셋 직접 선택, 딜레이 On/Off, 공간계 On/Off, 모듈레이션 On/Off, 컴프 On/Off, 노이즈게이트 On/Off.
- **프리셋 직접 선택**을 고르면 그 행에만 프리셋 번호(1~20) 칸이 나타난다.
- 부스트 값은 dB로 입력하고, 입력 범위와 상태 판정은 2-2를 따른다.
- **마지막으로 누른 버튼 표시**: 초콜릿 신호가 들어오면 해당 행을 잠깐 강조한다. 밟아보면서 배치를 확인할 수 있다.
- 저장은 재부팅 없이 즉시 적용을 목표로 한다. 기존 페이지들의 Save and Reboot 방식은 필요할 때만 따른다.

### 구현 위치

| 위치 | 변경 |
| --- | --- |
| `web/index.html`, `web/script.js` | Footswitch 페이지, 드롭다운, 마지막 버튼 강조 |
| `wifi_config.c` | 설정 읽기/저장 메시지, 버튼 입력 이벤트를 웹소켓으로 전달 |
| `control.c` | 설정 항목 추가와 저장 |

## 2-2. 볼륨 부스트

화면과 웹에서는 "글로벌 볼륨" 대신 **볼륨 부스트**라는 이름으로, 부스트 OFF와 ON일 때의 볼륨 두 값만 설정한다. 내부에서는 TONEX One의 글로벌 볼륨(범위 -40~+3dB)을 바꾼다.

| 설정 | 예시 | 의미 |
| --- | --- | --- |
| 부스트 OFF 볼륨 | -2.0 dB | 평소 합주 상태 |
| 부스트 ON 볼륨 | +3.0 dB | 솔로 |
| 부스트 양 | +5.0 dB | ON − OFF. 자동 계산, 표시만 함 |
| 부팅 시 OFF로 시작 | 켜짐 | 켜면 부팅할 때 볼륨을 OFF 값으로 맞춤 |

### 입력 규칙

- 두 값 모두 -40~+3dB, 0.5dB 단위. OFF < ON이어야 저장된다.
- **ON 값은 +3dB를 넘을 수 없다.** TONEX One 글로벌 볼륨의 최대값이다. 예를 들어 OFF -1dB / ON +4dB(차이 5dB)는 불가능하므로, OFF -2dB / ON +3dB로 같은 부스트 양을 내고 평소 음량은 앰프나 믹서에서 맞춘다.
- 범위를 넘는 값을 입력하면 저장 전에 이 대안을 안내한다.

### 상태 판정

| 현재 볼륨 | 상태 |
| --- | --- |
| ON 값과 같음 (±0.2dB) | 부스트 ON |
| OFF 값과 같음 (±0.2dB) | 부스트 OFF |
| 그 외 (웹·다른 MIDI로 바뀜) | 사용자 지정 |

- 부스트 토글: ON이면 OFF 값으로, OFF나 사용자 지정이면 ON 값으로 보낸다.
- 상태는 보드가 추적하는 실제 볼륨(`TONEX_GLOBAL_MASTER_VOLUME`)으로 매번 판정하므로, 초콜릿·웹·재부팅 어느 경로로 바뀌어도 어긋나지 않는다.
- 참고: 글로벌 볼륨 MIDI 값 매핑은 dB = -40 + (값/127) × 43 (`midi_helper_scale_midi_to_float`, `tonex_params.c`의 min -40 / max 3). 번역 계층은 MIDI 값을 거치지 않고 dB(float)로 직접 설정한다.

## 3. 보드 화면 상태 표시

메인 화면에 볼륨 부스트 상태를 ON/OFF로 크게 보여주고, dB 숫자는 보조 정보로 작게 표시한다. 지금은 메인 화면에 볼륨 정보가 없고, 웹 설정 화면에 들어가야만 볼 수 있다.

| 표시 | 내용 | 위치 (제안) |
| --- | --- | --- |
| 부스트 상태 배지 | ON = 강조색 채운 배지 "BOOST ON", OFF = 회색 배지 "BOOST OFF", 사용자 지정 = 테두리만 있는 배지 "CUSTOM" | 프리셋 이름 옆 또는 우상단 |
| 현재 볼륨 | 배지 아래 작은 글씨 (예: "+3.0 dB") | 배지 아래 |
| 이펙트 상태 | 딜레이·공간계·모듈레이션 On/Off는 하단 아이콘으로 이미 표시됨 | 기존 유지 |
| 마지막 동작 알림 | 초콜릿으로 실행한 동작 이름을 1초간 표시 (예: "부스트 ON", "탭템포", "딜레이 OFF") | 화면 하단 토스트 |

- 배지 상태는 2-2의 상태 판정 규칙을 그대로 쓴다. 무대에서는 색으로, 가까이서는 글자로 구분된다.
- 볼륨은 이미 `TONEX_GLOBAL_MASTER_VOLUME` 파라미터로 추적되고 있다. 값이 바뀔 때 배지와 라벨 갱신만 추가하면 된다.
- 화면 갱신은 배지와 글자만 바꾼다. 프리셋 변경 때처럼 앰프 스킨 전체를 다시 그리지 않아서 빠르다.

### 구현 위치

| 위치 | 변경 |
| --- | --- |
| `ui_design_800x480land/Tonex_Controller_800_480land.eez-project` (EEZ Studio) → `source/main/ui_generated_800x480land/` 재생성 | BOOST 배지, 볼륨 라벨, 토스트 객체 추가 |
| `display.c` | 파라미터 변경 수신 시 라벨 갱신, 기존 UI 업데이트 큐 방식을 따름 |
| `choc_map.c` | 동작 실행 시 토스트 요청 |

## 4. 설정 접근성 개선

폰 Wi-Fi를 바꾸지 않고 설정하기(코드 수정 없음)와, 버튼 배치를 보드 화면에서도 바꾸기(코드 수정) 두 가지다.

### 4-1. Station 모드 + 폰 핫스팟 (지금 바로 가능)

1. tonex.local → Config → WiFi에서 모드를 **Station**으로 바꾸고, SSID/비밀번호에 폰 핫스팟(또는 집 공유기)을 넣는다.
2. 저장하면 재부팅 후 보드가 핫스팟에 붙고, 화면에 IP 주소가 토스트로 뜬다.
3. 폰 브라우저에서 그 IP로 접속한다. 핫스팟에서는 `tonex.local` 이름 해석이 안 될 수 있다.

- 폰은 데이터로 인터넷을 쓰면서 보드에 접속하므로 Wi-Fi를 끊을 필요가 없다.
- Wi-Fi와 블루투스가 안테나를 공유해서, Wi-Fi가 켜져 있으면 초콜릿 반응이 늦어질 수 있다. 연주 중에는 핫스팟을 끄거나 Access Point Timed로 돌린다.
- 다른 대안은 막혀 있다. USB는 포트가 하나라 TONEX One 전용이고, 블루투스는 초콜릿(Central)에 쓰고 있어 Peripheral과 동시 사용이 안 되며, 아이폰 사파리는 웹 블루투스를 지원하지 않는다.

### 4-2. 보드 화면에 Footswitch 탭 추가

기존 화면 설정 다이얼로그(`ui_settings_tab_view`, 탭 Gate·Comp·Amp·EQ·Mod·Dly·Rev·Glob)에 **Footswitch** 탭을 더해, 버튼 A~F의 동작을 드롭다운(LVGL `lv_dropdown`)으로 고른다. 웹 Footswitch 페이지(2-1)와 같은 설정값을 공유한다. 같은 탭에서 볼륨 부스트 OFF·ON 값(2-2)도 설정한다.

| 항목 | 내용 |
| --- | --- |
| 재부팅 | 불필요. 버튼 배치는 실행 중에 읽는 값이라 저장 즉시 적용 (`control_save_user_data(0)`) |
| 수정 범위 | `ui_design_800x480land/` EEZ Studio 프로젝트에 탭 추가 후 코드 재생성, `display.c`에 이벤트 처리 |
| 대상 화면 | 4.3B(800×480)만. 다른 화면 크기 5종은 웹 설정으로 충분 |
| 불편한 점 | 보드가 바닥에 있어 터치가 불편하다. 처음 세팅용으로 쓴다 |

BT·Wi-Fi·유선 MIDI 같은 시스템 설정은 부팅 때 초기화되어 저장 후 재부팅이 필요하다(`control_save_user_data(1)`). 화면으로 옮기는 것은 이번 범위에서 제외한다.

### 4-3. 참고: 화면 렌더링과 성능

- 보드 화면은 LVGL이 글자·슬라이더·탭을 코드로 그리고, 앰프 스킨과 이펙트 아이콘만 이미지다(플래시 10MB `skins` 영역). ESP32-S3에서 HTML 렌더링은 불가능하다.
- 프리셋 전환이 느린 주원인은 PSRAM에 있는 화면 버퍼 2장(800×480, 각 약 750KB)과 큰 스킨 이미지의 전체 재그리기다. 라벨만 바뀌는 3번 항목의 표시는 영향이 작다.
- 개선하려면 스킨 끄기(또는 작은 스킨) 옵션을 검토한다.

## 구현 전 확인할 것

두 가지 도구로 확인한다: 맥용 무료 앱 MIDI Monitor(Snoize)로 초콜릿 신호를, 화면 출력 디버그 빌드로 TONEX One USB 구성을 본다.

**초콜릿 신호 (번역 계층용)**

- [ ] Program change A에서 A+B / C+D를 눌렀을 때 CC 2 / CC 3(값 5) 외에 PC도 같이 나오는지
- [ ] 단일 버튼의 PC가 누를 때 나오는지, 뗄 때 나오는지 (탭템포 정확도)
- [ ] MIDI 채널과, 뱅크가 끝에서 처음으로 돌아가는지
- [ ] Program change B나 Advanced custom mode에 더 나은 동시 누르기 신호가 있는지

**TONEX One USB (튜너용)**

- [ ] Full-Speed 연결에서 Audio Streaming 인터페이스와 등시성 IN 엔드포인트가 있는지
- [ ] UAC 버전(1 또는 2), 최대 패킷 크기, 샘플레이트
- [ ] 녹음 채널이 처리된 신호인지 DI(원음)인지
- [ ] 글로벌 볼륨 -40dB에서 USB 녹음 신호도 작아지는지
- [ ] (선택) 본체 풋스위치로 튜너를 켰을 때 USB로 미확인 메시지(`Unknown type`)가 들어오는지
- 위 항목은 포크 튜너 보고서(Setup > Tuner, 웹 Config > Firmware > 튜너)에 한 번에 나온다(구현 노트 5장).

**기타**

- [ ] 업스트림 V3.0.0.2 알파(4.3B 화면 깜빡임 수정, LCD 타이밍 조정) 기준으로 포크할지 결정
- [ ] 보드 전원을 9V 1A 이상으로 교체

## 참고: 현재 사용 중인 설정 (개조 전)

- 보드 BT: Bluetooth Mode = Central, Chocolate Series = 켜짐, Midi CC on BT = 켜짐
- 초콜릿(CubeSuite)은 개조 전까지 커스텀 모드로 A=볼륨 부스트 토글(CC 122), B=기본 볼륨 복귀(CC 122), C=프리셋 다운(CC 86), D=프리셋 업(CC 87)을 임시로 쓰고 있다. 번역 계층 적용 후에는 **Program change A 모드로 되돌린다.** 길게 눌러 튜너를 쓰려면 대신 **Manufacturer Control 모드**로 둔다(뗄 때도 신호가 와야 길게 누름을 안다, 구현 노트 3-2).
- 초콜릿 기본 모드의 동시 누르기(CC 2/3, 값 5)는 TONEX의 딜레이 On/Off·타입과 번호가 겹친다. 그래서 업스트림은 Midi CC on BT를 기본으로 꺼둔다.

## 출처

- [Builty/TonexOneController](https://github.com/Builty/TonexOneController) — 소스(`usb_tonex_one.c`, `usb_tonex_one_plus.c`, `usb_comms.c`, `midi_control.c`, `midi_helper.c`, `tonex_params.c`, `display.c`)와 커밋 이력
- [MidiCommands.md](https://github.com/Builty/TonexOneController/blob/main/MidiCommands.md) — 컨트롤러 CC 대응표
- [HardwarePlatform_Waveshare4.3B.md](https://github.com/Builty/TonexOneController/blob/main/HardwarePlatform_Waveshare4.3B.md) — 4.3B 배선·전원
- [Issue #476 — ToneX One+ Tuner not working on ESP 4.3B](https://github.com/Builty/TonexOneController/issues/476)
- [Issue #448](https://github.com/Builty/TonexOneController/issues/448) — ONE+ 튜너 요청을 One에 보냈으나 무시, ESP32-S3 녹음 시도 결론 / [Discussion #224](https://github.com/Builty/TonexOneController/discussions/224) — One 원격 튜너 없음
- [tx-one-control 하드웨어 덤프](https://github.com/p-prakapienka/tx-one-control/blob/HEAD/docs/hardware-probes/tonexprobe20260819_220308.log.txt) — TONEX One HS 디스크립터(녹음 EP 0x84, mps 56, 클럭 7)
- [Tonex-Linux-Fix](https://github.com/elroyski/Tonex-Linux-Fix) — 44.1kHz S32_LE 스테레오, 클럭 초기화 순서 / [madejm usb-audio-workbench](https://github.com/madejm/TonexOneController/tree/usb-audio-workbench) — ESP32-S3 녹음 시도
- [MOD Audio Forum — MIDI "Chocolate" controller 튜토리얼](https://forum.mod.audio/t/midi-chocolate-controller-with-the-mod-dwarf-an-introduction/7003) — CubeSuite 모드별 신호
- [Espressif usb_host_uac](https://components.espressif.com/components/espressif/usb_host_uac) — UAC 1.0만 지원
- [esp-uac2-host](https://tomevault.io/tome/Averyy/esp-uac2-host) — 커뮤니티 UAC2 호스트 드라이버
- [ESP-IDF USB Host FIFO 설정 (Kconfig)](https://codex.ac6.fr/Zephyr/raw/hal_espressif-3.6.0/components/usb/Kconfig)

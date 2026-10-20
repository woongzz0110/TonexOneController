# 펌웨어 업데이트 방법 (Waveshare 4.3B, 포크 펌웨어)

보드 USB-C는 평소 TONEX One에 연결돼 있어서, 처음 한 번만 USB로 설치하고 그 뒤로는 Wi-Fi로 업데이트한다.

| 상황 | 방법 | 필요한 파일 |
| --- | --- | --- |
| 포크 펌웨어 첫 설치 (업스트림 펌웨어에서 넘어올 때) | USB (크롬 설치 페이지) | 설치 묶음 전체 (파티션 표가 바뀜) |
| 그 뒤 업데이트 | Wi-Fi (웹 Config > Firmware) | `TonexController.bin` 하나 |
| 스킨 이미지가 바뀐 업스트림 버전을 받았을 때 | USB (크롬 설치 페이지) | 설치 묶음 전체 |
| Wi-Fi 업데이트가 안 될 때, 복구 | USB (크롬 설치 페이지) | 설치 묶음 전체 |

크롬 설치 페이지: https://woongzz0110.github.io/TonexOneController/ (main에서 마지막으로 성공한 빌드. 설치 묶음 전체를 페이지가 알아서 쓴다)

## 파일 받기

크롬 설치 페이지로 USB 설치할 때는 받을 파일이 없다. Wi-Fi 업데이트용 `TonexController.bin`이나 파이썬 도구용 설치 묶음은 아래 중 하나에서 받는다.

- 크롬 설치 페이지의 `TonexController.bin 받기` (main에서 마지막으로 성공한 빌드)
- 맥에서 빌드: `cd source && IDF_PRESET=WS43B_FORK idf.py build` → `source/build/fork_ws43b/`
  - Wi-Fi용: `source/build/fork_ws43b/TonexController.bin`
- GitHub Actions(맥 없이 폰으로 가능): 포크 저장소 Actions 탭에서 "Fork WS43B build" 실행 결과의 산출물
  - `ws43b-fork-wifi-update` = `TonexController.bin`
  - `ws43b-fork-usb-install` = USB 설치 묶음(부트로더·파티션 표·otadata·앱·스킨·`flasher_args.json`·설치 도구)
  - 포크 저장소는 Actions가 기본으로 꺼져 있다. 처음 한 번 Actions 탭에서 켜야 한다. 이때 업스트림 워크플로 "ESP-IDF Build"(24개 플랫폼 빌드)도 같이 켜지므로, 필요 없으면 그 워크플로만 Disable 한다.
  - 같은 워크플로가 main 빌드일 때만 크롬 설치 페이지를 GitHub Pages에 올린다. 처음 한 번 Settings > Pages의 Source를 **GitHub Actions**로 바꿔야 한다.
    이때 생기는 `github-pages` 환경은 기본 브랜치(main)에서만 배포를 허용하므로 기능 브랜치 빌드는 페이지에 올라가지 않는다.

## USB 설치 (첫 설치·복구)

### 크롬 설치 페이지

데스크톱 크롬·엣지에서 https://woongzz0110.github.io/TonexOneController/ 를 연다. 페이지 맨 위 "펌웨어 버전"이 설치될 빌드다(보드 Setup 화면 Info 탭의 버전과 비교).

1. TONEX One에서 USB 케이블을 빼고, 보드 **Boot 버튼을 누른 채** 컴퓨터에 USB-C로 연결한 뒤 버튼을 뗀다.
2. "USB로 설치" → 포트(보통 `USB JTAG/serial debug unit`) 선택 → `Install Tonex Controller fork` → `Erase device` 체크 여부 → `Next` → `Install`.
   - **`Erase device`를 체크하면 플래시 전체를 지운다.** 보드 설정(BT·Wi-Fi·프리셋 순서 등 업스트림 설정과 포크 부스트·초콜릿 설정)이 모두 기본값으로 돌아간다.
     설정을 유지하려면 체크하지 않는다. 업스트림 펌웨어에서 처음 넘어올 때도 체크하지 않아도 된다(NVS 위치가 같다).
   - 체크하지 않으면 부트로더·파티션 표·otadata·앱·스킨만 쓴다(파이썬 도구 기본 동작과 같다). 쓰는 주소는 빌드의 `flasher_args.json`에서 가져온다.
3. USB를 빼고 9V 전원으로 켠다. 컴퓨터 USB 전원만으로는 화면이 깨진다.

기능 브랜치 빌드나 맥에서 빌드한 펌웨어를 같은 방식으로 올리려면 페이지를 로컬에 띄운다(Web Serial은 localhost에서도 된다).
CI 설치 묶음을 푼 폴더도 빌드 폴더 대신 쓸 수 있다.

```bash
python3 tools/web_installer/build_site.py source/build/fork_ws43b /tmp/tonex_site
python3 -m http.server -d /tmp/tonex_site 8000    # 크롬으로 http://localhost:8000
```

### 파이썬 도구 (대안)

크롬을 쓸 수 없을 때, 또는 스킨을 건너뛰는 등 일부만 쓰고 싶을 때.

1. TONEX One에서 USB 케이블을 빼고, 보드 **Boot 버튼을 누른 채** 맥에 USB-C로 연결한 뒤 버튼을 뗀다.
2. 포트 확인: `ls /dev/cu.usbmodem*`
3. 설치 (ESP-IDF 환경, 또는 `pip install "esptool>=5"` 후):

   ```bash
   python3 tools/flash_fork_ws43b.py /dev/cu.usbmodemXXXX
   ```

   - CI 설치 묶음을 쓸 때: 압축을 푼 폴더에서 `python3 flash_fork_ws43b.py /dev/cu.usbmodemXXXX .`
   - 보드 설정을 지우려면 `--erase-settings`. 지우지 않아도 기존 설정(BT·Wi-Fi·프리셋 순서 등)은 그대로 남는다(NVS 위치가 같다).
   - 같은 파티션 표로 다시 설치할 때는 `--no-skins`로 스킨(약 5.9MB) 쓰기를 건너뛸 수 있다. 도구가 보드의 파티션 표를 먼저 읽어 이 빌드와 다르면(업스트림에서 처음 넘어올 때) 거부한다.
4. USB를 빼고 9V 전원으로 켠다. 맥 USB 전원만으로는 화면이 깨진다.

두 방법 모두 otadata를 함께 써서 `ota_0`으로 부팅하게 만든다. Wi-Fi 업데이트를 한 번 하면 부트로더가 `ota_1`을 부팅하므로, 앱 이미지만 `0x10000`에 다시 써서는 반영되지 않는다.

## Wi-Fi 업데이트

1. 폰을 보드에 연결한다.
   - Access Point 모드(기본 "Access Point Timed"): 보드를 켠 뒤 **1분 안에** 폰 Wi-Fi를 `TonexConfig`(비밀번호 `12345678`, 바꿨다면 그 값)에 붙인다. 1분 안에 아무도 붙지 않으면 Wi-Fi가 꺼진다.
   - Station 모드: 보드와 폰을 같은 핫스팟·공유기에 붙인다(계획서 4-1).
2. 브라우저로 `http://tonex.local` 또는 `http://192.168.4.1`(AP 모드) / 보드 화면에 뜬 IP(Station 모드)에 접속한다.
3. Config > **Firmware**: 현재 버전과 "Wi-Fi 업데이트 가능"이 보이는지 확인 → `TonexController.bin` 선택 → 업로드.
   (현재 버전과 업데이트 가능 여부는 보드에서도 볼 수 있다: 메인 화면 상단 상태 아이콘 → Setup 화면 Info 탭)
4. 100%가 되면 보드가 재부팅한다. AP 모드는 폰 Wi-Fi가 끊기므로 다시 붙는다.

### 안전장치

- 브라우저가 올리기 전에, 보드가 받는 동안 한 번 더 이미지 헤더(ESP32-S3, 프로젝트 `TonexController`)와 포크 플랫폼 표식(`WS43B`)을 확인한다.
  업스트림 펌웨어 이미지, 다른 보드용 이미지, 설치 묶음의 다른 파일(bootloader·partition·skins)은 거부된다.
- 업로드가 중간에 끊기거나 검증에 실패하면 기존 펌웨어로 계속 동작한다.
- 새 펌웨어는 켜진 뒤 웹 서버를 띄우고 **30초 동안 재부팅되지 않으면 확정**된다. 그 30초 안에 재부팅되면(오류로 리셋, 전원 껐다 켜기) 부트로더가 이전 펌웨어로 되돌린다.
  멈춘 채로 30초가 지나면 확정되므로, 새 펌웨어가 이상하면 켜진 뒤 30초 안에 전원을 껐다 켠다.
- 직전 업데이트가 확정되기 전(켜진 뒤 30초 이내)에는 다시 올릴 수 없다. 화면 안내대로 잠시 뒤 다시 올린다.
- 롤백까지 실패하면 USB 설치로 복구한다(보드 ROM 다운로드 모드는 항상 살아 있다).
- `/update`에는 인증이 없다(업스트림 웹 설정 전체와 같다). 보드 AP 비밀번호(기본 `12345678`)를 바꿔 두고, Station 모드는 믿을 수 있는 네트워크에서만 쓴다.

## 업스트림 펌웨어로 돌아가기

업스트림 배포판은 파티션 표가 다르다(`factory` 하나 + 스킨 위치 0x4F2000). Wi-Fi로는 올라가지 않으므로 USB로 설치한다.

- 웹 도구 https://builty.github.io/TonexOneController/ 에서 Waveshare 4.3B 선택 → 설치(부트로더·파티션 표·스킨까지 전부 써진다). 포크 설정 키(`boostconf`, `chocconf`)는 남아도 업스트림이 무시한다.

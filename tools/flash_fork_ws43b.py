#!/usr/bin/env python3
"""USB로 포크 펌웨어(WS43B_FORK)를 Waveshare 4.3B에 설치한다.

사용:
  python3 tools/flash_fork_ws43b.py PORT [DIR] [--no-skins] [--erase-settings]

  PORT   예: /dev/cu.usbmodem1101 (보드 Boot 버튼을 누른 채 USB 연결)
  DIR    flasher_args.json 이 있는 폴더. 기본값 source/build/fork_ws43b
         (GitHub Actions 산출물 zip을 푼 폴더도 된다)

  --no-skins       스킨 이미지(약 5.9MB)를 건너뛴다. 스킨 파티션 위치가 바뀌는 첫 설치에는 쓰지 말 것.
  --erase-settings 보드 설정(NVS)을 지운다.

esptool 5 이상이 필요하다: ESP-IDF 환경에서 실행하거나 `pip install "esptool>=5"`.
otadata는 항상 함께 쓴다. Wi-Fi 업데이트 뒤에는 부트로더가 ota_1로 부팅하므로, 앱만 다시 쓰면 반영되지 않는다.
"""
import json
import os
import shutil
import subprocess
import sys

NVS_OFFSET = "0x9000"
NVS_SIZE = "0x4000"


def find_esptool():
    exe = shutil.which("esptool") or shutil.which("esptool.py")
    if exe:
        return [exe]
    return [sys.executable, "-m", "esptool"]


def resolve(directory, path):
    candidate = os.path.normpath(os.path.join(directory, path))
    if os.path.exists(candidate):
        return candidate
    flat = os.path.join(directory, os.path.basename(path))
    if os.path.exists(flat):
        return flat
    sys.exit(f"파일을 찾을 수 없습니다: {path} (폴더 {directory})")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    options = {a for a in sys.argv[1:] if a.startswith("--")}
    unknown = options - {"--no-skins", "--erase-settings"}
    if not args or unknown:
        sys.exit(__doc__)

    port = args[0]
    here = os.path.dirname(os.path.abspath(__file__))
    directory = args[1] if len(args) > 1 else os.path.join(here, "..", "source", "build", "fork_ws43b")
    meta_path = os.path.join(directory, "flasher_args.json")
    if not os.path.exists(meta_path):
        sys.exit(f"{meta_path} 가 없습니다. 먼저 IDF_PRESET=WS43B_FORK idf.py build 를 하거나 CI 산출물 폴더를 지정하세요.")

    with open(meta_path) as f:
        meta = json.load(f)

    if meta.get("extra_esptool_args", {}).get("chip") != "esp32s3":
        sys.exit("ESP32-S3용 빌드가 아닙니다.")

    files = []
    for offset, path in sorted(meta["flash_files"].items(), key=lambda item: int(item[0], 16)):
        if "--no-skins" in options and "skins" in os.path.basename(path):
            continue
        files += [offset, resolve(directory, path)]

    if not any("TonexController" in f for f in files[1::2]):
        sys.exit("TonexController.bin 이 없습니다.")

    esptool = find_esptool()
    common = esptool + ["--chip", "esp32s3", "-p", port, "-b", "460800"]
    settings = meta.get("flash_settings", {})

    if "--erase-settings" in options:
        subprocess.run(common + ["--after", "no-reset", "erase-region", NVS_OFFSET, NVS_SIZE], check=True)

    command = common + ["--before", "default-reset", "--after", "hard-reset", "write-flash",
                        "--flash-mode", settings.get("flash_mode", "dio"),
                        "--flash-size", settings.get("flash_size", "16MB"),
                        "--flash-freq", settings.get("flash_freq", "80m")] + files
    print(" ".join(command))
    subprocess.run(command, check=True)
    print("완료. USB를 빼고 9V 전원으로 켜세요.")


if __name__ == "__main__":
    main()

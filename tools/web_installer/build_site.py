#!/usr/bin/env python3
"""포크 펌웨어 웹 설치 페이지(ESP Web Tools)를 한 폴더로 만든다: index.html + manifest.json + 설치 파일.

사용: python3 tools/web_installer/build_site.py BUILD_DIR OUT_DIR

  BUILD_DIR  flasher_args.json 이 있는 폴더 (예: source/build/fork_ws43b, CI 설치 묶음을 푼 폴더도 된다)
  OUT_DIR    만들 폴더. GitHub Pages에 그대로 올린다.

쓰기 주소는 flasher_args.json 에서 읽는다(파티션 표를 바꾸면 따라 바뀐다).
로컬에서 보려면: python3 -m http.server -d OUT_DIR 8000 → http://localhost:8000
"""
import json
import os
import shutil
import struct
import sys

CHIP_FAMILIES = {"esp32s3": "ESP32-S3"}
WIFI_IMAGE = "TonexController.bin"
# 설치 묶음에 꼭 있어야 하는 flasher_args.json 항목. 업스트림이 스킨 등록 방식을 바꿔 빠지면 스킨 없는 페이지가 조용히 배포된다.
REQUIRED_SECTIONS = ("bootloader", "partition-table", "otadata", "app", "skins")
APP_DESC_MAGIC = 0xABCD5432
# 이미지 헤더(24) + 첫 세그먼트 헤더(8) 뒤에 esp_app_desc_t(256바이트)가 온다.
APP_DESC_OFFSET = 32
APP_DESC_SIZE = 256


def resolve(directory, path):
    candidate = os.path.normpath(os.path.join(directory, path))
    if os.path.exists(candidate):
        return candidate
    flat = os.path.join(directory, os.path.basename(path))
    if os.path.exists(flat):
        return flat
    sys.exit(f"파일을 찾을 수 없습니다: {path} (폴더 {directory})")


def read_app_desc(app_path):
    with open(app_path, "rb") as f:
        head = f.read(APP_DESC_OFFSET + APP_DESC_SIZE)
    if len(head) < APP_DESC_OFFSET + APP_DESC_SIZE or struct.unpack_from("<I", head, APP_DESC_OFFSET)[0] != APP_DESC_MAGIC:
        sys.exit(f"{app_path} 에 앱 정보(esp_app_desc)가 없습니다.")
    version, project = struct.unpack_from("<32s32s", head, APP_DESC_OFFSET + 16)
    return version.split(b"\0")[0].decode(), project.split(b"\0")[0].decode()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build_dir, out_dir = sys.argv[1], sys.argv[2]

    with open(os.path.join(build_dir, "flasher_args.json")) as f:
        meta = json.load(f)

    chip = meta.get("extra_esptool_args", {}).get("chip")
    if chip not in CHIP_FAMILIES:
        sys.exit(f"지원하지 않는 칩입니다: {chip}")
    for section in REQUIRED_SECTIONS:
        if meta.get(section, {}).get("offset") not in meta["flash_files"]:
            sys.exit(f"flasher_args.json 에 {section} 항목이 없습니다. 빌드 설정이나 source/CMakeLists.txt 의 플래시 이미지 등록을 확인하세요.")

    os.makedirs(out_dir, exist_ok=True)
    parts = []
    for offset, path in sorted(meta["flash_files"].items(), key=lambda item: int(item[0], 16)):
        source = resolve(build_dir, path)
        name = os.path.basename(source)
        # 사이트에서는 파일 이름만 남기므로 이름이 겹치면 한쪽이 덮인다.
        if any(part["path"] == name for part in parts):
            sys.exit(f"설치 파일 이름이 겹칩니다: {name}")
        shutil.copyfile(source, os.path.join(out_dir, name))
        parts.append({"path": name, "offset": int(offset, 16)})

    app_name = os.path.basename(meta["app"]["file"])
    # index.html 의 Wi-Fi 업데이트 다운로드 링크가 이 이름을 가리킨다.
    if app_name != WIFI_IMAGE:
        sys.exit(f"앱 이미지 이름이 {WIFI_IMAGE} 가 아닙니다: {app_name}")
    version, project = read_app_desc(os.path.join(out_dir, app_name))
    if project != "TonexController":
        sys.exit(f"TonexController 이미지가 아닙니다: {project}")

    manifest = {
        # 설치 대화상자 버튼이 "Install {name}"이 된다. index.html·문서가 이 문구를 인용한다.
        "name": "Tonex Controller fork",
        "version": version,
        # 없으면 Improv 가 없는 보드는 묻지 않고 플래시 전체를 지운다(설정 삭제).
        "new_install_prompt_erase": True,
        # 포크 펌웨어는 Improv Serial 이 없다. 기다리지 않고 바로 끝낸다.
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": CHIP_FAMILIES[chip], "parts": parts}],
    }
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    shutil.copyfile(os.path.join(os.path.dirname(os.path.abspath(__file__)), "index.html"),
                    os.path.join(out_dir, "index.html"))

    for part in parts:
        print(f"{part['offset']:#09x}  {part['path']}")
    print(f"{out_dir}: {manifest['name']} {version}")


if __name__ == "__main__":
    main()

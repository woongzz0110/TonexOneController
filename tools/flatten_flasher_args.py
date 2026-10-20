#!/usr/bin/env python3
"""flasher_args.json 의 파일 경로를 파일 이름만 남겨 한 폴더짜리 설치 묶음용으로 쓴다.

사용: python3 tools/flatten_flasher_args.py SRC_JSON DEST_JSON
"""
import json
import os
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    with open(sys.argv[1]) as source:
        meta = json.load(source)

    meta["flash_files"] = {offset: os.path.basename(path) for offset, path in meta["flash_files"].items()}

    for section in ("bootloader", "partition-table", "otadata", "app"):
        if isinstance(meta.get(section), dict) and "file" in meta[section]:
            meta[section]["file"] = os.path.basename(meta[section]["file"])

    with open(sys.argv[2], "w") as dest:
        json.dump(meta, dest, indent=2)


if __name__ == "__main__":
    main()

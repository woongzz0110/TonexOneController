// 호스트 미리보기 시나리오가 바꾸는 보드 상태
#pragma once
#include <stdint.h>

extern uint8_t PreviewSupported;      // TONEX 계열이 연결된 것처럼
extern uint8_t PreviewVolumeKnown;    // 페달이 볼륨을 알려준 것처럼
extern float PreviewVolume;           // 그때의 글로벌 볼륨(dB)

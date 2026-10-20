// 호스트 미리보기 시나리오가 바꾸는 보드 상태
#pragma once
#include <stdint.h>

extern uint8_t PreviewSupported;      // TONEX 계열이 연결된 것처럼
extern uint8_t PreviewVolumeKnown;    // 페달이 볼륨을 알려준 것처럼
extern float PreviewVolume;           // 그때의 글로벌 볼륨(dB)
extern uint8_t PreviewChocLink;       // 초콜릿 BLE 연결 상태(ChocLinkStates)
extern const char* PreviewSsid;       // 업스트림 Wi-Fi 설정의 SSID

// 업스트림 컨트롤러 설정(control_get_config_item_int가 돌려줄 값)
void preview_set_config(uint32_t item, uint32_t value);

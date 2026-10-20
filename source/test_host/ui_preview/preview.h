// 호스트 미리보기 시나리오가 바꾸는 보드 상태
#pragma once
#include <stdint.h>

extern uint8_t PreviewSupported;      // TONEX 계열이 연결된 것처럼
extern uint8_t PreviewVolumeKnown;    // 페달이 볼륨을 알려준 것처럼
extern float PreviewVolume;           // 그때의 글로벌 볼륨(dB)
extern uint8_t PreviewChocLink;       // 초콜릿 BLE 연결 상태(ChocLinkStates)
extern const char* PreviewSsid;       // 업스트림 Wi-Fi 설정의 SSID
extern uint8_t PreviewModeller;       // usb_get_connected_modeller_type()
extern uint8_t PreviewTunerSource;    // tuner_ctrl.h TunerSources
extern uint8_t PreviewTunerProbing;   // 페달 후보를 시험 중(몇 번째인지는 PreviewTunerProbeIndex)
extern uint8_t PreviewTunerProbeIndex;
extern uint8_t PreviewTunerAudioState;  // tuner_audio.h TunerAudioStates
extern uint8_t PreviewTunerPedalOn;     // 확인된 후보를 켜고 값을 기다리는 중
extern uint8_t PreviewChocInput;      // choc_exec.h ChocInputModes

// 업스트림 컨트롤러 설정(control_get_config_item_int가 돌려줄 값)
void preview_set_config(uint32_t item, uint32_t value);

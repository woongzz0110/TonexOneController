/*
 Copyright (C) 2026

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#pragma once

#include "lvgl.h"

// 업스트림 튜너 화면을 원조 One에서도 쓰게 하는 화면 쪽: 화면 진입·나가기 감지, 메인 화면 튜너 아이콘,
// 튜너 화면의 출처 표시, 초콜릿으로 여닫기, Setup 화면 Tuner 탭(설정·진단).
// 모두 display 태스크(LVGL 락 보유)에서 불린다.

void display_fork_tuner_init(void);
void display_fork_tuner_toggle(void);
void display_fork_tuner_create_tab(lv_obj_t* tabview);
void display_fork_tuner_refresh_tab(void);

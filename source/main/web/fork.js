// 포크 추가 웹 화면: 상단 메뉴 Boost, Config > Chocolate, Config > Firmware.
// 업스트림 script.js를 고치지 않고 processReturnCmd()를 감싸서 덧붙인다(업스트림 병합 충돌 최소화).
// 상수 값은 펌웨어 choc_map.h / boost.h 와 같아야 한다.

(function () {
    'use strict';

    const CHOC_ACTIONS = [
        '없음',
        '부스트 토글',
        '부스트 켜기',
        '부스트 끄기',
        '탭템포',
        '프리셋 업',
        '프리셋 다운',
        '프리셋 직접 선택',
        '딜레이 On/Off',
        '공간계 On/Off',
        '모듈레이션 On/Off',
        '컴프 On/Off',
        '노이즈게이트 On/Off',
        '튜너 열기/닫기'
    ];
    const CHOC_ACTION_PRESET_SELECT = 7;
    const CHOC_BUTTONS = ['A', 'B', 'C', 'D', 'E', 'F'];
    // tuner_proto.h TunerModes, uac_stream.h UacChannelModes
    const TUNER_MODES = ['자동(페달 메시지 → USB 녹음)', '페달 메시지만', 'USB 녹음만'];
    const TUNER_CHANNELS = ['1+2 평균', '1번', '2번'];
    const TUNER_REFRESH_MSEC = 1000;

    // 업스트림 Bluetooth 설정 값(control.h BT_MODE_*)
    const BT_MODE_CENTRAL = 1;
    // choc_exec.h ChocLinkStates
    const CHOC_STATE_CONNECTING = 1;
    const CHOC_STATE_CONNECTED = 2;
    // choc_exec.h ChocInputModes
    const CHOC_INPUT_TAP = 1;
    const CHOC_INPUT_PRESS_RELEASE = 2;
    let chocInput = 0;

    const BOOST_MIN_DB = -40;
    const BOOST_MAX_DB = 3;
    const BOOST_STEP_DB = 0.5;

    const BOOST_STATE_TEXT = ['BOOST OFF', 'BOOST ON', 'CUSTOM', 'BOOST --'];
    const BOOST_STATE_CLASS = ['fork-badge-off', 'fork-badge-on', 'fork-badge-custom', 'fork-badge-unknown'];

    // wifi_fork.c ForkConfigResults 순서
    const RESULT_TEXT = [
        '저장했습니다. 바로 적용됩니다.',
        '부스트 값은 -40 ~ +3 dB 범위여야 합니다.',
        '부스트 값은 0.5 dB 단위여야 합니다.',
        '부스트 OFF 값은 ON 값보다 작아야 합니다.',
        '버튼 동작이나 프리셋 번호가 범위를 벗어났습니다.'
    ];

    // ESP-IDF 앱 이미지 배치: esp_image_header_t(24) + esp_image_segment_header_t(8) 뒤에 esp_app_desc_t
    const IMAGE_MAGIC = 0xE9;
    const IMAGE_CHIP_ID_OFFSET = 12;
    const CHIP_ID_ESP32S3 = 9;
    const APP_DESC_OFFSET = 32;
    const APP_DESC_MAGIC = 0xABCD5432;
    const APP_PROJECT_NAME_OFFSET = APP_DESC_OFFSET + 48;
    const APP_PROJECT_NAME = 'TonexController';

    // 두 페이지가 같은 설정 메시지(GETFORKCONFIG)를 받으므로, 손대는 중인 페이지는 그 페이지 값이 실제로 바뀌었을 때만 알린다
    const pages = {
        boost: { dirty: false, loaded: false, note: 'fork_boost_note', keys: /^BOOST_(OFF|ON|BOOT_OFF)$/, snapshot: '' },
        choc: { dirty: false, loaded: false, note: 'fork_choc_note', keys: /^(TRANSLATE|BTN\d_(ACT|PRM)|TUNER_HOLD|HOLD_MS)$/, snapshot: '' }
    };
    // 한 연결 안에서 저장 결과(FORKCONFIGRESULT)는 보낸 순서대로 온다
    const savingPages = [];
    let maxPresets = 20;
    let otaMaxSize = 0;
    let otaPlatform = '';
    let otaAvailable = false;
    let otaPending = false;
    let uploading = false;
    let uploadDone = false;
    let pendingTimer = null;
    let menuAdded = false;
    const highlightTimers = {};

    // ------------------------------------------------------------------ page

    // 상단 메뉴에 Boost가 붙으면 휴대폰 폭(375px)을 넘어 Config가 화면 밖으로 밀리므로, 좁은 화면에서만 줄을 넘긴다
    const STYLE = `
        @media (max-width: 480px) { #nav-items-list { flex-wrap: wrap; } }
        .fork-rows { border: 1px solid #373737; border-radius: 6px; margin-bottom: 12px; }
        .fork-row { display: flex; align-items: center; gap: 8px; padding: 8px; border-bottom: 1px solid #373737; }
        .fork-row:last-child { border-bottom: none; }
        .fork-row.fork-hit { background-color: #513d2b; transition: background-color 0.2s; }
        .fork-row .fork-name { flex: 0 0 32px; font-size: 1.1rem; font-weight: 600; text-align: center; }
        .fork-row .btn { white-space: nowrap; }
        .fork-row .fork-action { flex: 1 1 auto; min-width: 0; }
        .fork-row .fork-preset { flex: 0 0 72px; width: 72px; padding-left: 6px; padding-right: 6px; }
        .fork-badge { display: inline-block; min-width: 130px; padding: 6px 14px; border-radius: 8px;
                      border: 2px solid #fb9230; font-weight: 600; text-align: center; }
        .fork-badge-on { background: #fb9230; color: #000; }
        .fork-badge-off { background: #5a5a5a; color: #fff; border-color: #5a5a5a; }
        .fork-badge-custom { background: transparent; color: #fb9230; }
        .fork-badge-unknown { background: transparent; color: #888; border-color: #555; }
        .fork-note { color: #fb9230; min-height: 1.5em; }
        .fork-muted { color: #999; font-size: 0.9rem; }
        .fork-ok { color: #6cc24a; }
        .fork-num { max-width: 110px; display: inline-block; }
        #ForkBoost .form-check-label, #ForkChocolate .form-check-label { margin-left: 8px; }
        .fork-fields { display: flex; flex-wrap: wrap; align-items: center; gap: 8px 24px; }
        .fork-field { display: inline-flex; align-items: center; gap: 6px; white-space: nowrap; margin: 0; }
        #fork_usb_info, #fork_tuner_info { background: #111; color: #ddd; padding: 8px; font-size: 0.8rem; max-height: 420px;
                         overflow: auto; white-space: pre; }
    `;

    function buildBoostSection() {
        return `
        <div id="ForkBoost" class="content-section">
            <div class="col py-3 text-white">
                <h5 class="selected_text">Boost</h5>
                <div class="container">
                    <p class="fork-muted">TONEX 글로벌 볼륨을 두 값(OFF / ON) 사이에서 바꿔 솔로 부스트로 씁니다.
                       초콜릿 버튼이나 아래 버튼으로 켜고 끕니다.</p>
                    <p>
                        <span id="fork_badge" class="fork-badge fork-badge-unknown">BOOST --</span>
                        &nbsp;<span id="fork_volume" class="fork-muted"></span>
                        &nbsp;<button type="button" class="btn btn-sm btn-warning" id="fork_boost_toggle">부스트 토글</button>
                    </p>

                    <div class="fork-fields">
                        <label class="fork-field" for="fork_boost_off">OFF 값
                            <input type="number" class="form-control text_entry_style fork-num" id="fork_boost_off"
                                   min="${BOOST_MIN_DB}" max="${BOOST_MAX_DB}" step="${BOOST_STEP_DB}"> dB</label>
                        <label class="fork-field" for="fork_boost_on">ON 값
                            <input type="number" class="form-control text_entry_style fork-num" id="fork_boost_on"
                                   min="${BOOST_MIN_DB}" max="${BOOST_MAX_DB}" step="${BOOST_STEP_DB}"> dB</label>
                        <span class="fork-field fork-muted">부스트 양 <span id="fork_boost_amount">-</span></span>
                    </div>
                    <div class="form-check form-switch mt-3">
                        <input class="form-check-input" type="checkbox" role="switch" id="fork_boot_off">
                        <label class="form-check-label" for="fork_boot_off">OFF로 시작</label>
                    </div>
                    <p class="fork-muted">켜 두면 TONEX가 연결될 때마다 볼륨을 OFF 값으로 맞춥니다.</p>
                    <p class="fork-muted">범위 -40 ~ +3 dB, 0.5 dB 단위, OFF &lt; ON. 화면의 dB는 TONEX One 0~10 볼륨을 -40~+3으로 환산한 값입니다.</p>
                    <p class="fork-note" id="fork_boost_note"></p>

                    <button type="button" class="btn btn-success" id="fork_boost_save">저장 (즉시 적용)</button>
                    <button type="button" class="btn btn-secondary" id="fork_boost_reload">다시 불러오기</button>
                </div>
            </div>
        </div>`;
    }

    function buildChocolateSection() {
        const rows = CHOC_BUTTONS.map((name, index) => `
            <div class="fork-row" id="fork_row_${index}">
                <div class="fork-name">${name}</div>
                <select class="form-select select_style fork-action" id="fork_act_${index}">
                    ${CHOC_ACTIONS.map((label, value) => `<option value="${value}">${label}</option>`).join('')}
                </select>
                <input type="number" class="form-control text_entry_style fork-preset" id="fork_prm_${index}"
                       min="1" max="20" step="1" value="1" title="프리셋 번호" placeholder="번호">
                <button type="button" class="btn btn-sm btn-outline-warning" id="fork_test_${index}" title="이 버튼을 누른 것처럼 실행">실행</button>
            </div>`).join('');

        return `
        <div id="ForkChocolate" class="content-section">
            <div class="col py-3 text-white">
                <h5 class="selected_text">M-Vave Chocolate</h5>
                <div class="container">
                    <p id="fork_choc_status" class="fork-muted">연결 상태 확인 중…</p>

                    <div class="form-check form-switch">
                        <input class="form-check-input" type="checkbox" role="switch" id="fork_translate">
                        <label class="form-check-label" for="fork_translate">버튼 매핑 사용</label>
                    </div>
                    <p class="fork-muted">켜면 초콜릿 버튼이 아래 동작을 실행합니다. 끄면 초콜릿 MIDI를 다른 MIDI 장치와 같이
                       기존 방식(PC = 프리셋 선택, CC = Midi 설정)으로 처리합니다.
                       켜 두면 초콜릿이 보내는 CC 20~25는 버튼 신호로만 쓰고 TONEX 설정 변경으로 넘기지 않습니다.</p>

                    <div class="fork-rows">${rows}</div>
                    <p class="fork-muted">초콜릿은 CubeSuite에서 Program change A 모드나 Manufacturer Control 모드로 둡니다. E는 A+B, F는 C+D를 함께 누른 것입니다.
                       '프리셋 직접 선택'을 고르면 그 줄에 프리셋 번호 칸이 나타나고, '실행'은 그 버튼을 짧게 누른 것과 같습니다.</p>

                    <div class="fork-fields">
                        <div class="fork-field">길게 눌러 튜너 열기/닫기
                            <select class="form-select" id="fork_hold_button">
                                <option value="-1">사용 안 함</option>
                                ${CHOC_BUTTONS.map((name, index) => `<option value="${index}">${name}</option>`).join('')}
                            </select>
                        </div>
                        <div class="fork-field">누르는 시간
                            <input type="number" class="form-control text_entry_style" id="fork_hold_time" min="0.3" max="2.0" step="0.1" value="0.8" style="width: 6em;"> 초
                        </div>
                    </div>
                    <p class="fork-muted">길게 누름은 뗄 때도 신호를 보내는 Manufacturer Control 모드에서만 됩니다(Program change 모드는 누를 때만 보냅니다).
                       길게 누름으로 고른 버튼의 원래 동작은 짧게 눌렀다 뗄 때 실행됩니다(탭 템포 버튼이면 그만큼 늦어집니다).
                       E·F를 길게 누름 버튼으로 쓰는 것은 실물로 확인하지 않았으니 A~D 가운데서 고르기를 권합니다.</p>
                    <p class="fork-note" id="fork_hold_hint"></p>
                    <p class="fork-note" id="fork_choc_note"></p>

                    <button type="button" class="btn btn-success" id="fork_choc_save">저장 (즉시 적용)</button>
                    <button type="button" class="btn btn-secondary" id="fork_choc_reload">다시 불러오기</button>
                </div>
            </div>
        </div>`;
    }

    function buildFirmwareSection() {
        return `
        <div id="ForkFirmware" class="content-section">
            <div class="col py-3 text-white">
                <h5 class="selected_text">Firmware</h5>
                <div class="container">
                    <p id="fork_fw_info" class="fork-muted">펌웨어 정보 확인 중…</p>

                    <h6 class="selected_text">Wi-Fi 업데이트</h6>
                    <p class="fork-muted">빌드 결과의 <b>TonexController.bin</b> 하나만 올립니다(부트로더·스킨은 그대로).
                       업로드가 끝나면 보드가 재부팅됩니다. 새 펌웨어가 켜진 뒤 30초 안에 다시 재부팅되면(오류 리셋, 전원 껐다 켜기)
                       이전 펌웨어로 되돌아가고, 30초가 지나면 새 펌웨어로 확정됩니다.</p>
                    <input type="file" class="form-control" id="fork_fw_file" accept=".bin">
                    <div class="progress mt-2" style="height: 22px;">
                        <div class="progress-bar bg-warning" id="fork_fw_progress" role="progressbar" style="width: 0%">0%</div>
                    </div>
                    <p class="fork-note" id="fork_fw_note"></p>
                    <button type="button" class="btn btn-success" id="fork_fw_upload">업로드</button>

                    <br><br>
                    <h6 class="selected_text">USB 정보 (튜너 가능성 확인)</h6>
                    <p class="fork-muted">TONEX One이 보드에 알려준 USB 구성입니다. 보드 Setup 화면 Info 탭과 같은 내용입니다.</p>
                    <button type="button" class="btn btn-secondary" id="fork_usb_refresh">새로고침</button>
                    <button type="button" class="btn btn-secondary" id="fork_usb_copy">복사</button>
                    <pre id="fork_usb_info" class="mt-2">-</pre>

                    <br>
                    <h6 class="selected_text">튜너 (원조 TONEX One)</h6>
                    <p class="fork-muted">튜너 화면을 처음 열면 페달에 튜너 요청 번호 후보 4개를 1초씩 차례로 보내 보고(그동안 기타 줄을 튕기세요), 튜너 값이 오지 않으면 USB 녹음으로 음을 잽니다.
                       실물로 확인할 때 아래 보고서를 복사해 보내 주세요. 페달 본체 튜너(풋스위치 길게)를 켰다 끄면 상태 변화도 기록됩니다.</p>
                    <div class="fork-field">신호 출처
                        <select class="form-select" id="fork_tuner_mode">
                            ${TUNER_MODES.map((label, value) => `<option value="${value}">${label}</option>`).join('')}
                        </select>
                    </div>
                    <div class="fork-field">USB 녹음 채널
                        <select class="form-select" id="fork_tuner_channel">
                            ${TUNER_CHANNELS.map((label, value) => `<option value="${value}">${label}</option>`).join('')}
                        </select>
                    </div>
                    <button type="button" class="btn btn-warning" id="fork_tuner_probe">페달 다시 시험</button>
                    <button type="button" class="btn btn-outline-warning" id="fork_tuner_probe_all" title="프리셋·슬롯이 바뀔 수 있는 번호까지 6개 모두">전체 후보 시험</button>
                    <button type="button" class="btn btn-secondary" id="fork_tuner_refresh">새로고침</button>
                    <button type="button" class="btn btn-secondary" id="fork_tuner_copy">복사</button>
                    <label class="fork-muted ms-2"><input type="checkbox" id="fork_tuner_auto" checked> 1초마다 새로고침</label>
                    <pre id="fork_tuner_info" class="mt-2">-</pre>
                </div>
            </div>
        </div>`;
    }

    function reloadPage(page) {
        pages[page].dirty = false;
        document.getElementById(pages[page].note).textContent = '';
        sendWS({ CMD: 'GETFORKCONFIG' });
    }

    function injectPage() {
        const style = document.createElement('style');
        style.textContent = STYLE;
        document.head.appendChild(style);

        const wrapper = document.querySelector('.content-wrapper');
        wrapper.insertAdjacentHTML('beforeend', buildBoostSection() + buildChocolateSection() + buildFirmwareSection());

        CHOC_BUTTONS.forEach((name, index) => {
            document.getElementById(`fork_act_${index}`).addEventListener('change', () => { pages.choc.dirty = true; updatePresetFields(); });
            document.getElementById(`fork_prm_${index}`).addEventListener('input', () => { pages.choc.dirty = true; });
            document.getElementById(`fork_test_${index}`).addEventListener('click', () => sendWS({ CMD: 'CHOCTEST', BUTTON: index }));
        });
        document.getElementById('fork_translate').addEventListener('change', () => { pages.choc.dirty = true; });
        document.getElementById('fork_hold_button').addEventListener('change', () => { pages.choc.dirty = true; updateHoldHint({}); });
        document.getElementById('fork_hold_time').addEventListener('input', () => { pages.choc.dirty = true; });
        document.getElementById('fork_choc_save').addEventListener('click', saveChocolate);
        document.getElementById('fork_choc_reload').addEventListener('click', () => reloadPage('choc'));

        document.getElementById('fork_boot_off').addEventListener('change', () => { pages.boost.dirty = true; });
        ['fork_boost_off', 'fork_boost_on'].forEach(id =>
            document.getElementById(id).addEventListener('input', () => { pages.boost.dirty = true; updateBoostAmount(); validateBoost(); }));
        document.getElementById('fork_boost_save').addEventListener('click', saveBoost);
        document.getElementById('fork_boost_reload').addEventListener('click', () => reloadPage('boost'));
        document.getElementById('fork_boost_toggle').addEventListener('click', () => sendWS({ CMD: 'SETBOOST', ON: 2 }));

        document.getElementById('fork_fw_upload').addEventListener('click', uploadFirmware);
        document.getElementById('fork_usb_refresh').addEventListener('click', () => sendWS({ CMD: 'GETUSBINFO' }));
        document.getElementById('fork_usb_copy').addEventListener('click', () => copyText('fork_usb_info', 'USB 정보'));

        document.getElementById('fork_tuner_mode').addEventListener('change', event =>
            sendWS({ CMD: 'SETTUNER', MODE: Number(event.target.value) }));
        document.getElementById('fork_tuner_channel').addEventListener('change', event =>
            sendWS({ CMD: 'SETTUNER', CHANNEL: Number(event.target.value) }));
        document.getElementById('fork_tuner_probe').addEventListener('click', () => sendWS({ CMD: 'SETTUNER', REPROBE: 1 }));
        document.getElementById('fork_tuner_probe_all').addEventListener('click', () => {
            if (window.confirm('뒤의 두 후보는 TONEX One의 슬롯·프리셋 선택 명령일 수 있어 시험 중 소리가 바뀔 수 있습니다. 6개 모두 시험할까요?')) {
                sendWS({ CMD: 'SETTUNER', REPROBE: 2 });
            }
        });
        document.getElementById('fork_tuner_refresh').addEventListener('click', () => sendWS({ CMD: 'GETTUNER' }));
        document.getElementById('fork_tuner_copy').addEventListener('click', () => copyText('fork_tuner_info', '튜너 보고서'));

        // 보고 있을 때만 묻는다(다른 화면에서는 웹소켓을 쓰지 않는다)
        setInterval(() => {
            if (document.getElementById('ForkFirmware').classList.contains('active') &&
                document.getElementById('fork_tuner_auto').checked && !uploading) {
                sendWS({ CMD: 'GETTUNER' });
            }
        }, TUNER_REFRESH_MSEC);
    }

    function applyTuner(data) {
        const info = document.getElementById('fork_tuner_info');

        if (!data['FEATURE']) {
            info.textContent = '이 펌웨어 빌드에는 포크 튜너가 없습니다.';
            return;
        }

        // 고르는 중인 목록은 덮지 않는다
        ['fork_tuner_mode', 'fork_tuner_channel'].forEach((id, index) => {
            const select = document.getElementById(id);
            if (document.activeElement !== select) {
                select.value = String(data[index === 0 ? 'MODE' : 'CHANNEL']);
            }
        });
        info.textContent = (data['LINES'] || []).join('\n') || '-';

        if ('REPROBE' in data) {
            setToast(data['REPROBE'] ? '페달 시험을 시작했습니다. 바로 줄을 계속 튕기세요.'
                                     : '시험하지 않았습니다: 원조 TONEX One이 아직 준비되지 않았거나 신호 출처가 USB 녹음만입니다.');
        }
    }

    function showSection(target) {
        document.querySelectorAll('.content-section').forEach(section => section.classList.remove('active'));
        const section = document.getElementById(target);
        if (section) {
            section.classList.add('active');
        }

        // 휴대폰 폭에서 펼친 메뉴를 닫는다(업스트림 메뉴와 같은 동작)
        const navbar = document.querySelector('.navbar-collapse');
        if (navbar && navbar.classList.contains('show') && typeof bootstrap !== 'undefined') {
            bootstrap.Collapse.getInstance(navbar)?.hide();
        }

        if ((target === 'ForkBoost') || (target === 'ForkChocolate')) {
            sendWS({ CMD: 'GETFORKCONFIG' });
        } else if (target === 'ForkFirmware') {
            sendWS({ CMD: 'GETFWINFO' });
            sendWS({ CMD: 'GETUSBINFO' });
            sendWS({ CMD: 'GETTUNER' });
        }
    }

    function menuLink(target, label, className) {
        const li = document.createElement('li');
        li.className = (className === 'nav-link') ? 'nav-item' : '';
        li.innerHTML = `<a class="${className} menu-link" data-target="${target}" href="#">${label}</a>`;
        li.firstChild.onclick = e => { e.preventDefault(); showSection(target); };
        return li;
    }

    // 업스트림이 GETMODELLERDATA마다 메뉴를 다시 만들므로 매번 덧붙인다
    function addMenuItems() {
        const toggles = document.querySelectorAll('#nav-items-list .dropdown-toggle');
        const globals = document.querySelector('#nav-items-list [data-target="Global"]');
        let configMenu = null;

        toggles.forEach(toggle => {
            if (toggle.textContent.trim() === 'Config') {
                configMenu = toggle.parentElement.querySelector('.dropdown-menu');
            }
        });

        if (!configMenu || configMenu.querySelector('[data-target="ForkFirmware"]')) {
            return;
        }

        // 부스트·버튼 매핑은 TONEX 계열에서만 동작한다(Valeton 메뉴에는 Global 항목이 없다)
        if (modellerIsTonex()) {
            if (globals) {
                globals.parentElement.after(menuLink('ForkBoost', 'Boost', 'nav-link'));
            }
            configMenu.appendChild(menuLink('ForkChocolate', 'Chocolate', 'dropdown-item'));
        }
        configMenu.appendChild(menuLink('ForkFirmware', 'Firmware', 'dropdown-item'));
        menuAdded = true;
    }

    function modellerIsTonex() {
        return (typeof modellerType === 'undefined') || (typeof AMP_MODELLER_VALETON_GP5 === 'undefined') ||
               (modellerType !== AMP_MODELLER_VALETON_GP5);
    }

    // ------------------------------------------------------------------ boost

    function readBoostInputs() {
        return {
            off: parseFloat(document.getElementById('fork_boost_off').value),
            on: parseFloat(document.getElementById('fork_boost_on').value)
        };
    }

    function formatDb(value) {
        return (value > 0 ? '+' : '') + value.toFixed(1) + ' dB';
    }

    function updateBoostAmount() {
        const boost = readBoostInputs();
        const amount = document.getElementById('fork_boost_amount');
        amount.textContent = (isNaN(boost.off) || isNaN(boost.on)) ? '-' : formatDb(boost.on - boost.off);
    }

    function isStep(value) {
        return Math.abs(value * 2 - Math.round(value * 2)) < 1e-6;
    }

    // 문제가 없으면 null, 있으면 보여줄 문구
    function validateBoost() {
        const boost = readBoostInputs();
        const note = document.getElementById('fork_boost_note');
        let message = null;

        if (isNaN(boost.off) || isNaN(boost.on)) {
            message = '부스트 OFF/ON 값을 입력하세요.';
        } else if (boost.on > BOOST_MAX_DB) {
            // ON 상한이 TONEX 글로벌 볼륨 최대값이라, 같은 부스트 양은 OFF를 낮춰서 낸다
            const amount = boost.on - boost.off;
            const altOff = BOOST_MAX_DB - amount;
            message = `ON 값은 +3 dB를 넘을 수 없습니다(TONEX 글로벌 볼륨 최대값). ` +
                      (altOff >= BOOST_MIN_DB
                          ? `같은 부스트 양(${formatDb(amount)})은 OFF ${formatDb(altOff)} / ON +3.0 dB로 설정하고, 평소 음량은 앰프나 믹서에서 맞추세요.`
                          : '부스트 양을 줄이세요.');
        } else if ((boost.off < BOOST_MIN_DB) || (boost.on < BOOST_MIN_DB) || (boost.off > BOOST_MAX_DB)) {
            message = RESULT_TEXT[1];
        } else if (!isStep(boost.off) || !isStep(boost.on)) {
            message = RESULT_TEXT[2];
        } else if (boost.off >= boost.on) {
            message = RESULT_TEXT[3];
        }

        note.textContent = message || '';
        return message;
    }

    function saveBoost() {
        if (validateBoost() !== null) {
            return;
        }

        const boost = readBoostInputs();
        savingPages.push('boost');
        sendWS({
            CMD: 'SETFORKCONFIG',
            BOOST_OFF: boost.off,
            BOOST_ON: boost.on,
            BOOST_BOOT_OFF: document.getElementById('fork_boot_off').checked ? 1 : 0
        });
    }

    function applyBoostConfig(data) {
        document.getElementById('fork_boost_off').value = Number(data['BOOST_OFF']).toFixed(1);
        document.getElementById('fork_boost_on').value = Number(data['BOOST_ON']).toFixed(1);
        document.getElementById('fork_boot_off').checked = data['BOOST_BOOT_OFF'] === 1;
        updateBoostAmount();
    }

    function updateBoostState(data) {
        if (!('BOOST_STATE' in data)) {
            return;
        }

        const state = (data['BOOST_STATE'] >= 0 && data['BOOST_STATE'] <= 3) ? data['BOOST_STATE'] : 3;
        const badge = document.getElementById('fork_badge');
        badge.textContent = BOOST_STATE_TEXT[state];
        badge.className = 'fork-badge ' + BOOST_STATE_CLASS[state];
        document.getElementById('fork_volume').textContent =
            ('VOLUME' in data) ? '현재 볼륨 ' + formatDb(Number(data['VOLUME'])) : (data['BOOST_SUPPORTED'] ? '볼륨 읽는 중…' : 'TONEX 연결 안 됨');
        document.getElementById('fork_boost_toggle').disabled = !data['BOOST_SUPPORTED'];
    }

    // ------------------------------------------------------------------ chocolate

    function updatePresetFields() {
        CHOC_BUTTONS.forEach((name, index) => {
            const action = parseInt(document.getElementById(`fork_act_${index}`).value);
            const field = document.getElementById(`fork_prm_${index}`);
            field.style.display = (action === CHOC_ACTION_PRESET_SELECT) ? '' : 'none';
            field.max = maxPresets;
        });
    }

    function saveChocolate() {
        const message = { CMD: 'SETFORKCONFIG' };

        message['TRANSLATE'] = document.getElementById('fork_translate').checked ? 1 : 0;
        for (let index = 0; index < CHOC_BUTTONS.length; index++) {
            const action = parseInt(document.getElementById(`fork_act_${index}`).value);
            let preset = parseInt(document.getElementById(`fork_prm_${index}`).value);

            if (action === CHOC_ACTION_PRESET_SELECT && (isNaN(preset) || preset < 1 || preset > maxPresets)) {
                document.getElementById('fork_choc_note').textContent = `${CHOC_BUTTONS[index]}: 프리셋 번호는 1 ~ ${maxPresets} 입니다.`;
                return;
            }
            // 숨겨진 칸에 남은 값 때문에 저장 전체가 거절되지 않게 한다(보드는 1 ~ 150만 받는다)
            if (isNaN(preset) || preset < 1 || (action !== CHOC_ACTION_PRESET_SELECT && preset > maxPresets)) {
                preset = 1;
            }

            message[`BTN${index}_ACT`] = action;
            message[`BTN${index}_PRM`] = preset;
        }

        const holdMs = Math.round(parseFloat(document.getElementById('fork_hold_time').value) * 10) * 100;
        if (isNaN(holdMs) || holdMs < 300 || holdMs > 2000) {
            document.getElementById('fork_choc_note').textContent = '누르는 시간은 0.3 ~ 2.0초(0.1초 단위)입니다.';
            return;
        }
        message['TUNER_HOLD'] = parseInt(document.getElementById('fork_hold_button').value);
        message['HOLD_MS'] = holdMs;

        savingPages.push('choc');
        sendWS(message);
    }

    // choc_exec.h ChocInputModes
    function updateHoldHint(data) {
        const hint = document.getElementById('fork_hold_hint');
        const hold = parseInt(document.getElementById('fork_hold_button').value);

        if ('CHOC_INPUT' in data) {
            chocInput = data['CHOC_INPUT'];
        }

        if (hold < 0) {
            hint.textContent = '';
        } else if (chocInput === CHOC_INPUT_PRESS_RELEASE) {
            hint.textContent = '초콜릿이 누름·뗌을 보내고 있어 길게 누름이 동작합니다.';
            hint.className = 'fork-ok';
        } else if (chocInput === CHOC_INPUT_TAP) {
            hint.textContent = '초콜릿이 Program change 모드입니다. 길게 누르려면 Manufacturer Control 모드로 바꾸세요.';
            hint.className = 'fork-note';
        } else {
            hint.textContent = '초콜릿 버튼을 한 번 누르면 길게 누름이 되는 모드인지 여기 표시됩니다.';
            hint.className = 'fork-muted';
        }
    }

    function applyChocolateConfig(data) {
        document.getElementById('fork_translate').checked = data['TRANSLATE'] === 1;
        CHOC_BUTTONS.forEach((name, index) => {
            document.getElementById(`fork_act_${index}`).value = data[`BTN${index}_ACT`];
            document.getElementById(`fork_prm_${index}`).value = data[`BTN${index}_PRM`];
        });
        if ('TUNER_HOLD' in data) {
            document.getElementById('fork_hold_button').value = String(data['TUNER_HOLD']);
            document.getElementById('fork_hold_time').value = (data['HOLD_MS'] / 1000).toFixed(1);
        }
        updatePresetFields();
        updateHoldHint(data);
    }

    function updateChocolateStatus(data) {
        const status = document.getElementById('fork_choc_status');

        if (!('CHOC_STATE' in data)) {
            return;
        }

        if (data['CHOC_STATE'] === CHOC_STATE_CONNECTED) {
            status.textContent = '연결됨';
            status.className = 'fork-ok';
        } else if (data['CHOC_STATE'] === CHOC_STATE_CONNECTING) {
            status.textContent = '연결 중…';
            status.className = 'fork-muted';
        } else if (!data['BT_CHOC_EN'] || data['BT_MODE'] !== BT_MODE_CENTRAL) {
            status.textContent = '연결 안 됨 — Config > Bluetooth에서 Bluetooth Mode를 Central로, Chocolate Series를 켜야 연결합니다.';
            status.className = 'fork-note';
        } else {
            status.textContent = '연결 안 됨 — 초콜릿 전원을 켜면 저절로 연결됩니다.';
            status.className = 'fork-muted';
        }
    }

    // ------------------------------------------------------------------ config message

    function applyForkConfig(data) {
        maxPresets = data['MAX_PRESETS'] || 20;
        updateBoostState(data);
        updateChocolateStatus(data);

        [['boost', applyBoostConfig], ['choc', applyChocolateConfig]].forEach(([page, apply]) => {
            const state = pages[page];
            const snapshot = JSON.stringify(Object.keys(data).filter(key => state.keys.test(key)).sort().map(key => [key, data[key]]));

            // 첫 응답은 항상 채운다(그 전에 손댄 값이 있어도 기본값이 남지 않게)
            if (state.dirty && state.loaded) {
                if (snapshot !== state.snapshot) {
                    document.getElementById(state.note).textContent =
                        '보드에서 설정이 바뀌었습니다. 저장하면 이 화면 값으로 덮어씁니다(다시 불러오기로 보드 값 확인).';
                }
                return;
            }

            apply(data);
            state.snapshot = snapshot;
            state.loaded = true;
            state.dirty = false;
        });
    }

    function applySaveResult(result) {
        const page = savingPages.shift();

        // 이 화면이 보낸 저장이 아니면(다른 연결이 끊긴 뒤 남은 응답 등) 어느 페이지에도 붙이지 않는다
        if (!page) {
            return;
        }

        document.getElementById(pages[page].note).textContent = RESULT_TEXT[result] || ('저장 실패 (' + result + ')');
        if (result === 0) {
            pages[page].dirty = false;
            setToast((page === 'boost') ? 'Boost 설정 저장됨' : 'Chocolate 설정 저장됨');

            // 보드의 설정 푸시가 이 결과보다 먼저 왔으면 손대는 중이라 건너뛰었으므로, 저장된 값을 다시 받아 기준을 맞춘다
            sendWS({ CMD: 'GETFORKCONFIG' });
        }
    }

    function highlightButton(index) {
        const row = document.getElementById(`fork_row_${index}`);
        if (!row) {
            return;
        }

        row.classList.add('fork-hit');
        clearTimeout(highlightTimers[index]);
        highlightTimers[index] = setTimeout(() => row.classList.remove('fork-hit'), 1500);
    }

    // ------------------------------------------------------------------ firmware

    function updateUploadButton() {
        document.getElementById('fork_fw_upload').disabled = uploading || !otaAvailable || otaPending;
    }

    function applyFirmwareInfo(data) {
        otaMaxSize = data['MAX_SIZE'] || 0;
        otaPlatform = data['PLATFORM'] || '';
        otaAvailable = !!data['OTA'];
        otaPending = otaAvailable && !!data['PENDING'];

        let text = `${data['PROJECT']} ${data['VERSION']} (빌드 ${data['BUILD_DATE']} ${data['BUILD_TIME']}, IDF ${data['IDF']}), 실행 중 슬롯 ${data['RUNNING']}`;
        if (otaPending) {
            text += ' — 방금 업데이트한 펌웨어가 확정되기를 기다리는 중입니다(켜진 뒤 30초). 확정되면 업로드할 수 있습니다.';
        } else if (otaAvailable) {
            text += `, Wi-Fi 업데이트 가능 (${data['PLATFORM']}, 최대 ${Math.floor(otaMaxSize / 1024)} KB)`;
        } else {
            text += ' — 이 펌웨어는 Wi-Fi 업데이트를 지원하지 않습니다. USB로 WS43B_FORK 빌드를 먼저 설치하세요.';
        }
        document.getElementById('fork_fw_info').textContent = text;
        updateUploadButton();

        // 확정되면 버튼이 저절로 풀리도록 다시 묻는다
        clearTimeout(pendingTimer);
        if (otaPending) {
            pendingTimer = setTimeout(() => sendWS({ CMD: 'GETFWINFO' }), 5000);
        }
    }

    function setProgress(percent) {
        const bar = document.getElementById('fork_fw_progress');
        bar.style.width = percent + '%';
        bar.textContent = percent + '%';
    }

    function containsAscii(bytes, text) {
        const first = text.charCodeAt(0);
        outer: for (let i = bytes.indexOf(first); i >= 0 && i <= bytes.length - text.length; i = bytes.indexOf(first, i + 1)) {
            for (let j = 1; j < text.length; j++) {
                if (bytes[i + j] !== text.charCodeAt(j)) {
                    continue outer;
                }
            }
            return true;
        }
        return false;
    }

    // 보드도 같은 검사를 하지만, 업로드 도중 거절되면 브라우저는 사유 없이 "연결 끊김"만 보게 되므로 먼저 본다
    async function checkImage(file) {
        const bytes = new Uint8Array(await file.arrayBuffer());
        const view = new DataView(bytes.buffer);

        if (bytes.length < APP_PROJECT_NAME_OFFSET + 32 || bytes[0] !== IMAGE_MAGIC) {
            return 'ESP32 앱 이미지가 아닙니다. 빌드 결과의 TonexController.bin 을 고르세요(bootloader·partition·skins 파일이 아님).';
        }
        if (view.getUint16(IMAGE_CHIP_ID_OFFSET, true) !== CHIP_ID_ESP32S3) {
            return 'ESP32-S3용 이미지가 아닙니다.';
        }
        const name = String.fromCharCode(...bytes.subarray(APP_PROJECT_NAME_OFFSET, APP_PROJECT_NAME_OFFSET + 32)).replace(/\0.*$/, '');
        if (view.getUint32(APP_DESC_OFFSET, true) !== APP_DESC_MAGIC || name !== APP_PROJECT_NAME) {
            return 'Tonex Controller 펌웨어가 아닙니다.';
        }
        if (otaPlatform && !containsAscii(bytes, `TNXCTL-OTA-PLATFORM=${otaPlatform};`)) {
            return `${otaPlatform} 포크 OTA 빌드(WS43B_FORK) 이미지가 아닙니다. 업스트림·다른 보드용 이미지는 Wi-Fi로 올릴 수 없습니다.`;
        }
        return null;
    }

    function finishUpload() {
        uploading = false;
        updateUploadButton();
    }

    // 업스트림은 웹소켓이 열릴 때만 GETMODELLERDATA를 요청한다. 업로드 성공 뒤에 오면 재부팅 뒤 다시 붙은 것이다.
    // 새 펌웨어가 확정되기 전에는 GETFWINFO의 PENDING이 버튼을 계속 막는다.
    function onReconnected() {
        if (!uploadDone) {
            return;
        }

        uploadDone = false;
        finishUpload();
        document.getElementById('fork_fw_note').textContent = '보드가 다시 연결됐습니다.';
        if (document.getElementById('ForkFirmware').classList.contains('active')) {
            sendWS({ CMD: 'GETFWINFO' });
        }
    }

    async function uploadFirmware() {
        const file = document.getElementById('fork_fw_file').files[0];
        const note = document.getElementById('fork_fw_note');

        if (uploading) {
            return;
        }
        if (!file) {
            note.textContent = 'TonexController.bin 파일을 고르세요.';
            return;
        }
        if (otaMaxSize && file.size > otaMaxSize) {
            note.textContent = `파일이 너무 큽니다 (${file.size} > ${otaMaxSize} 바이트). 앱 이미지가 맞는지 확인하세요.`;
            return;
        }

        // 이미지를 읽는 동안 다시 눌러 업로드가 두 번 나가지 않게, 기다리기 전에 막는다
        uploading = true;
        updateUploadButton();
        note.textContent = '이미지 확인 중…';

        let problem;
        try {
            problem = await checkImage(file);
        } catch (e) {
            problem = '파일을 읽지 못했습니다. 파일을 다시 고르세요.';
        }
        if (problem) {
            note.textContent = problem;
            finishUpload();
            return;
        }

        const request = new XMLHttpRequest();
        note.textContent = '업로드 중… 끝날 때까지 이 화면을 닫지 마세요.';
        setProgress(0);

        request.upload.onprogress = e => {
            if (e.lengthComputable) {
                setProgress(Math.round((e.loaded / e.total) * 100));
            }
        };
        request.onload = () => {
            if (request.status === 200) {
                // 보드가 재부팅해 웹소켓이 다시 붙을 때까지 버튼을 막아 둔다(onReconnected)
                uploadDone = true;
                setProgress(100);
                note.textContent = '완료. 보드가 재부팅합니다. 약 20초 뒤 다시 연결됩니다(AP 모드는 Wi-Fi 재연결 필요).';
            } else {
                note.textContent = '실패: ' + request.responseText;
                finishUpload();
            }
        };
        request.onerror = () => {
            note.textContent = '업로드 중 연결이 끊겼습니다(Wi-Fi 끊김이거나 보드가 이미지를 거절함). 보드는 기존 펌웨어로 계속 동작합니다. ' +
                '업데이트 직후(켜진 뒤 30초 이내)였다면 잠시 뒤 다시 올리세요.';
            finishUpload();
        };

        request.open('POST', '/update');
        request.setRequestHeader('Content-Type', 'application/octet-stream');
        request.send(file);
    }

    function copyText(elementId, label) {
        const text = document.getElementById(elementId).textContent;
        const area = document.createElement('textarea');
        area.value = text;
        document.body.appendChild(area);
        area.select();
        try {
            document.execCommand('copy');
            setToast(`${label}를 복사했습니다`);
        } catch (e) {
            setToast('복사 실패: 길게 눌러 직접 선택하세요');
        }
        document.body.removeChild(area);
    }

    // ------------------------------------------------------------------ messages

    function handleForkMessage(data) {
        switch (data['CMD']) {
            case 'GETFORKCONFIG':
                applyForkConfig(data);
                return true;

            case 'BOOSTSTATE':
                updateBoostState(data);
                return true;

            case 'CHOCEVENT':
                highlightButton(data['BUTTON']);
                updateHoldHint(data);
                return true;

            case 'FORKCONFIGRESULT':
                applySaveResult(data['RESULT']);
                return true;

            case 'GETFWINFO':
                applyFirmwareInfo(data);
                return true;

            case 'GETUSBINFO':
                document.getElementById('fork_usb_info').textContent = (data['LINES'] || []).join('\n') || '-';
                return true;

            case 'GETTUNER':
                applyTuner(data);
                return true;
        }

        return false;
    }

    injectPage();

    const upstreamProcessReturnCmd = window.processReturnCmd;
    window.processReturnCmd = function (data) {
        if (handleForkMessage(data)) {
            return;
        }

        upstreamProcessReturnCmd(data);

        if (data['CMD'] === 'GETMODELLERDATA') {
            // 끊긴 연결로 보낸 저장의 결과는 오지 않는다
            savingPages.length = 0;
            addMenuItems();
            onReconnected();
        } else if (data['CMD'] === 'GETCONFIG' && !menuAdded) {
            addMenuItems();
        }
    };
})();

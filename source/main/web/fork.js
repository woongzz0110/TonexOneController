// 포크 추가 웹 화면: Config > Footswitch, Config > Firmware.
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
        '노이즈게이트 On/Off'
    ];
    const CHOC_ACTION_PRESET_SELECT = 7;

    const CHOC_BUTTONS = [
        { name: 'A', desc: '버튼 A' },
        { name: 'B', desc: '버튼 B' },
        { name: 'C', desc: '버튼 C' },
        { name: 'D', desc: '버튼 D' },
        { name: 'E', desc: 'A + B 동시' },
        { name: 'F', desc: 'C + D 동시' }
    ];

    const BOOST_MIN_DB = -40;
    const BOOST_MAX_DB = 3;
    const BOOST_STEP_DB = 0.5;

    const BOOST_STATE_TEXT = ['BOOST OFF', 'BOOST ON', 'CUSTOM', 'BOOST --'];
    const BOOST_STATE_CLASS = ['fork-badge-off', 'fork-badge-on', 'fork-badge-custom', 'fork-badge-unknown'];

    // wifi_fork.c ChocConfigResults 순서
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

    let formDirty = false;
    let configLoaded = false;
    let maxPresets = 20;
    let otaMaxSize = 0;
    let otaPlatform = '';
    let otaAvailable = false;
    let otaPending = false;
    let uploading = false;
    let pendingTimer = null;
    let menuAdded = false;
    const highlightTimers = {};

    // ------------------------------------------------------------------ page

    const STYLE = `
        .fork-rows { border: 1px solid #373737; border-radius: 6px; margin-bottom: 12px; }
        .fork-row { display: flex; align-items: center; gap: 8px; padding: 8px; border-bottom: 1px solid #373737; }
        .fork-row:last-child { border-bottom: none; }
        .fork-row.fork-hit { background-color: #513d2b; transition: background-color 0.2s; }
        .fork-row .fork-name { flex: 0 0 60px; font-size: 0.9rem; }
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
        .fork-num { max-width: 110px; display: inline-block; }
        #fork_usb_info { background: #111; color: #ddd; padding: 8px; font-size: 0.8rem; max-height: 420px;
                         overflow: auto; white-space: pre; }
    `;

    function buildFootswitchSection() {
        const rows = CHOC_BUTTONS.map((button, index) => `
            <div class="fork-row" id="fork_row_${index}">
                <div class="fork-name"><b>${button.name}</b><br><span class="fork-muted">${button.desc}</span></div>
                <select class="form-select select_style fork-action" id="fork_act_${index}">
                    ${CHOC_ACTIONS.map((name, value) => `<option value="${value}">${name}</option>`).join('')}
                </select>
                <input type="number" class="form-control text_entry_style fork-preset" id="fork_prm_${index}"
                       min="1" max="20" step="1" value="1" title="프리셋 번호" placeholder="번호">
                <button type="button" class="btn btn-sm btn-outline-warning" id="fork_test_${index}" title="이 버튼을 누른 것처럼 실행">실행</button>
            </div>`).join('');

        return `
        <div id="ForkFootswitch" class="content-section">
            <div class="col py-3 text-white">
                <h5 class="selected_text">Footswitch (M-Vave Chocolate)</h5>
                <div class="container">
                    <p>
                        <span id="fork_badge" class="fork-badge fork-badge-unknown">BOOST --</span>
                        &nbsp;<span id="fork_volume" class="fork-muted"></span>
                        &nbsp;<button type="button" class="btn btn-sm btn-warning" id="fork_boost_toggle">부스트 토글</button>
                    </p>
                    <p class="fork-muted" id="fork_choc_status">초콜릿 연결 상태 확인 중…</p>

                    <div class="form-check form-switch">
                        <input class="form-check-input" type="checkbox" role="switch" id="fork_translate">
                        <label class="form-check-label" for="fork_translate">번역 사용 (끄면 초콜릿 신호를 원래 MIDI 처리로 보냄)</label>
                    </div>
                    <br>

                    <div class="fork-rows">${rows}</div>
                    <p class="fork-muted">'프리셋 직접 선택'을 고르면 그 줄에 프리셋 번호 칸이 나타납니다. '실행'은 초콜릿 버튼을 누른 것과 같습니다.</p>

                    <h6 class="selected_text">볼륨 부스트</h6>
                    <div class="row g-2 align-items-center">
                        <div class="col-auto"><label for="fork_boost_off">부스트 OFF</label></div>
                        <div class="col-auto"><input type="number" class="form-control text_entry_style fork-num" id="fork_boost_off"
                             min="${BOOST_MIN_DB}" max="${BOOST_MAX_DB}" step="${BOOST_STEP_DB}"></div>
                        <div class="col-auto">dB</div>
                        <div class="col-auto"><label for="fork_boost_on">부스트 ON</label></div>
                        <div class="col-auto"><input type="number" class="form-control text_entry_style fork-num" id="fork_boost_on"
                             min="${BOOST_MIN_DB}" max="${BOOST_MAX_DB}" step="${BOOST_STEP_DB}"></div>
                        <div class="col-auto">dB</div>
                        <div class="col-auto fork-muted">부스트 양 <span id="fork_boost_amount">-</span></div>
                    </div>
                    <div class="form-check form-switch mt-2">
                        <input class="form-check-input" type="checkbox" role="switch" id="fork_boot_off">
                        <label class="form-check-label" for="fork_boot_off">부팅 시 OFF 값으로 시작</label>
                    </div>
                    <p class="fork-muted">범위 -40 ~ +3 dB, 0.5 dB 단위, OFF &lt; ON. 화면의 dB는 TONEX One 0~10 볼륨을 -40~+3으로 환산한 값입니다.</p>
                    <p class="fork-note" id="fork_note"></p>

                    <button type="button" class="btn btn-success" id="fork_save">저장 (즉시 적용)</button>
                    <button type="button" class="btn btn-secondary" id="fork_reload">다시 불러오기</button>
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
                    <p class="fork-muted">TONEX One이 보드에 알려준 USB 구성입니다. 보드 화면 설정의 USB 탭과 같은 내용입니다.</p>
                    <button type="button" class="btn btn-secondary" id="fork_usb_refresh">새로고침</button>
                    <button type="button" class="btn btn-secondary" id="fork_usb_copy">복사</button>
                    <pre id="fork_usb_info" class="mt-2">-</pre>
                </div>
            </div>
        </div>`;
    }

    function injectPage() {
        const style = document.createElement('style');
        style.textContent = STYLE;
        document.head.appendChild(style);

        const wrapper = document.querySelector('.content-wrapper');
        wrapper.insertAdjacentHTML('beforeend', buildFootswitchSection() + buildFirmwareSection());

        CHOC_BUTTONS.forEach((button, index) => {
            document.getElementById(`fork_act_${index}`).addEventListener('change', () => { formDirty = true; updatePresetFields(); });
            document.getElementById(`fork_prm_${index}`).addEventListener('input', () => { formDirty = true; });
            document.getElementById(`fork_test_${index}`).addEventListener('click', () => sendWS({ CMD: 'CHOCTEST', BUTTON: index }));
        });

        ['fork_translate', 'fork_boot_off'].forEach(id =>
            document.getElementById(id).addEventListener('change', () => { formDirty = true; }));
        ['fork_boost_off', 'fork_boost_on'].forEach(id =>
            document.getElementById(id).addEventListener('input', () => { formDirty = true; updateBoostAmount(); validateBoost(); }));

        document.getElementById('fork_save').addEventListener('click', saveChocConfig);
        document.getElementById('fork_reload').addEventListener('click', () => {
            formDirty = false;
            document.getElementById('fork_note').textContent = '';
            sendWS({ CMD: 'GETCHOCCONFIG' });
        });
        document.getElementById('fork_boost_toggle').addEventListener('click', () => sendWS({ CMD: 'SETBOOST', ON: 2 }));

        document.getElementById('fork_fw_upload').addEventListener('click', uploadFirmware);
        document.getElementById('fork_usb_refresh').addEventListener('click', () => sendWS({ CMD: 'GETUSBINFO' }));
        document.getElementById('fork_usb_copy').addEventListener('click', copyUsbInfo);
    }

    function showSection(target) {
        document.querySelectorAll('.content-section').forEach(section => section.classList.remove('active'));
        const section = document.getElementById(target);
        if (section) {
            section.classList.add('active');
        }

        if (target === 'ForkFootswitch') {
            sendWS({ CMD: 'GETCHOCCONFIG' });
        } else if (target === 'ForkFirmware') {
            sendWS({ CMD: 'GETFWINFO' });
            sendWS({ CMD: 'GETUSBINFO' });
        }
    }

    // 업스트림이 GETMODELLERDATA마다 메뉴를 다시 만들므로 매번 덧붙인다
    function addMenuItems() {
        const toggles = document.querySelectorAll('#nav-items-list .dropdown-toggle');
        let configMenu = null;

        toggles.forEach(toggle => {
            if (toggle.textContent.trim() === 'Config') {
                configMenu = toggle.parentElement.querySelector('.dropdown-menu');
            }
        });

        if (!configMenu || configMenu.querySelector('[data-target="ForkFirmware"]')) {
            return;
        }

        const items = (modellerIsTonex() ? [['ForkFootswitch', 'Footswitch']] : []).concat([['ForkFirmware', 'Firmware']]);
        items.forEach(([target, label]) => {
            const li = document.createElement('li');
            li.innerHTML = `<a class="dropdown-item menu-link" data-target="${target}" href="#">${label}</a>`;
            li.firstChild.onclick = e => { e.preventDefault(); showSection(target); };
            configMenu.appendChild(li);
        });
        menuAdded = true;
    }

    function modellerIsTonex() {
        return (typeof modellerType === 'undefined') || (typeof AMP_MODELLER_VALETON_GP5 === 'undefined') ||
               (modellerType !== AMP_MODELLER_VALETON_GP5);
    }

    // ------------------------------------------------------------------ footswitch

    function updatePresetFields() {
        CHOC_BUTTONS.forEach((button, index) => {
            const action = parseInt(document.getElementById(`fork_act_${index}`).value);
            const field = document.getElementById(`fork_prm_${index}`);
            field.style.display = (action === CHOC_ACTION_PRESET_SELECT) ? '' : 'none';
            field.max = maxPresets;
        });
    }

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

    // returns null if OK, else the message to show
    function validateBoost() {
        const boost = readBoostInputs();
        const note = document.getElementById('fork_note');
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

    function applyChocConfig(data) {
        maxPresets = data['MAX_PRESETS'] || 20;
        updateStatus(data);

        // 첫 응답은 항상 채운다(그 전에 손댄 값이 있어도 기본값이 남지 않게)
        if (formDirty && configLoaded) {
            document.getElementById('fork_note').textContent = '보드에서 설정이 바뀌었습니다. 저장하면 이 화면 값으로 덮어씁니다(다시 불러오기로 보드 값 확인).';
            return;
        }

        document.getElementById('fork_translate').checked = data['TRANSLATE'] === 1;
        CHOC_BUTTONS.forEach((button, index) => {
            document.getElementById(`fork_act_${index}`).value = data[`BTN${index}_ACT`];
            document.getElementById(`fork_prm_${index}`).value = data[`BTN${index}_PRM`];
        });
        document.getElementById('fork_boost_off').value = Number(data['BOOST_OFF']).toFixed(1);
        document.getElementById('fork_boost_on').value = Number(data['BOOST_ON']).toFixed(1);
        document.getElementById('fork_boot_off').checked = data['BOOST_BOOT_OFF'] === 1;

        updatePresetFields();
        updateBoostAmount();
        configLoaded = true;
        formDirty = false;
    }

    function updateStatus(data) {
        if ('CHOC_CONNECTED' in data) {
            document.getElementById('fork_choc_status').textContent =
                data['CHOC_CONNECTED'] ? '초콜릿 연결됨' : '초콜릿 연결 안 됨 (보드 BT 모드 Central, Chocolate 켜짐인지 확인)';
        }

        if ('BOOST_STATE' in data) {
            const state = (data['BOOST_STATE'] >= 0 && data['BOOST_STATE'] <= 3) ? data['BOOST_STATE'] : 3;
            const badge = document.getElementById('fork_badge');
            badge.textContent = BOOST_STATE_TEXT[state];
            badge.className = 'fork-badge ' + BOOST_STATE_CLASS[state];
            document.getElementById('fork_volume').textContent =
                ('VOLUME' in data) ? '현재 볼륨 ' + formatDb(Number(data['VOLUME'])) : (data['BOOST_SUPPORTED'] ? '볼륨 읽는 중…' : 'TONEX 연결 안 됨');
        }
    }

    function saveChocConfig() {
        if (validateBoost() !== null) {
            return;
        }

        const message = { CMD: 'SETCHOCCONFIG' };
        const boost = readBoostInputs();

        message['TRANSLATE'] = document.getElementById('fork_translate').checked ? 1 : 0;
        for (let index = 0; index < CHOC_BUTTONS.length; index++) {
            const action = parseInt(document.getElementById(`fork_act_${index}`).value);
            let preset = parseInt(document.getElementById(`fork_prm_${index}`).value);

            if (action === CHOC_ACTION_PRESET_SELECT && (isNaN(preset) || preset < 1 || preset > maxPresets)) {
                document.getElementById('fork_note').textContent = `${CHOC_BUTTONS[index].name}: 프리셋 번호는 1 ~ ${maxPresets} 입니다.`;
                return;
            }
            // 숨겨진 칸에 남은 값 때문에 저장 전체가 거절되지 않게 한다(보드는 1 ~ 150만 받는다)
            if (isNaN(preset) || preset < 1 || (action !== CHOC_ACTION_PRESET_SELECT && preset > maxPresets)) {
                preset = 1;
            }

            message[`BTN${index}_ACT`] = action;
            message[`BTN${index}_PRM`] = preset;
        }
        message['BOOST_OFF'] = boost.off;
        message['BOOST_ON'] = boost.on;
        message['BOOST_BOOT_OFF'] = document.getElementById('fork_boot_off').checked ? 1 : 0;

        sendWS(message);
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

    function copyUsbInfo() {
        const text = document.getElementById('fork_usb_info').textContent;
        const area = document.createElement('textarea');
        area.value = text;
        document.body.appendChild(area);
        area.select();
        try {
            document.execCommand('copy');
            setToast('USB 정보를 복사했습니다');
        } catch (e) {
            setToast('복사 실패: 길게 눌러 직접 선택하세요');
        }
        document.body.removeChild(area);
    }

    // ------------------------------------------------------------------ messages

    function handleForkMessage(data) {
        switch (data['CMD']) {
            case 'GETCHOCCONFIG':
                applyChocConfig(data);
                return true;

            case 'BOOSTSTATE':
                updateStatus(data);
                return true;

            case 'CHOCEVENT':
                highlightButton(data['BUTTON']);
                return true;

            case 'CHOCCONFIGRESULT': {
                const result = data['RESULT'];
                document.getElementById('fork_note').textContent = RESULT_TEXT[result] || ('저장 실패 (' + result + ')');
                if (result === 0) {
                    // 새 설정은 보드가 모든 웹 화면에 다시 보내준다
                    formDirty = false;
                    setToast('Footswitch 설정 저장됨');
                }
                return true;
            }

            case 'GETFWINFO':
                applyFirmwareInfo(data);
                return true;

            case 'GETUSBINFO':
                document.getElementById('fork_usb_info').textContent = (data['LINES'] || []).join('\n') || '-';
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
            addMenuItems();
        } else if (data['CMD'] === 'GETCONFIG' && !menuAdded) {
            addMenuItems();
        }
    };
})();

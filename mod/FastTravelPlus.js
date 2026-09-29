// Fast Travel Plus: adds "Fast Travel" to the map screen's action bar and a hotkey (` by default, set on the
// MODS page). Both open the game's own fast travel menu through fasttravelplus.dll. During a mission it asks
// first; if you go ahead, the mission is abandoned the way the pause menu's Abandon does it, and fast travel
// opens once the game has loaded.
(function () {
    'use strict';
    if (window.__FastTravelPlusInstalled) return;
    window.__FastTravelPlusInstalled = true;

    var C = window.__FastTravelPlusConfig || {};
    var DIAG = !!C.diagnostics;
    var URL = 'coui://base/__fasttravelplus__.json';
    var LOG_URL = 'coui://base/__fasttravelplus_log__.json';
    var STYLE_ID = 'ftp-style';
    var ICON = 'coui://base/uiresources/game/symbols/district_upgrades/ICN_DISTRICT_UPGRADE_FAST_TRAVEL.svg';
    var MOD_ID = 'fasttravelplus', HOTKEY_OPTION = 'hotkey';
    var MOD_PAD = 'fasttravelplus_controller', PAD_HOLD = 'pad_hold', PAD_PRESS = 'pad_press';
    var KEYS = C.keys || [];           // the hotkey slider's key names, by position (0 = Off)
    var PAD = C.pad || [];             // the controller sliders' button names, by position (0 = None / Off)
    var TICK_MS = 100;
    // In a mission, a hotkey goes ahead only when held this long, in a press that starts after the question
    // came up; the in-play prompt goes away after PROMPT_IDLE_MS without a hold.
    var HOLD_MS = 5000, PROMPT_IDLE_MS = 10000;
    var TEXT = {
        button: 'Fast Travel',
        unavailable: 'Not available right now',
        heading: 'Abandon the mission?',
        body: "You're in a mission. Fast travelling abandons it, the same as Abandon in the pause menu, and you'll " +
            'have to start it again. The fast travel menu opens once the game has loaded.',
        also: 'You can also hold {key} for 5 seconds to go ahead.',
        prompt: "You're in a mission. Hold {key} for 5 seconds to abandon it and fast travel. The fast travel menu " +
            'opens once the game has loaded.',
        cancel: 'Cancel',
        confirm: 'Abandon and fast travel',
        failed: "Couldn't abandon the mission"
    };
    // After Abandon: wait this long for the loading screen, then for the game to settle after it.
    var LOAD_START_MS = 20000, LOAD_MAX_MS = 180000, SETTLE_MS = 1500;
    var CSS =
        '.ftp-callout{display:flex;flex-direction:row;align-items:center}' +
        '.ftp-callout__icon{width:2.3148148148vh;height:2.3148148148vh}' +
        '.ftp-callout.action-callout--hovered .ftp-callout__icon{--icon-color:rgb(13, 13, 13)}' +
        '.modal.ftp-modal{opacity:1;z-index:50}' +
        '.ftp-modal .modal__window{height:auto;min-height:37.037037037vh}' +
        '.ftp-modal__heading{margin-bottom:1.8518518519vh}' +
        '.ftp-modal__text{font-size:2.2222222222vh;font-weight:400;line-height:1.4;margin-bottom:2.7777777778vh}' +
        '.ftp-prompt{position:fixed;top:13.8888888889vh;left:0;width:100vw;display:flex;flex-direction:row;' +
        'justify-content:center;pointer-events:none;z-index:50}' +
        '.ftp-prompt__box{width:55.5555555556vh;padding:2.2222222222vh 2.7777777778vh;' +
        'background-color:rgb(232, 232, 232);color:rgb(13, 13, 13)}' +
        '.ftp-prompt__heading{font-size:2.7777777778vh;font-weight:700;margin-bottom:.9259259259vh}' +
        '.ftp-prompt__text{font-size:2.2222222222vh;font-weight:400;line-height:1.4}' +
        '.ftp-hold{height:.5555555556vh;margin-top:1.3888888889vh;background-color:rgba(13, 13, 13, 0.2)}' +
        '.ftp-modal .ftp-hold{margin-top:0;margin-bottom:2.7777777778vh}' +
        '.ftp-hold__fill{height:100%;width:0;background-color:rgb(13, 13, 13)}';

    function model(k, d) { var m = window[k]; return m && m.value !== undefined ? m.value : d; }
    function loc(k, d) {
        var m = window[k], t = m && (m.translation !== undefined ? m.translation : m.value);
        return typeof t === 'string' && t ? t : d;
    }
    function now() { return Date.now(); }
    function hex(s) { var o = ''; for (var i = 0; i < s.length; i++) o += ('0' + s.charCodeAt(i).toString(16)).slice(-2); return o; }
    function connected(n) { for (var i = 0; n && i < 128; i++, n = n.parentNode) if (n === document.body) return true; return false; }
    function el(tag, cls, text) {
        var n = document.createElement(tag);
        if (cls) n.className = cls;
        if (text !== undefined) n.textContent = text;
        return n;
    }
    function remove(n) { if (n && n.parentNode) n.parentNode.removeChild(n); }

    var logCount = 0;
    function log(msg, force) {
        if (!DIAG && !force) return;
        if (++logCount > 400) return;
        try {
            var x = new XMLHttpRequest();
            x.open('GET', LOG_URL + '?m=' + encodeURIComponent(String(msg).slice(0, 1400)), true);
            x.send();
        } catch (e) { /* logging must never throw */ }
    }

    function getJson(url, done, timeoutMs) {
        var x;
        try { x = new XMLHttpRequest(); } catch (e) { done(null, String(e)); return; }
        var finished = false, timer = setTimeout(function () {
            if (finished) return; finished = true;
            try { x.abort(); } catch (e) { }
            done(null, 'timeout');
        }, timeoutMs || 1500);
        x.onload = function () {
            if (finished) return; finished = true; clearTimeout(timer);
            var r = null;
            try { if (x.status === 200 || x.status === 0) r = JSON.parse(x.responseText); } catch (e) { r = null; }
            done(r, r ? '' : 'bad response');
        };
        x.onerror = function () {
            if (finished) return; finished = true; clearTimeout(timer);
            done(null, 'request failed');
        };
        try { x.open('GET', url, true); x.send(); } catch (e) { if (!finished) { finished = true; clearTimeout(timer); done(null, String(e)); } }
    }

    var nonce = 0;
    function act(action, done) {
        getJson(URL + '?a=' + action + '&n=' + (++nonce), function (r, err) {
            log(action + ' -> ' + (r ? JSON.stringify(r) : err));
            if (done) done(r, err);
        });
    }

    function ensureStyle() {
        if (document.getElementById(STYLE_ID)) return;
        var st = el('style');
        st.id = STYLE_ID;
        st.textContent = CSS;
        (document.head || document.body).appendChild(st);
    }

    // ---------------------------------------------------------------- the hotkeys
    // The sliders' positions live in Mod Settings Menu (window.CMM); without it, the DLL's saved values.
    function menuPosition(mod, key, count, fallback) {
        var v;
        try { if (window.CMM && typeof window.CMM.value === 'function') v = window.CMM.value(mod, key); } catch (e) { v = undefined; }
        if (typeof v === 'number' && isFinite(v) && Math.round(v) >= 0 && Math.round(v) < count) return Math.round(v);
        return fallback;
    }
    function hotkeyIndex() { return menuPosition(MOD_ID, HOTKEY_OPTION, KEYS.length, typeof C.hotkey === 'number' ? C.hotkey : 1); }
    function padHoldIndex() { return menuPosition(MOD_PAD, PAD_HOLD, PAD.length, typeof C.padHold === 'number' ? C.padHold : 11); }
    function padPressIndex() { return menuPosition(MOD_PAD, PAD_PRESS, PAD.length, typeof C.padPress === 'number' ? C.padPress : 12); }
    function hotkeyName() { var i = hotkeyIndex(); return i > 0 && KEYS[i] ? KEYS[i] : ''; }
    function padName(i) { return i > 0 && PAD[i] ? PAD[i] : ''; }
    // "LS (L3) + RS (R3)", or one button when Hold is None; '' when the controller hotkey is off.
    function comboName() {
        var p = padPressIndex(), h = padHoldIndex();
        if (!padName(p)) return '';
        return (padName(h) ? padName(h) + ' + ' : '') + padName(p);
    }
    function inputName(source) { return source === 'pad' ? comboName() : hotkeyName(); }
    function withKey(s, source) {
        var names = source ? [inputName(source)] : [hotkeyName(), comboName()];
        return s.replace('{key}', names.filter(function (n) { return n; }).join(' or '));
    }

    // The DLL counts presses of each and times the current one; a poll picks up new presses and hold times, and
    // tells the DLL when a slider moved.
    var hk = { presses: null, padPresses: null, keyHeld: 0, padHeld: 0, polling: false,
        sent: typeof C.hotkey === 'number' ? C.hotkey : null,
        sentHold: typeof C.padHold === 'number' ? C.padHold : null, sentPress: typeof C.padPress === 'number' ? C.padPress : null };
    function fresh(field, value) {
        if (typeof value !== 'number') return false;
        var last = hk[field];
        hk[field] = value;
        return last !== null && value > last && value - last < 50;
    }
    function pollHotkey() {
        if (hk.polling) return;
        var index = hotkeyIndex(), hold = padHoldIndex(), press = padPressIndex();
        var action = index !== hk.sent ? 'hotkey&k=' + index
            : hold !== hk.sentHold || press !== hk.sentPress ? 'pad&h=' + hold + '&p=' + press : 'status';
        hk.polling = true;
        getJson(URL + '?a=' + action + '&n=' + (++nonce), function (r) {
            hk.polling = false;
            if (!r || !r.ok) return;
            if (action.indexOf('hotkey') === 0 && r.accepted) { hk.sent = index; log('hotkey: ' + (KEYS[index] || index)); }
            if (action.indexOf('pad') === 0 && r.accepted) { hk.sentHold = hold; hk.sentPress = press; log('controller hotkey: ' + (comboName() || 'off')); }
            hk.keyHeld = typeof r.keyHeld === 'number' ? r.keyHeld : 0;
            hk.padHeld = typeof r.padHeld === 'number' ? r.padHeld : 0;
            var key = fresh('presses', r.presses), pad = fresh('padPresses', r.padPresses);
            if (key && hotkeyIndex() > 0) onHotkey('key');
            else if (pad && padPressIndex() > 0) onHotkey('pad');
        }, 1000);
    }

    // What a press does depends on the screen: it closes the fast travel menu, acts like the map's Fast Travel on
    // the map, and opens fast travel during plain play. While our mission question is up a press does nothing:
    // only a 5 s hold goes ahead (tickModal). The keyboard hotkey and the controller combo do the same; source
    // only says which one to name in the prompt.
    function onHotkey(source) {
        if (ui.modal) return;
        if (model('ui_stacks_game_states_fast_travel_current', false)) {
            log('hotkey: closing fast travel');
            try { if (window.engine && typeof engine.trigger === 'function') engine.trigger('fast_travel_close'); } catch (e) { }
            return;
        }
        if (model('ui_stacks_game_states_map_active', false)) { press(); return; }
        if (!playable()) { log('hotkey: not now (' + model('ui_stacks_game_state', '?') + ')'); return; }
        if (ui.busy) return;
        ui.busy = true;
        act('status', function (s) {
            ui.busy = false;
            if (!s || !s.installed || s.busy) { log('hotkey: unavailable', true); return; }
            if (s.inMission) { if (playable()) showPrompt(source); return; }
            open();
        });
    }

    // ---------------------------------------------------------------- the map button
    var ui = { bar: null, button: null, label: null, modal: null, modalKind: '', fill: null, idleSince: 0, basePresses: 0,
        basePadPresses: 0, flashUntil: 0, lastPress: 0,
        busy: false };

    // The map's own action bar: the absolute callout set that holds the "toggle legend" callout.
    function findBar() {
        if (ui.bar && connected(ui.bar)) return ui.bar;
        ui.bar = null;
        var sets = document.querySelectorAll('.action-callout-set--absolute');
        for (var i = 0; i < sets.length && !ui.bar; i++) {
            var callouts = sets[i].querySelectorAll('.action-callout--interactive');
            for (var j = 0; j < callouts.length; j++) {
                var click = callouts[j].getAttribute('data-bind-click');
                if (click && click.indexOf('map_toggle_legend') !== -1) { ui.bar = sets[i]; break; }
            }
        }
        return ui.bar;
    }

    function makeButton() {
        var b = el('div', 'action-callout action-callout--interactive ftp-callout');
        var icon = el('div', 'icon icon--mask ftp-callout__icon');
        icon.setAttribute('style', 'mask-image: url(' + ICON + ');');
        b.appendChild(icon);
        ui.label = el('div', 'action-callout__label action-callout__label--right', buttonText());
        b.appendChild(ui.label);
        b.__ftp = true;
        // The game's own callouts react to click; mouseup is a fallback in case click never arrives.
        b.addEventListener('click', press);
        b.addEventListener('mouseup', press);
        b.addEventListener('mouseenter', function () { b.classList.add('action-callout--hovered'); });
        b.addEventListener('mouseleave', function () { b.classList.remove('action-callout--hovered'); });
        return b;
    }

    function buttonText() { return now() < ui.flashUntil ? TEXT.unavailable : loc('ui_loc_MENU_FAST_TRAVEL', TEXT.button); }

    function flash(why) {
        log('button: ' + why, true);
        ui.flashUntil = now() + 2500;
        if (ui.label) ui.label.textContent = buttonText();
    }

    // The map page is cloned while hidden and the clone comes back when it shows again, so copies of our
    // button without listeners can appear. Keep exactly one live button, first (leftmost) in the live bar.
    function ensureButton(mapOpen) {
        var copies = document.querySelectorAll('.ftp-callout');
        for (var i = 0; i < copies.length; i++) if (copies[i] !== ui.button) remove(copies[i]);
        if (!mapOpen || !C.hooks) { remove(ui.button); return; }
        var bar = findBar();
        if (!bar) return;
        ensureStyle();
        if (!ui.button) ui.button = makeButton();
        if (bar.firstChild !== ui.button) { bar.insertBefore(ui.button, bar.firstChild); log('button added to the map bar'); }
        var t = buttonText();
        if (ui.label && ui.label.textContent !== t) ui.label.textContent = t;
    }

    function press() {
        var t = now();
        if (t - ui.lastPress < 400 || ui.busy || ui.modal) return;
        ui.lastPress = t;
        ui.busy = true;
        act('status', function (s) {
            ui.busy = false;
            if (!s || !s.installed) { flash('status unavailable'); return; }
            if (s.busy) { flash('busy'); return; }
            if (s.inMission) { showModal(); return; }
            open();
        });
    }

    function open() {
        act('open', function (r) { if (!r || !r.accepted) flash('open refused'); });
    }

    // ---------------------------------------------------------------- the mission question
    // On the map (the mouse is there): the game's modal with Cancel and Go ahead. During play (no mouse): a
    // prompt. Either way a hotkey held for HOLD_MS goes ahead, with a bar that fills while it's held.
    function callout(text, onPress) {
        var c = el('div', 'action-callout modal__actions__callout action-callout--inverted action-callout--interactive');
        c.appendChild(el('div', 'action-callout__label', text));
        var last = 0;
        function go() { var t = now(); if (t - last < 400) return; last = t; onPress(); }
        c.addEventListener('click', go);
        c.addEventListener('mouseup', go);
        c.addEventListener('mouseenter', function () { c.classList.add('action-callout--hovered'); });
        c.addEventListener('mouseleave', function () { c.classList.remove('action-callout--hovered'); });
        return c;
    }

    function showModal() {
        closeModal();
        ensureStyle();
        var root = el('div', 'modal modal__visible-container ftp-modal');
        var win = root.appendChild(el('div', 'modal__window'));
        var body = win.appendChild(el('div', 'modal__body'));
        var top = body.appendChild(el('div'));
        top.appendChild(el('div', 'ftp-modal__heading', TEXT.heading));
        var hotkeys = hotkeyName() || comboName();
        top.appendChild(el('p', 'ftp-modal__text', TEXT.body + (hotkeys ? ' ' + withKey(TEXT.also) : '')));
        if (hotkeys) top.appendChild(holdBar());
        var actions = body.appendChild(el('div', 'modal__actions'));
        actions.appendChild(callout(loc('ui_loc_EXIT_MODAL_CANCEL', TEXT.cancel), function () { log('mission warning: cancel', true); closeModal(); }));
        actions.appendChild(callout(TEXT.confirm, confirmAbandon));
        document.body.appendChild(root);
        opened(root, 'map');
        log('mission warning shown', true);
    }

    function showPrompt(source) {
        closeModal();
        ensureStyle();
        var root = el('div', 'ftp-prompt');
        var box = root.appendChild(el('div', 'ftp-prompt__box'));
        box.appendChild(el('div', 'ftp-prompt__heading', TEXT.heading));
        box.appendChild(el('div', 'ftp-prompt__text', withKey(TEXT.prompt, source || 'key')));
        box.appendChild(holdBar());
        document.body.appendChild(root);
        opened(root, 'prompt');
        log('mission prompt shown', true);
    }

    function holdBar() {
        var bar = el('div', 'ftp-hold');
        ui.fill = bar.appendChild(el('div', 'ftp-hold__fill'));
        return bar;
    }

    // Only presses after this point count, so the press that asked the question can't answer it.
    function opened(root, kind) {
        ui.modal = root; ui.modalKind = kind; ui.idleSince = now();
        ui.basePresses = hk.presses || 0;
        ui.basePadPresses = hk.padPresses || 0;
    }

    function closeModal() { remove(ui.modal); ui.modal = null; ui.modalKind = ''; ui.fill = null; }

    // How long a hotkey has been held, in a press that started after the question came up.
    function holding() {
        var ms = 0;
        if (hotkeyIndex() > 0 && hk.presses > ui.basePresses) ms = Math.max(ms, hk.keyHeld);
        if (padPressIndex() > 0 && hk.padPresses > ui.basePadPresses) ms = Math.max(ms, hk.padHeld);
        return ms;
    }

    // A HOLD_MS hold goes ahead. The question goes away when its screen does (the map closing, play being
    // interrupted), and the in-play prompt after PROMPT_IDLE_MS without a hold.
    function tickModal(t, mapOpen) {
        if (!ui.modal) return;
        if (!connected(ui.modal) || (ui.modalKind === 'map' && !mapOpen) || (ui.modalKind === 'prompt' && !playable())) {
            closeModal();
            return;
        }
        var held = holding();
        if (ui.fill) {
            var width = Math.min(100, Math.round(held / HOLD_MS * 100)) + '%';
            if (ui.fill.style.width !== width) ui.fill.style.width = width;
        }
        if (held >= HOLD_MS) { log('mission question: held, going ahead', true); confirmAbandon(); return; }
        if (held > 0) ui.idleSince = t;
        else if (ui.modalKind === 'prompt' && t - ui.idleSince > PROMPT_IDLE_MS) closeModal();
    }

    function confirmAbandon() {
        closeModal();
        log('mission warning: abandon and fast travel', true);
        act('abandon', function (r) {
            if (!r || !r.accepted) { flash('abandon refused'); return; }
            follow = { stage: 'result', since: now(), serial: r.serial };
        });
    }

    // ---------------------------------------------------------------- after Abandon
    // Stages: result (the DLL runs Abandon on the next frame) -> load (wait for the loading screen) ->
    // loading -> settle (the game is back; wait a moment) -> open.
    var follow = null, polling = false;

    // Plain play: no loading screen, and the game stack's top state is exploration or combat (no menu,
    // conversation or cutscene on top).
    function playable() {
        if (model('ui_loading_screen_visible', false)) return false;
        if (!model('ui_stacks_program_flow_states_game_current', true)) return false;
        var state = model('ui_stacks_game_state', null);
        if (typeof state === 'string' && state) return state === 'exploration' || state === 'combat';
        var exploring = model('ui_stacks_game_states_exploration_current', null);
        if (exploring !== null) return !!exploring || !!model('ui_stacks_game_states_combat_current', false);
        return !model('ui_gameplay_menu_open', false);
    }

    function tickFollow(t) {
        if (!follow) return;
        var loading = !!model('ui_loading_screen_visible', false);
        if (follow.stage === 'result') {
            if (polling) return;
            if (t - follow.since > 5000) { log('abandon: no answer from the game', true); follow = null; return; }
            polling = true;
            act('status', function (s) {
                polling = false;
                if (!follow || !s || s.serial === follow.serial) return;
                if (s.result === 'abandoning') { follow = { stage: 'load', since: now() }; return; }
                flash(TEXT.failed + ' (' + s.result + ')');
                follow = null;
            });
        } else if (follow.stage === 'load') {
            if (loading) follow = { stage: 'loading', since: t };
            else if (t - follow.since > LOAD_START_MS) { log('abandon: the loading screen never showed', true); act('clear'); follow = null; }
        } else if (follow.stage === 'loading') {
            if (!loading) follow = { stage: 'settle', since: t };
            else if (t - follow.since > LOAD_MAX_MS) { log('abandon: still loading, giving up', true); act('clear'); follow = null; }
        } else if (follow.stage === 'settle') {
            if (loading) { follow = { stage: 'loading', since: t }; return; }
            if (t - follow.since < SETTLE_MS || !playable()) {
                if (t - follow.since > 60000) { log('abandon: the game never became playable', true); act('clear'); follow = null; }
                return;
            }
            log('abandon: loaded, opening fast travel', true);
            follow = null;
            open();
        }
    }

    // ---------------------------------------------------------------- the MODS page
    // Mod Settings Menu shows our sliders' positions as numbers; next to each slider we show the key or
    // button's name instead, in a copy of the number's element that follows its classes (focus and section
    // colours). Mod Settings Menu names an option cmm_<hex of the mod id>_<hex of the option id>.
    function binding(mod, option) { return 'cmm_' + hex(mod) + '_' + hex(option); }
    var sliders = [
        { binding: binding(MOD_ID, HOTKEY_OPTION), name: function () { return KEYS[hotkeyIndex()] || ''; } },
        { binding: binding(MOD_PAD, PAD_HOLD), name: function () { return padName(padHoldIndex()) || 'None'; } },
        { binding: binding(MOD_PAD, PAD_PRESS), name: function () { return padName(padPressIndex()) || 'Off'; } }
    ];
    sliders.forEach(function (s) { s.src = null; s.el = null; });
    var sliderSearch = 0;
    function sliderFor(node) {
        var at = node.attributes;
        for (var j = 0; at && j < at.length; j++) {
            var v = at[j] && at[j].value;
            if (typeof v !== 'string' || v.indexOf('cmm_') === -1) continue;
            for (var k = 0; k < sliders.length; k++) if (v.indexOf(sliders[k].binding + '_') !== -1) return sliders[k];
        }
        return null;
    }
    function tickSlider(t) {
        var open = model('ui_stacks_menu_options_active', false) && model('ui_stacks_menu_options_states_mods_active', false);
        if (!open) return;
        var missing = false;
        sliders.forEach(function (s) {
            if (s.src && !connected(s.src)) s.src = s.el = null;
            missing = missing || !s.src;
        });
        if (missing && t - sliderSearch >= 300) {
            sliderSearch = t;
            var values = document.querySelectorAll('.options-slider__value');
            for (var i = 0; i < values.length; i++) {
                var s = sliderFor(values[i]);
                if (!s || s.src || !values[i].parentNode) continue;
                // A page copy taken while ours showed has an old name in it: out, and the number back.
                var old = values[i].parentNode.querySelectorAll('.ftp-key');
                for (var j = 0; j < old.length; j++) remove(old[j]);
                s.src = values[i];
                s.el = el('div');
                values[i].parentNode.insertBefore(s.el, values[i].nextSibling);
                log('slider found: ' + s.binding);
            }
        }
        sliders.forEach(function (s) {
            if (!s.src) return;
            var cls = s.src.className + ' ftp-key';
            if (s.el.className !== cls) s.el.className = cls;
            var name = s.name();
            if (s.el.textContent !== name) s.el.textContent = name;
            if (s.src.style.display !== 'none') s.src.style.display = 'none';
        });
    }

    // ---------------------------------------------------------------- main loop
    var lastMap = null;
    function tick() {
        try {
            var t = now();
            var mapOpen = !!model('ui_stacks_game_states_map_active', false);
            if (mapOpen !== lastMap) { log('map ' + (mapOpen ? 'open' : 'closed')); lastMap = mapOpen; }
            if (C.hooks) pollHotkey();
            ensureButton(mapOpen);
            tickModal(t, mapOpen);
            tickFollow(t);
            tickSlider(t);
        } catch (e) {
            log('tick error: ' + (e && e.stack || e), true);
        }
    }

    // A UI reload in the middle of the after-Abandon wait picks the wait up again.
    act('status', function (s) {
        if (s && s.abandonAge >= 0 && s.abandonAge < LOAD_MAX_MS) follow = { stage: 'load', since: now() };
    });
    var timer = setInterval(tick, TICK_MS);
    window.FastTravelPlus = {
        version: C.version,
        stop: function () {
            clearInterval(timer); closeModal(); remove(ui.button);
            sliders.forEach(function (s) { remove(s.el); if (s.src) s.src.style.display = ''; });
            remove(document.getElementById(STYLE_ID));
        },
        state: function () {
            return { follow: follow, button: !!(ui.button && connected(ui.button)), modal: ui.modalKind, hotkey: hotkeyName(),
                controller: comboName() };
        }
    };
    log('Fast Travel Plus ' + (C.version || '?') + ' started (hooks ' + !!C.hooks + ', hotkey ' + (hotkeyName() || 'off') +
        ', controller ' + (comboName() || 'off') + ')', true);
})();

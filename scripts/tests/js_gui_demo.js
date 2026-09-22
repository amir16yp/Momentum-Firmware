// Host smoke test: node scripts/tests/js_gui_demo.js
// Models the device API; does not measure native heap use or replace device testing.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname,
    "../../applications/system/js_app/examples/apps/Scripts/Examples/gui.js"), "utf8");
const demoModules = ["loading", "empty_screen", "text_input", "byte_input",
    "text_box", "file_picker", "widget", "button_menu", "button_panel", "menu",
    "number_input", "popup", "vi_list"];

function runDemo(index, back = false, pickedPath = "/ext/test.txt", sdkSupport = true) {
    const loaded = [];
    const views = [];
    const subscriptions = new Map();
    const timers = [];
    let runs = 0;
    let stopped = false;
    const dispatcher = {
        navigation: {},
        switchTo(view) { assert.ok(views.includes(view)); this.currentView = view; },
    };
    function emit(event, value) {
        const sub = subscriptions.get(event);
        assert.ok(sub, "event must have a subscription");
        const result = sub.callback({}, value, ...sub.args);
        if (Array.isArray(result)) sub.args = result;
    }
    const loop = {
        subscribe(event, callback, ...args) {
            assert.ok(event);
            assert.ok(!subscriptions.has(event));
            subscriptions.set(event, { callback, args });
        },
        timer(mode, interval) {
            const timer = { mode, interval };
            timers.push(timer);
            return timer;
        },
        stop() { stopped = true; },
        run() {
            stopped = false;
            if (++runs === 1) {
                assert.deepEqual(loaded, ["event_loop", "gui", "gui/submenu"]);
                assert.equal(views.length, 1);
                assert.equal(timers.length, 0);
                if (index < 0) emit(dispatcher.navigation);
                else emit(dispatcher.currentView.chosen, index);
            } else {
                assert.equal(runs, 2);
                assert.ok(loaded.includes("gui/" + demoModules[index]));
                assert.ok(loaded.length <= 6, "only selected demo dependencies are loaded");
                assert.ok(views.length <= 3, "only chooser, selected view and optional dialog exist");
                const view = dispatcher.currentView;
                if (back) emit(dispatcher.navigation);
                else if (index === 0) {
                    assert.equal(timers[0].mode, "oneshot");
                    emit(timers[0]);
                } else if (index === 6) {
                    assert.equal(timers.length, 1);
                    for (let tick = 0; tick <= 120; tick++) emit(timers[0]);
                    assert.equal(view.children[0].text, "01:00");
                    emit(view.button, { key: "right", type: "short" });
                } else if (index === 11) emit(view.timeout);
                else if ([2, 3, 5, 7, 8, 9, 10].includes(index)) {
                    if (index === 2) {
                        assert.equal(view.props.illegalSymbols, sdkSupport ? true : undefined);
                        emit(view.input, "Tester");
                    }
                    if (index === 3) emit(view.input, new Uint8Array([17, 34]).buffer);
                    if (index === 7 || index === 8) emit(view.input, { index: 1 });
                    if (index === 9) emit(view.chosen, 1);
                    if (index === 10) emit(view.input, 123);
                    assert.equal(dispatcher.currentView.module, "gui/dialog");
                    if (index === 5) assert.equal(dispatcher.currentView.props.text,
                        pickedPath ? "You selected:\n" + pickedPath : "You didn't select a file");
                    emit(dispatcher.currentView.input, "center");
                } else {
                    if (index === 12) emit(view.valueUpdate, {});
                    emit(dispatcher.navigation);
                }
            }
            assert.ok(stopped, "demo must exit on completion or Back");
        },
    };
    function factory(module) {
        function makeWith(props = {}, children = []) {
            const view = { module, props, children,
                input: {}, chosen: {}, button: {}, timeout: {}, valueUpdate: {},
                set(key, value) { this.props[key] = value; },
                setChildren(value) { this.children = value; },
            };
            views.push(view);
            return view;
        }
        return { make: makeWith, makeWith };
    }
    vm.runInNewContext(source, {
        require(name) {
            assert.ok(!loaded.includes(name), "device rejects duplicate require calls");
            loaded.push(name);
            if (name === "event_loop") return loop;
            if (name === "gui") return { viewDispatcher: dispatcher };
            if (name === "math") return Math;
            if (name === "flipper") return { getName: () => "Flipper" };
            if (name === "gui/icon") return { getBuiltin: name => ({ name }) };
            if (name === "gui/file_picker") return { pickFile: () => pickedPath };
            return factory(name);
        },
        doesSdkSupport: () => sdkSupport,
        Uint8Array: value => new Uint8Array(value),
    }, { timeout: 1000 });
    assert.equal(runs, index < 0 || index === 13 ? 1 : 2);
    if (index !== 0 && index !== 6) assert.equal(timers.length, 0);
}

for (let index = -1; index <= 13; index++) runDemo(index);
for (let index = 1; index <= 12; index++) runDemo(index, true);
runDemo(5, false, undefined);
runDemo(5, false, "");
runDemo(2, false, "", false);
console.log("GUI demo smoke tests passed: all selections, Back, timers, input and file picker.");

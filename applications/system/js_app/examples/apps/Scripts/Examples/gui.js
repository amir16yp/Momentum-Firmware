// Run one demo per launch: native plugins stay loaded until the script exits.
// Relaunch gui.js to try another demo without accumulating plugins in RAM.
let eventLoop = require("event_loop");
let gui = require("gui");
let submenuView = require("gui/submenu");
let selection = { index: 13 };
let views = {};
views.demos = submenuView.makeWith({
    header: "Choose demo (Back exits)",
}, [
    "Hourglass screen",
    "Empty screen",
    "Text input & Dialog",
    "Byte input",
    "Text box",
    "File picker",
    "Widget",
    "Button menu",
    "Button panel",
    "Menu",
    "Number input",
    "Popup",
    "Var. item list",
    "Exit app",
]);

eventLoop.subscribe(views.demos.chosen, function (_sub, index, selection, eventLoop) {
    selection.index = index;
    eventLoop.stop();
}, selection, eventLoop);
eventLoop.subscribe(gui.viewDispatcher.navigation, function (_sub, _item, eventLoop) {
    eventLoop.stop();
}, eventLoop);
gui.viewDispatcher.switchTo(views.demos);
eventLoop.run();

let index = selection.index;
if (index === 2 || index === 3 || index === 5 || index === 7 || index === 8 || index === 9 || index === 10) {
    let dialogView = require("gui/dialog");
    views.helloDialog = dialogView.make();
    eventLoop.subscribe(views.helloDialog.input, function (_sub, button, eventLoop) {
        if (button === "center")
            eventLoop.stop();
    }, eventLoop);
}

if (index === 0) {
    let loadingView = require("gui/loading");
    views.loading = loadingView.make();
    eventLoop.subscribe(eventLoop.timer("oneshot", 1000), function (_sub, _item, eventLoop) {
        eventLoop.stop();
    }, eventLoop);
    gui.viewDispatcher.switchTo(views.loading);
} else if (index === 1) {
    let emptyView = require("gui/empty_screen");
    views.empty = emptyView.make();
    gui.viewDispatcher.switchTo(views.empty);
} else if (index === 2) {
    let textInputView = require("gui/text_input");
    let flipper = require("flipper");
    views.keyboard = textInputView.makeWith({
        header: "Enter your name",
        minLength: 0,
        maxLength: 32,
        defaultText: flipper.getName(),
        defaultTextClear: true,
    });
    if (doesSdkSupport(["gui-textinput-illegalsymbols"])) {
        views.keyboard.set("illegalSymbols", true);
    }
    eventLoop.subscribe(views.keyboard.input, function (_sub, name, gui, views) {
        views.keyboard.set("defaultText", name);
        views.helloDialog.set("text", "Hi " + name + "! :)");
        views.helloDialog.set("center", "Hi Flipper! :)");
        gui.viewDispatcher.switchTo(views.helloDialog);
    }, gui, views);
    gui.viewDispatcher.switchTo(views.keyboard);
} else if (index === 3) {
    let byteInputView = require("gui/byte_input");
    views.bytekb = byteInputView.makeWith({
        header: "Look ma, I'm a header text!",
        length: 8,
        defaultData: Uint8Array([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88]),
    });
    eventLoop.subscribe(views.bytekb.input, function (_sub, data, gui, views) {
        let data_view = Uint8Array(data);
        let text = "0x";
        for (let i = 0; i < data_view.length; i++) {
            text += data_view[i].toString(16);
        }
        views.helloDialog.set("text", "You typed:\n" + text);
        views.helloDialog.set("center", "Cool!");
        gui.viewDispatcher.switchTo(views.helloDialog);
    }, gui, views);
    gui.viewDispatcher.switchTo(views.bytekb);
} else if (index === 4) {
    let textBoxView = require("gui/text_box");
    views.longText = textBoxView.makeWith({
        text: "This is a very long string that demonstrates the TextBox view. Use the D-Pad to scroll backwards and forwards.\nLorem ipsum dolor sit amet, consectetur adipiscing elit. Suspendisse rhoncus est malesuada quam egestas ultrices. Maecenas non eros a nulla eleifend vulputate et ut risus. Quisque in mauris mattis, venenatis risus eget, aliquam diam. Fusce pretium feugiat mauris, ut faucibus ex volutpat in. Phasellus volutpat ex sed gravida consectetur. Aliquam sed lectus feugiat, tristique lectus et, bibendum lacus. Ut sit amet augue eu sapien elementum aliquam quis vitae tortor. Vestibulum quis commodo odio. In elementum fermentum massa, eu pellentesque nibh cursus at. Integer eleifend lacus nec purus elementum sodales. Nulla elementum neque urna, non vulputate massa semper sed. Fusce ut nisi vitae dui blandit congue pretium vitae turpis.",
    });
    gui.viewDispatcher.switchTo(views.longText);
} else if (index === 5) {
    let filePicker = require("gui/file_picker");
    let path = filePicker.pickFile("/ext", "*");
    if (path) {
        views.helloDialog.set("text", "You selected:\n" + path);
    } else {
        views.helloDialog.set("text", "You didn't select a file");
    }
    views.helloDialog.set("center", "Nice!");
    gui.viewDispatcher.switchTo(views.helloDialog);
} else if (index === 6) {
    let widget = require("gui/widget");
    let icon = require("gui/icon");
    let math = require("math");
    let cuteDolphinWithWatch = icon.getBuiltin("DolphinWait_59x54");
    let jsLogo = icon.getBuiltin("js_script_10px");
    let stopwatchWidgetElements = [
        { element: "string", x: 67, y: 44, align: "bl", font: "big_numbers", text: "00 00" },
        { element: "string", x: 77, y: 22, align: "bl", font: "primary", text: "Stopwatch" },
        { element: "rect", x: 64, y: 27, w: 28, h: 20, radius: 3, fill: false },
        { element: "rect", x: 100, y: 27, w: 28, h: 20, radius: 3, fill: false },
        { element: "icon", x: 0, y: 5, iconData: cuteDolphinWithWatch },
        { element: "icon", x: 64, y: 13, iconData: jsLogo },
        { element: "button", button: "right", text: "Back" },
    ];
    views.stopwatchWidget = widget.makeWith({}, stopwatchWidgetElements);
    eventLoop.subscribe(views.stopwatchWidget.button, function (_sub, buttonEvent, eventLoop) {
        if (buttonEvent.key === "right" && buttonEvent.type === "short")
            eventLoop.stop();
    }, eventLoop);
    eventLoop.subscribe(eventLoop.timer("periodic", 500), function (_sub, _item, views, stopwatchWidgetElements, halfSeconds, math) {
        let text = math.floor(halfSeconds / 2 / 60).toString();
        if (halfSeconds < 10 * 60 * 2)
            text = "0" + text;

        text += (halfSeconds % 2 === 0) ? ":" : " ";

        if (((halfSeconds / 2) % 60) < 10)
            text += "0";
        text += (math.floor(halfSeconds / 2) % 60).toString();

        stopwatchWidgetElements[0].text = text;
        views.stopwatchWidget.setChildren(stopwatchWidgetElements);

        halfSeconds++;
        return [views, stopwatchWidgetElements, halfSeconds, math];
    }, views, stopwatchWidgetElements, 0, math);
    gui.viewDispatcher.switchTo(views.stopwatchWidget);
} else if (index === 7) {
    let buttonMenuView = require("gui/button_menu");
    views.buttonMenu = buttonMenuView.makeWith({
        header: "Header"
    }, [
        { type: "common", label: "Test" },
        { type: "control", label: "Test2" },
    ]);
    eventLoop.subscribe(views.buttonMenu.input, function (_sub, input, gui, views) {
        views.helloDialog.set("text", "You selected #" + input.index.toString());
        views.helloDialog.set("center", "Cool!");
        gui.viewDispatcher.switchTo(views.helloDialog);
    }, gui, views);
    gui.viewDispatcher.switchTo(views.buttonMenu);
} else if (index === 8) {
    let buttonPanelView = require("gui/button_panel");
    let icon = require("gui/icon");
    let offIcons = [icon.getBuiltin("off_19x20"), icon.getBuiltin("off_hover_19x20")];
    let powerIcons = [icon.getBuiltin("power_19x20"), icon.getBuiltin("power_hover_19x20")];
    views.buttonPanel = buttonPanelView.makeWith({
        matrixSizeX: 2,
        matrixSizeY: 2,
    }, [
        { type: "button", x: 0, y: 0, matrixX: 0, matrixY: 0, icon: offIcons[0], iconSelected: offIcons[1] },
        { type: "button", x: 30, y: 30, matrixX: 1, matrixY: 1, icon: powerIcons[0], iconSelected: powerIcons[1] },
        { type: "label", x: 0, y: 50, text: "Label", font: "primary" },
    ]);
    eventLoop.subscribe(views.buttonPanel.input, function (_sub, input, gui, views) {
        views.helloDialog.set("text", "You selected #" + input.index.toString());
        views.helloDialog.set("center", "Cool!");
        gui.viewDispatcher.switchTo(views.helloDialog);
    }, gui, views);
    gui.viewDispatcher.switchTo(views.buttonPanel);
} else if (index === 9) {
    let menuView = require("gui/menu");
    let icon = require("gui/icon");
    let settingsIcon = icon.getBuiltin("Settings_14");
    views.menu = menuView.makeWith({}, [
        { label: "One", icon: settingsIcon },
        { label: "Two", icon: settingsIcon },
        { label: "three", icon: settingsIcon },
    ]);
    eventLoop.subscribe(views.menu.chosen, function (_sub, index, gui, views) {
        views.helloDialog.set("text", "You selected #" + index.toString());
        views.helloDialog.set("center", "Cool!");
        gui.viewDispatcher.switchTo(views.helloDialog);
    }, gui, views);
    gui.viewDispatcher.switchTo(views.menu);
} else if (index === 10) {
    let numberInputView = require("gui/number_input");
    views.numberKbd = numberInputView.makeWith({
        header: "Number input",
        defaultValue: 100,
        minValue: 0,
        maxValue: 200,
    });
    eventLoop.subscribe(views.numberKbd.input, function (_sub, number, gui, views) {
        views.helloDialog.set("text", "You typed " + number.toString());
        views.helloDialog.set("center", "Cool!");
        gui.viewDispatcher.switchTo(views.helloDialog);
    }, gui, views);
    gui.viewDispatcher.switchTo(views.numberKbd);
} else if (index === 11) {
    let popupView = require("gui/popup");
    views.popup = popupView.makeWith({
        header: "Hello",
        text: "I'm going to be gone\nin 2 seconds",
    });
    views.popup.set("timeout", 2000);
    eventLoop.subscribe(views.popup.timeout, function (_sub, _item, eventLoop) {
        eventLoop.stop();
    }, eventLoop);
    gui.viewDispatcher.switchTo(views.popup);
} else if (index === 12) {
    let viListView = require("gui/vi_list");
    views.viList = viListView.makeWith({}, [
        { label: "One", variants: ["1", "1.0"] },
        { label: "Two", variants: ["2", "2.0"] },
    ]);
    eventLoop.subscribe(views.viList.valueUpdate, function (_sub, _item) {});
    gui.viewDispatcher.switchTo(views.viList);
}

if (index !== 13)
    eventLoop.run();

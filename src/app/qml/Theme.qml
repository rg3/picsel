// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

QtObject {
    // Dark, neutral surroundings so pictures are judged on their own colours.
    readonly property color window: "#141416"
    readonly property color panel: "#1c1c1f"
    readonly property color panelRaised: "#26262a"
    readonly property color tile: "#202023"
    readonly property color border: "#34343a"
    readonly property color text: "#e8e8ea"
    readonly property color textDim: "#9a9aa2"
    readonly property color accent: "#4c8dff"
    readonly property color selected: "#3fb950"
    readonly property color deselected: "#e5534b"
    readonly property color highlightRing: "#ffffff"
    readonly property color danger: "#e5534b"
    // Similar-picture group bars: no reds or greens, which mean (de)selected.
    readonly property var groupColors: ["#4c8dff", "#c084fc", "#f5a524", "#22d3ee", "#f472b6", "#facc15"]

    // Applied to every top-level window so the desktop theme (light KDE
    // palettes for buttons, fields…) never leaks into the dark UI.
    readonly property Palette palette: Palette {
        window: "#1c1c1f"
        windowText: "#e8e8ea"
        base: "#26262a"
        alternateBase: "#1c1c1f"
        text: "#e8e8ea"
        button: "#303035"
        buttonText: "#e8e8ea"
        brightText: "#ffffff"
        highlight: "#4c8dff"
        highlightedText: "#ffffff"
        toolTipBase: "#26262a"
        toolTipText: "#e8e8ea"
        placeholderText: "#7a7a82"
        mid: "#45454c"
        dark: "#121214"
        light: "#505058"
        midlight: "#3a3a40"
        shadow: "#000000"
        disabled.windowText: "#7a7a82"
        disabled.text: "#7a7a82"
        disabled.buttonText: "#7a7a82"
        disabled.button: "#26262a"
    }

    readonly property int gap: 10
    readonly property int frameWidth: 4
    readonly property real highlightScale: 1.08
    readonly property int animMs: 120

    readonly property int fontSmall: 12
    readonly property int fontNormal: 14
    readonly property int fontLarge: 17
}

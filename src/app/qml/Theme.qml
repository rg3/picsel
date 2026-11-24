// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

QtObject {
    id: theme

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
    readonly property color control: "#303035"      // button faces
    readonly property color textDisabled: "#7a7a82" // also placeholders
    // Similar-picture group bars: no reds or greens, which mean (de)selected.
    readonly property var groupColors: ["#4c8dff", "#c084fc", "#f5a524", "#22d3ee", "#f472b6", "#facc15"]

    // Applied to every top-level window so the desktop theme (light KDE
    // palettes for buttons, fields…) never leaks into the dark UI. Built
    // from the named colours above, so the two can't drift apart.
    readonly property Palette palette: Palette {
        // Roles that differ when disabled are set per group: a shorthand
        // (windowText: …) sets every group, and with bindings nothing says
        // whether it is applied before or after disabled.windowText.
        active.windowText: theme.text
        inactive.windowText: theme.text
        disabled.windowText: theme.textDisabled
        active.text: theme.text
        inactive.text: theme.text
        disabled.text: theme.textDisabled
        active.buttonText: theme.text
        inactive.buttonText: theme.text
        disabled.buttonText: theme.textDisabled
        active.button: theme.control
        inactive.button: theme.control
        disabled.button: theme.panelRaised
        // The same in every group.
        window: theme.panel
        base: theme.panelRaised
        alternateBase: theme.panel
        brightText: "#ffffff"
        highlight: theme.accent
        highlightedText: "#ffffff"
        toolTipBase: theme.panelRaised
        toolTipText: theme.text
        placeholderText: theme.textDisabled
        mid: "#45454c"
        dark: "#121214"
        light: "#505058"
        midlight: "#3a3a40"
        shadow: "#000000"
    }

    readonly property int gap: 10
    readonly property int frameWidth: 4
    readonly property real highlightScale: 1.08
    readonly property int animMs: 120

    // Video controls under the video (VideoPlayer). The full view leaves the
    // same room under a video's thumbnail, so the two line up.
    readonly property int videoControlsHeight: 44
    readonly property int videoControlsGap: 8

    readonly property int fontSmall: 12
    readonly property int fontNormal: 14
    readonly property int fontLarge: 17
}

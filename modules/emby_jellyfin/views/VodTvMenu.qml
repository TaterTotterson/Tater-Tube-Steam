import QtQuick
import Components

FocusScope {
    id: menuRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string youtubeModuleId: "com.240mp.youtube_playlist"
    property string vodModuleId: "com.240mp.emby_jellyfin"
    property var rows: []

    function commercialsEnabled() {
        var value = appCore.get_setting(youtubeModuleId, "tv_mode_commercials")
        if (value === undefined || value === null || value === "")
            return true
        return value === true || value === "ON" || value === "true" || value === "1"
    }

    function midrollCommercialsEnabled() {
        var value = appCore.get_setting(vodModuleId, "vod_midroll_commercials")
        return value === true || value === "ON" || value === "true" || value === "1"
    }

    function autoChannelsEnabled() {
        var value = appCore.get_setting(vodModuleId, "vod_auto_channels")
        if (value === undefined || value === null || value === "")
            return true
        return value === true || value === "ON" || value === "true" || value === "1"
    }

    function rebuildRows() {
        rows = [
            { key: "start", title: "START TV MODE" },
            { key: "auto_channels", title: "AUTO CHANNELS " + (autoChannelsEnabled() ? "ON" : "OFF") },
            { key: "commercials", title: "COMMERCIALS " + (commercialsEnabled() ? "ON" : "OFF") },
            { key: "midroll", title: "MID-ROLL " + (midrollCommercialsEnabled() ? "ON" : "OFF") },
            { key: "commercial_categories", title: "COMMERCIAL CATEGORIES" }
        ]
    }

    function selectRow(index) {
        if (index < 0 || index >= rows.length)
            return
        var row = rows[index] || ({})
        if (row.key === "start") {
            navigateTo("VodTvMode.qml", {}, { currentIndex: menuList.currentIndex })
        } else if (row.key === "auto_channels") {
            appCore.save_setting(vodModuleId, "vod_auto_channels",
                                 autoChannelsEnabled() ? "OFF" : "ON")
            rebuildRows()
        } else if (row.key === "commercials") {
            appCore.save_setting(youtubeModuleId, "tv_mode_commercials",
                                 commercialsEnabled() ? "OFF" : "ON")
            rebuildRows()
        } else if (row.key === "midroll") {
            appCore.save_setting(vodModuleId, "vod_midroll_commercials",
                                 midrollCommercialsEnabled() ? "OFF" : "ON")
            rebuildRows()
        } else if (row.key === "commercial_categories") {
            navigateTo("../../../views/MultiSelectSettings.qml", {
                moduleId: youtubeModuleId,
                settingKey: "vod_commercial_categories",
                settingLabel: "VOD COMMERCIALS"
            }, { currentIndex: menuList.currentIndex })
        }
    }

    Component.onCompleted: {
        rebuildRows()
        var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
        menuList.currentIndex = Math.max(0, Math.min(restore, rows.length - 1))
    }

    focus: true

    StaticBackground {
        anchors.fill: parent
        themeName: root.currentTheme
        visible: root.staticBackgroundEnabled
        running: visible
    }

    Rectangle {
        anchors.fill: parent
        color: root.staticBackgroundEnabled ? "transparent" : root.surfaceColor
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        iconHeight: root.sh * 0.075
        title: moduleRoot.moduleName
        subtitle: "TV MODE"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    ListView {
        id: menuList
        model: rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onReturnPressed: menuRoot.selectRow(currentIndex)
        Keys.onEnterPressed: menuRoot.selectRow(currentIndex)
        Keys.onSpacePressed: menuRoot.selectRow(currentIndex)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                menuRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: menuList.width
            height: root.sh * 0.0583333

            Rectangle {
                anchors.fill: rowText
                color: root.accentColor
                visible: menuList.currentIndex === index
            }

            Text {
                id: rowText
                text: modelData.title || ""
                color: menuList.currentIndex === index ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                elide: Text.ElideRight
                leftPadding: root.sw * 0.009375
                rightPadding: root.sw * 0.009375
                font.pixelSize: root.sh * 0.05
            }
        }
    }
}

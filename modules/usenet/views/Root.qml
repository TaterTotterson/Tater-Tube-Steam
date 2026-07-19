import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})
    property string moduleId: "com.240mp.usenet"
    property var _moduleInfo: appCore.get_module_info(moduleId)
    property string moduleName: _moduleInfo.name || "THE TUBE"
    property string moduleIcon: _moduleInfo.icon || ""
    property var navStack: []
    property var currentParams: ({})
    property double lastBackNavigationAtMs: 0

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function replaceWith(viewPath, params) {
        var resolved = Qt.resolvedUrl(viewPath)
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        var now = Date.now()
        if (now - lastBackNavigationAtMs < 220)
            return
        lastBackNavigationAtMs = now
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var previous = navStack.pop()
        if (!previous.source || previous.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        var restored = Object.assign({}, previous.params)
        restored.navListState = previous.listState || {}
        currentParams = restored
        internalLoader.setSource(previous.source, { "navParams": restored })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    Connections {
        target: usenetBackend
        function onAuthStateChanged() {
            if (usenetBackend.get_auth_state() !== "authed") {
                moduleRoot.navStack = []
                moduleRoot.replaceWith("Browse.qml", {})
            }
        }
    }

    Component.onCompleted: {
        var recommendation = navParams.recommendation || ({})
        var launch = recommendation.launch || ({})
        if (launch.type === "localFile") {
            navigateTo("LocalPlayer.qml", {
                item: launch,
                title: recommendation.title || launch.title || "TATER'S PICK"
            })
        } else if (launch.type === "localFolder" && (launch.mediaType || "") === "show") {
            navigateTo("LocalShow.qml", {
                item: launch,
                libraryName: appCore.taterPicksTitle
            })
        } else if (launch.type === "localFolder" && (launch.mediaType || "") === "season") {
            navigateTo("LocalSeason.qml", {
                item: launch,
                showTitle: launch.title || "TATER'S PICK",
                libraryName: appCore.taterPicksTitle
            })
        } else {
            navigateTo("Browse.qml", {})
        }
    }
}

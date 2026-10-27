import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// Lyric overlay: a vertical stream of words. The newest word carries the
// highlight block; older words rise above it, shrink and fade. Animations run
// only on word changes (plus the highlight sweep while a word is sung), and
// pause with playback, so the scene idles when nothing plays.
Item {
    id: root
    width: 1280
    height: 760

    property var wordStream: []       // [{ text, glyph }] across the whole song
    property int currentWord: -1      // global index of the newest sung word, -1 before the first
    property bool wordActive: false   // currentWord is being sung right now
    property bool playing: false
    property int style: 3             // 0 Drift, 1 Lens, 2 Lens + Glyphs, 3 Glyphs
    property color textColor: "#ee6e55"      // lyric
    property color highlightColor: "#e8566f" // block behind the sung word, symbols
    property color secondaryColor: "#b8392f" // older and upcoming words
    property string statusText: ""
    property string timingNote: "" // shown small while estimated lyrics play
    property string attribution: "" // required credit for the lyrics source; shown with the lyrics
    property bool shaderLens: true
    property string fontFamily: ""
    property real textScale: 1.0
    property bool backdrop: true
    property string trackTitle: ""
    property string trackArtist: ""
    property string artSource: "" // image://art/<n>, empty without artwork
    property bool artBackdrop: true
    property bool continuousDrift: false
    property bool immersive: false // screensaver mode: full screen, lock-screen dim

    // Drift style, opt-in: the wall keeps floating while music plays. This is the one
    // looping animation, and it stops whenever playback pauses or nothing is shown.
    readonly property bool drifting: ship && continuousDrift && playing && stack.count > 0
    property real driftPhase: 0
    NumberAnimation on driftPhase {
        from: 0
        to: Math.PI * 2
        duration: 16000
        loops: Animation.Infinite
        running: root.drifting
    }

    // Before the first sung word: a title card instead of an empty screen.
    readonly property bool showIntro: stack.count === 0 && trackTitle !== ""

    // A neutral grotesque suits the look. Apple's SF Pro is never shipped (its licence forbids that), but is used
    // when the user has installed it; otherwise Inter Display, the closest open face; else bundled Nunito.
    function firstInstalled(names) {
        var installed = Qt.fontFamilies();
        for (var i = 0; i < names.length; ++i)
            if (installed.indexOf(names[i]) >= 0)
                return names[i];
        return "Nunito";
    }
    readonly property string family: fontFamily !== "" ? fontFamily
                                   : firstInstalled(["SF Pro Display", "SF Pro", "Inter Display", "Inter"])
    readonly property bool lens: style === 1 || style === 2
    readonly property bool glyphs: style === 2 || style === 3
    readonly property bool ship: style === 0
    readonly property bool karaoke: style === 4
    readonly property real unit: Math.min(height * 0.16, width * 0.1) * textScale * (immersive ? 1.3 : 1) // fullscreen: lock-screen scale
    readonly property real anchorY: height * (lens ? 0.5 : 0.6)
    readonly property int maxAge: lens ? 4 : 2 // the sung word plus two above it
    readonly property int maxLookahead: lens ? 2 : 0

    // Highlight sweep across the sung word, driven by C++ at each word change.
    property real sweep: 1
    NumberAnimation {
        id: sweepAnimation
        target: root
        property: "sweep"
        to: 1
        paused: running && !root.playing
    }

    // Instrumental break: notes bob while the frozen words dim; cleared 1.5 s before singing resumes.
    property bool instrumental: false
    Timer { id: breakTimer; repeat: false; onTriggered: root.instrumental = false }
    onPlayingChanged: if (!playing) breakTimer.stop() // the resume frame recomputes the break
    property real breakPhase: 0
    NumberAnimation on breakPhase {
        from: 0
        to: Math.PI * 2
        duration: 2400
        loops: Animation.Infinite
        running: root.instrumental && root.playing
    }

    // Called by C++ with one consistent frame.
    function setFrame(word, active, progress, remainingMs, breakMs) {
        breakTimer.stop();
        instrumental = (breakMs || 0) !== 0;
        if (breakMs > 0) {
            breakTimer.interval = Math.max(0, breakMs - 1500);
            breakTimer.start();
        }
        currentWord = word;
        wordActive = active;
        sweepAnimation.stop();
        sweep = active ? progress : 1;
        if (active && remainingMs > 0) {
            sweepAnimation.duration = remainingMs;
            sweepAnimation.start();
        }
        syncStack();
    }

    onWordStreamChanged: { stack.clear(); syncStack(); }
    onLensChanged: syncStack()

    // Relative size by age (0 = newest; negative = upcoming, lens styles only).
    function sizeFor(age) {
        var a = Math.abs(age);
        if (lens)
            return [1.6, 1.0, 0.72, 0.52, 0.4][Math.min(a, 4)];
        return [0.62, 1.0, 0.68, 0.5, 0.4][Math.min(a, 4)]; // sung word small in its block; it grows as it rises
    }
    function opacityFor(age) {
        if (age < 0)
            return age === -1 ? 0.45 : 0.22;
        return [1, 1, 0.62, 0.3, 0.15][Math.min(age, 4)];
    }
    function rowCenter(age) {
        var y = anchorY;
        var step = age > 0 ? 1 : -1;
        for (var a = 0; a !== age; a += step)
            y -= step * unit * (sizeFor(a) + sizeFor(a + step)) * 0.5 * 1.04;
        return y;
    }
    // Deterministic per-word variation, so a word keeps its look while it rises.
    function noise(g, salt) {
        var x = Math.sin(g * 12.9898 + salt * 78.233) * 43758.5453;
        return x - Math.floor(x);
    }
    function wordColor(age) {
        if (age < 0)
            return secondaryColor; // upcoming words (lens styles)
        // Older words recede by opacity; only a hint of the secondary colour, never muddy.
        return age <= 1 ? textColor : Qt.tint(textColor, Qt.rgba(secondaryColor.r, secondaryColor.g, secondaryColor.b, 0.22));
    }

    ListModel { id: stack }

    function lineStartOf(g) {
        while (g > 0 && g < wordStream.length && wordStream[g - 1].line === wordStream[g].line)
            --g;
        return g;
    }
    // Words of the current line only (the wall clears on every new line); words
    // that drop out get a short exit instead of vanishing.
    function syncStack() {
        var lo = currentWord < 0 ? 0 : Math.max(currentWord - maxAge, lens ? 0 : lineStartOf(currentWord));
        var hi = currentWord < 0 ? -1 : Math.min(currentWord + maxLookahead, wordStream.length - 1);
        for (var i = stack.count - 1; i >= 0; --i) {
            var item = stack.get(i);
            var inside = item.g >= lo && item.g <= hi;
            if (!inside && !item.leaving)
                stack.setProperty(i, "leaving", true);
            else if (inside && item.leaving)
                stack.setProperty(i, "leaving", false);
        }
        for (var w = Math.max(lo, 0); w <= hi; ++w) {
            var present = false;
            for (var j = 0; j < stack.count; ++j)
                if (stack.get(j).g === w) { present = true; break; }
            if (!present)
                stack.append({ g: w, leaving: false });
        }
    }
    function dropLeaving(g) {
        for (var i = stack.count - 1; i >= 0; --i)
            if (stack.get(i).g === g && stack.get(i).leaving)
                stack.remove(i);
    }

    // ---- Screensaver dim: the whole screen goes dark behind the lyrics ----
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.02, 0.02, 0.03, 1)
        opacity: root.immersive ? 0.86 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 700 } }
    }

    // ---- Album art backdrop: the cover, blurred and darkened, fading out at the edges ----
    Item {
        id: artLayer
        objectName: "artBackdrop"
        readonly property bool wanted: root.artBackdrop && root.artSource !== ""
        anchors.fill: parent
        opacity: wanted && (stack.count > 0 || root.showIntro || root.immersive) ? (root.immersive ? 1 : 0.55) : 0
        visible: root.shaderLens && opacity > 0.01 // blur needs the GPU path
        Behavior on opacity { NumberAnimation { duration: 900; easing.type: Easing.InOutQuad } }

        Image {
            id: artImage
            source: root.artSource
            sourceSize: Qt.size(6, 6) // a few pixels of the cover: pure colour, no shapes
            smooth: true
            asynchronous: true
            cache: false
            visible: false
        }
        ShaderEffect {
            anchors.fill: parent
            property variant source: artImage
            property vector2d center: Qt.vector2d(0.5, Math.min(0.6, root.anchorY / Math.max(1, root.height)))
            property real brightness: 0.55
            property real saturation: 1.0
            fragmentShader: "qrc:/dsalyrik/shaders/artglow.frag.qsb"
        }
    }

    // ---- Soft backdrop: a dark radial glow behind the words, faintly tinted ----
    Shape {
        id: backdropShape
        anchors.fill: parent
        opacity: root.backdrop && (stack.count > 0 || root.showIntro) ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 600 } }
        ShapePath {
            strokeWidth: -1
            fillGradient: RadialGradient {
                centerX: root.width / 2
                centerY: root.anchorY - root.unit * 0.8
                centerRadius: Math.max(root.width, root.height) * 0.48
                focalX: centerX
                focalY: centerY
                // Lighter when the album art is showing, so the cover's colour comes through.
                GradientStop { position: 0.0; color: Qt.rgba(0.04 + root.secondaryColor.r * 0.08, 0.03 + root.secondaryColor.g * 0.04, 0.05 + root.secondaryColor.b * 0.06, artLayer.visible ? 0.18 : 0.4) }
                GradientStop { position: 0.55; color: Qt.rgba(0.02, 0.02, 0.03, artLayer.visible ? 0.1 : 0.2) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            startX: 0; startY: 0
            PathLine { x: root.width; y: 0 }
            PathLine { x: root.width; y: root.height }
            PathLine { x: 0; y: root.height }
            PathLine { x: 0; y: 0 }
        }
    }

    // ---- Intro card: cover, title and artist until singing starts ----
    Column {
        id: introCard
        objectName: "introCard"
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.anchorY - height * 0.62
        spacing: root.unit * 0.16
        opacity: root.showIntro ? 1 : 0
        visible: opacity > 0.01
        scale: root.showIntro ? 1 : 0.94
        Behavior on opacity { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }

        Item {
            id: tile
            readonly property real side: root.unit * 2.3
            width: side
            height: side
            anchors.horizontalCenter: parent.horizontalCenter

            // Placeholder when the player supplies no cover.
            Rectangle {
                anchors.fill: parent
                visible: root.artSource === ""
                radius: tile.side * 0.12
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.lighter(root.highlightColor, 1.15) }
                    GradientStop { position: 1.0; color: Qt.darker(root.secondaryColor, 1.3) }
                }
                Text {
                    anchors.centerIn: parent
                    text: "\u266B"
                    color: "#ffffff"
                    opacity: 0.85
                    font.pixelSize: tile.side * 0.45
                }
            }
            Image {
                id: cover
                anchors.fill: parent
                visible: root.artSource !== "" && !root.shaderLens // software path: square corners
                source: root.artSource
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(512, 512)
                asynchronous: true
                cache: false
            }
            Rectangle {
                id: tileMask
                anchors.fill: parent
                radius: tile.side * 0.12
                visible: false
                layer.enabled: true
            }
            MultiEffect {
                anchors.fill: parent
                visible: root.artSource !== "" && root.shaderLens
                source: cover
                maskEnabled: true
                maskSource: tileMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
                autoPaddingEnabled: false
                shadowEnabled: true
                shadowOpacity: 0.6
                shadowBlur: 0.8
                shadowVerticalOffset: root.unit * 0.06
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, root.width * 0.84)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.trackTitle
            color: root.textColor
            font.family: root.family
            font.weight: Font.Black
            font.pixelSize: root.unit * 0.62
            font.letterSpacing: -root.unit * 0.012
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, root.width * 0.84)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            visible: root.trackArtist !== ""
            text: root.trackArtist
            color: Qt.rgba(1, 1, 1, 0.72)
            font.family: root.family
            font.weight: Font.Bold
            font.pixelSize: root.unit * 0.36
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.statusText !== ""
            text: root.statusText
            color: root.secondaryColor
            opacity: 0.9
            font.family: root.family
            font.weight: Font.Bold
            font.pixelSize: root.unit * 0.26
        }
    }

    // ---- Instrumental break: three notes bobbing in the highlight colour ----
    Row {
        id: breakNotes
        objectName: "breakNotes"
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.anchorY + root.unit * 0.95
        spacing: root.unit * 0.32
        opacity: root.instrumental && stack.count > 0 ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 500 } }
        Repeater {
            model: ["\u266A", "\u266B", "\u266A"]
            Text {
                required property string modelData
                required property int index
                text: modelData
                color: root.highlightColor
                font.pixelSize: root.unit * 0.62
                opacity: 0.55 + 0.45 * Math.abs(Math.sin(root.breakPhase + index * 0.9))
                transform: Translate { y: -Math.abs(Math.sin(root.breakPhase + index * 0.9)) * root.unit * 0.22 }
            }
        }
    }

    // ---- Lyrics credit (required by the source's terms) and estimated-timing note ----
    Text {
        id: attributionText
        objectName: "attributionText"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.unit * 0.18
        width: Math.min(implicitWidth, root.width * 0.9)
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        visible: root.attribution !== "" && (stack.count > 0 || root.showIntro)
        text: root.attribution
        color: Qt.rgba(1, 1, 1, 0.55)
        font.family: root.family
        font.weight: Font.DemiBold
        font.pixelSize: Math.max(11, root.unit * 0.16)
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: attributionText.visible ? attributionText.top : parent.bottom
        anchors.bottomMargin: attributionText.visible ? root.unit * 0.05 : root.unit * 0.25
        visible: root.timingNote !== "" && stack.count > 0
        text: root.timingNote
        color: root.secondaryColor
        opacity: 0.75
        font.family: root.family
        font.weight: Font.Bold
        font.pixelSize: root.unit * 0.22
    }

    // ---- Status without a track title ----
    Text {
        anchors.centerIn: parent
        visible: root.currentWord < 0 && root.statusText !== "" && root.trackTitle === ""
        text: root.statusText
        color: root.secondaryColor
        font.family: root.family
        font.weight: Font.Bold
        font.pixelSize: root.unit * 0.32
    }

    // ---- Karaoke: the current line fills left to right as it is sung ----
    // Line index of the newest sung word, and fill progress (0..1) per word of that line.
    readonly property bool karaokeValid: karaoke && currentWord >= 0 && currentWord < wordStream.length
    readonly property int karaokeLine: karaokeValid ? wordStream[currentWord].line : -1
    readonly property int karaokeFirst: {
        if (!karaokeValid)
            return -1;
        var g = currentWord;
        while (g > 0 && wordStream[g - 1].line === karaokeLine)
            --g;
        return g;
    }
    function lineWords(line, from) {
        var out = [];
        for (var g = from; g >= 0 && g < wordStream.length && wordStream[g].line === line; ++g)
            out.push(wordStream[g].text);
        return out;
    }
    readonly property var karaokeWords: karaokeValid && karaokeFirst >= 0 ? lineWords(wordStream[karaokeFirst].line, karaokeFirst) : []
    readonly property var karaokeNext: {
        if (!karaokeValid || karaokeFirst < 0)
            return [];
        var first = karaokeFirst + karaokeWords.length;
        return first < wordStream.length ? lineWords(wordStream[first].line, first) : [];
    }
    readonly property var karaokeProgress: {
        var out = [];
        for (var i = 0; i < karaokeWords.length; ++i) {
            var g = karaokeFirst + i;
            out.push(g < currentWord ? 1 : (g === currentWord ? (wordActive ? sweep : 1) : 0));
        }
        return out;
    }

    FontMetrics {
        id: karaokeMetrics
        font.family: root.family
        font.weight: Font.Bold
        font.pixelSize: root.unit * 0.78
        font.letterSpacing: -root.unit * 0.02
    }
    // Balanced rows, centred: [{x, y, w}] per word.
    function karaokeLayout(words) {
        var maxWidth = width * 0.84, space = karaokeMetrics.advanceWidth(" "), lineHeight = karaokeMetrics.height * 1.08;
        var rows = [[]], widths = [0];
        for (var i = 0; i < words.length; ++i) {
            var w = karaokeMetrics.advanceWidth(words[i]);
            var row = rows.length - 1;
            var needed = widths[row] + (rows[row].length ? space : 0) + w;
            if (rows[row].length && needed > maxWidth) {
                rows.push([]);
                widths.push(0);
                row++;
                needed = w;
            }
            rows[row].push({ i: i, w: w });
            widths[row] = needed;
        }
        var out = [], top = -rows.length * lineHeight / 2;
        for (var r = 0; r < rows.length; ++r) {
            var x = (width - widths[r]) / 2;
            for (var k = 0; k < rows[r].length; ++k) {
                out[rows[r][k].i] = { x: x, y: top + r * lineHeight, w: rows[r][k].w };
                x += rows[r][k].w + space;
            }
        }
        return out;
    }

    Item {
        id: karaokeView
        objectName: "karaokeView"
        anchors.fill: parent
        visible: root.karaoke && root.karaokeLine >= 0
        readonly property var layout: root.karaokeLayout(root.karaokeWords)
        transform: Translate { id: karaokeShift }
        Connections {
            target: root
            function onKaraokeLineChanged() { if (root.karaoke) karaokeIn.restart() }
        }
        ParallelAnimation {
            id: karaokeIn
            NumberAnimation { target: karaokeShift; property: "y"; from: root.unit * 0.5; to: 0; duration: 380; easing.type: Easing.OutCubic }
            NumberAnimation { target: karaokeView; property: "opacity"; from: 0; to: 1; duration: 300; easing.type: Easing.OutQuad }
        }

        Repeater {
            model: root.karaokeWords.length
            Item {
                required property int index
                readonly property var place: karaokeView.layout[index] || { x: 0, y: 0, w: 0 }
                readonly property real progress: root.karaokeProgress[index] || 0
                x: place.x
                y: root.anchorY + place.y
                width: place.w
                height: karaokeMetrics.height
                Text { // not yet sung
                    text: root.karaokeWords[index]
                    font: karaokeMetrics.font
                    color: Qt.rgba(1, 1, 1, 0.38)
                }
                Item { // sung part, wiping in from the left
                    width: parent.width * parent.progress
                    height: parent.height
                    clip: true
                    Text {
                        text: root.karaokeWords[index]
                        font: karaokeMetrics.font
                        color: root.textColor
                    }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.anchorY + (karaokeView.layout.length ? karaokeView.layout[karaokeView.layout.length - 1].y : 0)
               + karaokeMetrics.height * 1.35
            width: Math.min(implicitWidth, root.width * 0.84)
            elide: Text.ElideRight
            text: root.karaokeNext.join(" ")
            color: Qt.rgba(1, 1, 1, 0.3)
            font.family: root.family
            font.weight: Font.Bold
            font.pixelSize: root.unit * 0.42
        }
    }

    // ---- The word stream ----
    Item {
        id: stage
        anchors.fill: parent
        visible: !root.karaoke
        layer.enabled: root.lens && root.shaderLens && stack.count > 0
        layer.textureSize: Qt.size(width * 2, height * 2) // magnify without smearing
        layer.smooth: true
        layer.effect: ShaderEffect {
            fragmentShader: "qrc:/dsalyrik/shaders/lens.frag.qsb"
            property vector2d center: Qt.vector2d(0.5, root.anchorY / Math.max(1, root.height))
            property real radius: 0.45
            property real strength: 0.28
            property real aspect: width / Math.max(1, height)
        }

        Repeater {
            model: stack
            delegate: Item {
                id: wordItem
                required property int g
                required property bool leaving
                readonly property var entry: g < root.wordStream.length ? root.wordStream[g] : { text: "", glyph: "" }
                readonly property int age: root.currentWord - g
                readonly property bool sung: age === 0 && root.wordActive
                readonly property real jitter: (root.noise(g, 1) - 0.5) * root.width * (root.lens ? 0.06 : 0.1)
                readonly property bool slanted: !root.lens && root.noise(g, 2) < 0.45

                width: row.width
                height: row.height
                x: (root.width - width) / 2 + jitter
                y: root.rowCenter(age) - height / 2 + enter * root.unit * 0.5 - (leaving ? root.unit * 0.6 : 0)
                scale: root.sizeFor(age) * (1 - 0.18 * enter)
                opacity: leaving ? 0 : root.opacityFor(age) * (1 - enter) * (root.instrumental ? 0.4 : 1)
                Timer { running: wordItem.leaving; interval: 280; onTriggered: root.dropLeaving(wordItem.g) }
                z: -Math.abs(age)

                // Continuous sway (no Behavior: it follows the phase directly).
                readonly property real swayLean: root.drifting ? 0.28 * Math.sin(root.driftPhase + g * 1.7) : 0
                readonly property real swayX: root.drifting ? Math.sin(root.driftPhase * 2 + g) * root.unit * 0.1 : 0
                readonly property real swayY: root.drifting ? Math.cos(root.driftPhase + g * 2.3) * root.unit * 0.06 : 0

                // Depth lean for the Drift style (0 = flat).
                property real leanX: root.ship ? (root.noise(g, 3) - 0.5) * 1.1 : 0
                property real leanY: root.ship ? -0.18 - 0.1 * Math.max(0, age) : 0
                Behavior on leanX { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
                Behavior on leanY { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }

                property real enter: 1 // 1 = just appeared below its row
                Component.onCompleted: enter = 0
                Behavior on enter { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                Behavior on y { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 320 } }
                Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }

                transform: [
                    // Faux-italic for some words, sheared about the row's middle.
                    Matrix4x4 {
                        readonly property real k: wordItem.slanted ? 0.16 : 0
                        matrix: Qt.matrix4x4(1, -k, 0, k * wordItem.height / 2, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                    },
                    // Drift: a wall of words leaning in depth (true keystone, not just foreshortening),
                    // each at its own angle; the lean grows as a word rises away.
                    Matrix4x4 {
                        readonly property real cx: wordItem.width / 2
                        readonly property real cy: wordItem.height / 2
                        matrix: {
                            const px = (wordItem.leanX + wordItem.swayLean) / Math.max(1, wordItem.width);
                            const py = wordItem.leanY / Math.max(1, wordItem.height);
                            // Translate to the centre, apply a projective w = 1 + px*x + py*y, translate back.
                            return Qt.matrix4x4(1 + px * cx, py * cx, 0, -cx * (px * cx + py * cy),
                                                px * cy, 1 + py * cy, 0, -cy * (px * cx + py * cy),
                                                0, 0, 1, 0,
                                                px, py, 0, 1 - px * cx - py * cy);
                        }
                    },
                    Translate { x: wordItem.swayX; y: wordItem.swayY }
                ]

                Row {
                    id: row
                    spacing: root.unit * 0.2

                    Text {
                        id: glyph
                        visible: root.glyphs && wordItem.entry.glyph !== ""
                        anchors.verticalCenter: parent.verticalCenter
                        // Pops in beside its word with a spring and a small turn.
                        Component.onCompleted: glyphPop.start()
                        ParallelAnimation {
                            id: glyphPop
                            NumberAnimation { target: glyph; property: "scale"; from: 0.2; to: 1; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
                            NumberAnimation { target: glyph; property: "rotation"; from: -24; to: 0; duration: 420; easing.type: Easing.OutBack }
                        }
                        text: wordItem.entry.glyph + "\uFE0E" // text presentation: tintable, not colour emoji
                        color: root.highlightColor
                        font.family: "Noto Emoji" // monochrome, tinted like a symbol
                        font.weight: Font.Bold     // heavier strokes to match the type
                        font.pixelSize: root.unit * 0.86
                    }

                    Item {
                        width: label.width
                        height: label.height

                        // Highlight block that sweeps across the word while it is sung.
                        Rectangle {
                            id: block
                            // Full while sung; it lingers faintly behind the word as it rises, then goes.
                            visible: wordItem.sung || (wordItem.age === 1 && opacity > 0.01)
                            opacity: wordItem.sung ? 1 : (wordItem.age === 1 ? 0.28 : 0)
                            Behavior on opacity { NumberAnimation { duration: 600 } }
                            x: -root.unit * 0.1
                            y: label.height * 0.17
                            width: (label.width + root.unit * 0.2) * (wordItem.sung ? root.sweep : 1)
                            height: label.height * 0.74
                            radius: root.unit * 0.035
                            rotation: -2
                            antialiasing: true
                            color: root.highlightColor
                            // A small spring when the word starts.
                            transformOrigin: Item.Left
                            onVisibleChanged: if (visible) blockPop.restart()
                            NumberAnimation { id: blockPop; target: block; property: "scale"; from: 0.82; to: 1; duration: 260; easing.type: Easing.OutBack }
                        }
                        Text {
                            id: label
                            text: wordItem.entry.text
                            font.family: root.family
                            font.weight: Font.Bold
                            font.pixelSize: root.unit
                            font.letterSpacing: -root.unit * 0.03
                            color: wordItem.sung ? "#ffffff" : root.wordColor(wordItem.age)
                            // A soft vertical gradient: white to peach while sung, the lyric colour otherwise.
                            layer.enabled: root.shaderLens
                            layer.effect: ShaderEffect {
                                fragmentShader: "qrc:/dsalyrik/shaders/gradient.frag.qsb"
                                property color topColor: wordItem.sung ? "#ffffff" : Qt.lighter(root.wordColor(wordItem.age), 1.12)
                                property color bottomColor: wordItem.sung ? Qt.tint("#ffffff", Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.4))
                                                                          : root.wordColor(wordItem.age)
                            }
                        }
                    }
                }
            }
        }
    }
}

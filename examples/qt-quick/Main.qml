import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BookmarkExample

ApplicationWindow {
    width: 760
    height: 560
    visible: true
    title: qsTr("PDF Bookmark — Qt Quick example")

    Bookmarker { id: bookmarker }

    FileDialog {
        id: openDialog
        title: qsTr("Choose a PDF book")
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: bookmarker.analyze(selectedFile)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Button {
                text: qsTr("Open PDF…")
                enabled: !bookmarker.busy
                onClicked: openDialog.open()
            }
            Button {
                text: qsTr("Write bookmarked copy")
                enabled: bookmarker.canWrite && !bookmarker.busy
                onClicked: bookmarker.write()
            }
            Button {
                text: qsTr("Cancel")
                enabled: bookmarker.busy
                onClicked: bookmarker.cancel()
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                running: bookmarker.busy
                visible: bookmarker.busy
                implicitWidth: 32
                implicitHeight: 32
            }
            Label {
                visible: bookmarker.busy
                text: qsTr("%1 pages read").arg(bookmarker.pagesRead)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: bookmarker.status
            wrapMode: Text.Wrap
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: bookmarker.nodes
            ScrollBar.vertical: ScrollBar {}

            delegate: RowLayout {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width - 12

                Item { implicitWidth: 20 * row.modelData.depth }
                TextField {
                    Layout.fillWidth: true
                    text: row.modelData.title
                    // Only the stored plan changes; the list is not rebuilt while typing.
                    onEditingFinished: bookmarker.setTitle(row.index, text)
                }
                Label {
                    Layout.preferredWidth: 70
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("page %1").arg(row.modelData.page)
                }
            }
        }
    }
}

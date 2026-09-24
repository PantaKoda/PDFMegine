// pdfbookmark Qt Quick example.
//   pdfbookmark_qt_example                    the window (Main.qml)
//   pdfbookmark_qt_example --selftest <pdf>   no window: analyze, write "<name> (bookmarked).pdf",
//                                             exit 0 on success (used by CI), 3 if no plan, 1 on error
#include "bookmarker.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTextStream>

#include <cstring>

namespace {

int selftest() {
    // QCoreApplication::arguments() is Unicode-safe on Windows.
    const QStringList args = QCoreApplication::arguments();
    QTextStream out(stdout);
    Bookmarker bookmarker;
    QObject::connect(&bookmarker, &Bookmarker::statusChanged, [&] {
        out << bookmarker.status() << Qt::endl;
    });
    QObject::connect(&bookmarker, &Bookmarker::analysisFinished, [&](bool planReady) {
        if (planReady)
            bookmarker.write();
        else
            QCoreApplication::exit(3);
    });
    QObject::connect(&bookmarker, &Bookmarker::writeFinished,
                     [](bool ok) { QCoreApplication::exit(ok ? 0 : 1); });
    bookmarker.analyzePath(args.value(2));
    return QCoreApplication::exec();
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 3 && std::strcmp(argv[1], "--selftest") == 0) {
        QCoreApplication app(argc, argv);
        return selftest();
    }
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("BookmarkExample", "Main");
    return QGuiApplication::exec();
}

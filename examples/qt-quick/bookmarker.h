// Bookmarker: the bridge between QML and the pdfbookmark library.
//
// The library calls block for seconds to minutes, so they run on a private
// one-thread pool. Results and progress come back to the GUI thread through
// queued invocations. Only this class touches the library.
#pragma once

#include <pdfbookmark/pdfbookmark.hpp>

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>

class Bookmarker : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int pagesRead READ pagesRead NOTIFY pagesReadChanged)
    // One entry per bookmark: { title, page (1-based, for display), depth }.
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(bool canWrite READ canWrite NOTIFY nodesChanged)

public:
    explicit Bookmarker(QObject* parent = nullptr);
    ~Bookmarker() override;

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int pagesRead() const { return m_pagesRead; }
    QVariantList nodes() const { return m_nodes; }
    bool canWrite() const { return m_plan.has_value(); }

    Q_INVOKABLE void analyze(const QUrl& file);       // From a FileDialog.
    Q_INVOKABLE void analyzePath(const QString& path);
    Q_INVOKABLE void setTitle(int row, const QString& title);
    Q_INVOKABLE void write();                          // "<name> (bookmarked).pdf"
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void statusChanged();
    void pagesReadChanged();
    void nodesChanged();
    void analysisFinished(bool planReady);
    void writeFinished(bool ok);

private:
    void setBusy(bool busy);
    void setStatus(const QString& status);
    void setPagesRead(int pages);
    void onAnalyzed(const pdfbookmark::Result<pdfbookmark::AnalysisReport>& report);
    void onWritten(const pdfbookmark::Result<pdfbookmark::WriteResult>& result);
    void rebuildNodes();

    QThreadPool m_pool;  // One worker: one library operation at a time.
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    std::filesystem::path m_input;
    std::optional<pdfbookmark::BookmarkPlan> m_plan;  // Present only when writable.
    QVariantList m_nodes;
    QString m_status;
    int m_pagesRead = 0;
    bool m_busy = false;
};

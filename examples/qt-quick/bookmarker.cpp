#include "bookmarker.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include <string>
#include <unordered_map>

namespace {

QString fromUtf8(const std::string& text) { return QString::fromStdString(text); }  // UTF-8

// A new file next to the input that does not exist yet (the library refuses
// to overwrite anything by default, and never writes to the input).
std::filesystem::path outputFor(const std::filesystem::path& input) {
    const auto folder = input.parent_path();
    const auto stem = input.stem().wstring();
    auto candidate = folder / (stem + L" (bookmarked).pdf");
    for (int n = 2; std::filesystem::exists(candidate); ++n)
        candidate = folder / (stem + L" (bookmarked " + std::to_wstring(n) + L").pdf");
    return candidate;
}

}  // namespace

Bookmarker::Bookmarker(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    setStatus(tr("Open a PDF book to find its table of contents."));
}

Bookmarker::~Bookmarker() {
    // The worker captures `this`: stop it and wait before the object goes away.
    // Queued results addressed to a destroyed object are discarded by Qt.
    m_cancel->store(true);
    m_pool.waitForDone();
}

void Bookmarker::analyze(const QUrl& file) { analyzePath(file.toLocalFile()); }

void Bookmarker::analyzePath(const QString& path) {
    if (m_busy || path.isEmpty()) return;
    m_input = std::filesystem::path(path.toStdWString());  // Wide: keeps non-ASCII names.
    m_plan.reset();
    rebuildNodes();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    setPagesRead(0);
    setBusy(true);
    setStatus(tr("Looking for the table of contents in %1…").arg(QFileInfo(path).fileName()));

    m_pool.start([this, input = m_input, cancel = m_cancel] {
        pdfbookmark::AnalysisOptions options;
        options.models = pdfbookmark::find_models();  // OCR for scanned pages, if deployed.
        options.plan.allow_partial = true;             // Leave out entries it cannot place.
        options.plan.title_style = pdfbookmark::PlanPolicy::TitleStyle::Chapter;
        auto report = pdfbookmark::analyze(
            input, options, pdfbookmark::RunControl{cancel.get()},
            [this](const pdfbookmark::AnalysisProgress& progress) {
                // Worker thread: hand the value to the GUI thread.
                const int pages = static_cast<int>(progress.pages_acquired);
                QMetaObject::invokeMethod(this, [this, pages] { setPagesRead(pages); },
                                          Qt::QueuedConnection);
            });
        auto shared = std::make_shared<decltype(report)>(std::move(report));
        QMetaObject::invokeMethod(this, [this, shared] { onAnalyzed(*shared); },
                                  Qt::QueuedConnection);
    });
}

void Bookmarker::onAnalyzed(const pdfbookmark::Result<pdfbookmark::AnalysisReport>& result) {
    setBusy(false);
    if (!result) {  // Errors are values: unreadable file, encrypted PDF, cancelled, ...
        setStatus(tr("Could not analyze the file: %1").arg(fromUtf8(result.error().message)));
        emit analysisFinished(false);
        return;
    }
    const auto& report = result.value();
    if (report.plan.ready && report.plan.plan) {
        m_plan = *report.plan.plan;
        rebuildNodes();
        setStatus(tr("Found %n bookmark(s). Edit titles if you like, then write the new PDF.", "",
                     static_cast<int>(m_plan->nodes.size())));
        emit analysisFinished(true);
        return;
    }
    // Not an error: e.g. no table of contents in the searched pages. The
    // blockers explain why in plain language.
    QStringList reasons;
    for (const auto& blocker : report.plan.blockers) reasons << fromUtf8(blocker);
    setStatus(tr("No bookmarks to write (%1). %2")
                  .arg(QString::fromLatin1(pdfbookmark::outcome_name(report.outcome)),
                       reasons.join(QStringLiteral(" "))));
    emit analysisFinished(false);
}

void Bookmarker::setTitle(int row, const QString& title) {
    if (!m_plan || row < 0 || row >= static_cast<int>(m_plan->nodes.size())) return;
    m_plan->nodes[static_cast<std::size_t>(row)].title = title.trimmed().toStdString();  // UTF-8
}

void Bookmarker::write() {
    if (m_busy || !m_plan) return;
    const auto validation = pdfbookmark::validate_plan(*m_plan);  // e.g. an emptied title
    if (!validation.valid) {
        setStatus(tr("The bookmarks are not valid: %1")
                      .arg(fromUtf8(validation.issues.front().message)));
        return;
    }
    setBusy(true);
    setStatus(tr("Writing…"));
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_pool.start([this, input = m_input, plan = *m_plan, cancel = m_cancel] {
        auto written = pdfbookmark::apply(input, outputFor(input), plan, pdfbookmark::ApplyOptions{},
                                          pdfbookmark::RunControl{cancel.get()});
        auto shared = std::make_shared<decltype(written)>(std::move(written));
        QMetaObject::invokeMethod(this, [this, shared] { onWritten(*shared); },
                                  Qt::QueuedConnection);
    });
}

void Bookmarker::onWritten(const pdfbookmark::Result<pdfbookmark::WriteResult>& result) {
    setBusy(false);
    if (!result) {
        setStatus(tr("Could not write the PDF: %1").arg(fromUtf8(result.error().message)));
        emit writeFinished(false);
        return;
    }
    setStatus(tr("Wrote %1 with %n bookmark(s). The original file is unchanged.", "",
                 static_cast<int>(result.value().verification.outline_items))
                  .arg(QString::fromStdWString(result.value().output.filename().wstring())));
    emit writeFinished(true);
}

void Bookmarker::cancel() { m_cancel->store(true); }

void Bookmarker::rebuildNodes() {
    m_nodes.clear();
    if (m_plan) {
        std::unordered_map<std::string, int> depth;  // Parents precede children in plans.
        for (const auto& node : m_plan->nodes) {
            const int d = node.parent_id && depth.count(*node.parent_id) ? depth[*node.parent_id] + 1 : 0;
            depth[node.id] = d;
            m_nodes.append(QVariantMap{
                {QStringLiteral("title"), fromUtf8(node.title)},
                {QStringLiteral("page"), static_cast<int>(node.destination.pdf_page_index) + 1},
                {QStringLiteral("depth"), d}});
        }
    }
    emit nodesChanged();
}

void Bookmarker::setBusy(bool busy) {
    if (m_busy == busy) return;
    m_busy = busy;
    emit busyChanged();
}

void Bookmarker::setStatus(const QString& status) {
    m_status = status;
    emit statusChanged();
}

void Bookmarker::setPagesRead(int pages) {
    if (m_pagesRead == pages) return;
    m_pagesRead = pages;
    emit pagesReadChanged();
}

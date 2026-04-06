#include "library/export/libraryexporter.h"

#include <QProgressDialog>

#include "library/export/engineprimeexportjob.h"
#include "library/export/engineprimeexportrequest.h"
#include "library/export/rekordboxexportjob.h"
#include "library/export/rekordboxexportrequest.h"
#include "moc_libraryexporter.cpp"
#include "util/parented_ptr.h"

namespace mixxx {

LibraryExporter::LibraryExporter(QWidget* parent,
        UserSettingsPointer pConfig,
        TrackCollectionManager* pTrackCollectionManager)
        : QWidget{parent},
          m_pConfig{std::move(pConfig)},
          m_pTrackCollectionManager{pTrackCollectionManager} {
}

void LibraryExporter::requestExportWithOptionalInitialSelection(
        std::optional<CrateId> initialSelectedCrateId,
        std::optional<int> initialSelectedPlaylistId) {
    if (!m_pDialog) {
        m_pDialog = make_parented<DlgLibraryExport>(
                this, m_pConfig, m_pTrackCollectionManager);
        connect(m_pDialog.get(),
                &DlgLibraryExport::startEnginePrimeExport,
                this,
                &LibraryExporter::beginEnginePrimeExport);
        connect(m_pDialog.get(),
                &DlgLibraryExport::startRekordboxExport,
                this,
                &LibraryExporter::beginRekordboxExport);
    } else {
        m_pDialog->show();
        m_pDialog->raise();
        m_pDialog->setWindowState(
                (m_pDialog->windowState() & ~Qt::WindowMinimized) |
                Qt::WindowActive);
    }

    m_pDialog->refresh();
    m_pDialog->setInitialSelection(initialSelectedCrateId, initialSelectedPlaylistId);
}

void LibraryExporter::beginEnginePrimeExport(
        QSharedPointer<EnginePrimeExportRequest> pRequest) {
    // Note that the job will run in a background thread.
    auto pJobThread = make_parented<EnginePrimeExportJob>(
            this,
            m_pTrackCollectionManager,
            pRequest);
    connect(pJobThread, &EnginePrimeExportJob::finished, pJobThread, &QObject::deleteLater);

    // TODO(XXX) The conclusion of the export (succeeded/failed) could be better
    //  presented as a user notification, rather than using a message box, if
    //  such functionality is added to Mixxx.
    connect(pJobThread,
            &EnginePrimeExportJob::completed,
            this,
            [](int numTracks, int numCrates, int numPlaylists) {
                QMessageBox::information(nullptr,
                        tr("Export Completed"),
                        QString{tr("Exported %1 track(s), %2 crate(s), and %3 playlist(s).")}
                                .arg(numTracks)
                                .arg(numCrates)
                                .arg(numPlaylists));
            });
    connect(pJobThread,
            &EnginePrimeExportJob::failed,
            this,
            [](const QString& message) {
                QMessageBox::critical(nullptr, tr("Export Failed"), message);
            });

    // Construct a dialog to monitor job progress and offer cancellation.
    auto pProgressDlg = make_parented<QProgressDialog>(this);
    pProgressDlg->setLabelText(tr("Exporting to Engine DJ..."));
    pProgressDlg->setMinimumDuration(0);
    connect(pJobThread,
            &EnginePrimeExportJob::jobMaximum,
            pProgressDlg,
            &QProgressDialog::setMaximum);
    connect(pJobThread,
            &EnginePrimeExportJob::jobProgress,
            pProgressDlg,
            &QProgressDialog::setValue);
    connect(pJobThread, &EnginePrimeExportJob::finished, pProgressDlg, &QObject::deleteLater);
    connect(pProgressDlg,
            &QProgressDialog::canceled,
            pJobThread,
            &EnginePrimeExportJob::slotCancel);

    pJobThread->start();
}

void LibraryExporter::beginRekordboxExport(
        QSharedPointer<RekordboxExportRequest> pRequest) {
    auto pJobThread = make_parented<RekordboxExportJob>(
            this,
            m_pTrackCollectionManager,
            pRequest);
    connect(pJobThread, &RekordboxExportJob::finished, pJobThread, &QObject::deleteLater);

    connect(pJobThread,
            &RekordboxExportJob::completed,
            this,
            [](int numTracks) {
                QMessageBox::information(nullptr,
                        tr("Rekordbox Export Completed"),
                        QString{tr("Exported %1 track(s) to Pioneer USB format.")}.arg(numTracks));
            });
    connect(pJobThread,
            &RekordboxExportJob::failed,
            this,
            [](const QString& message) {
                QMessageBox::critical(nullptr, tr("Rekordbox Export Failed"), message);
            });

    auto pProgressDlg = make_parented<QProgressDialog>(this);
    //: "Pioneer Rekordbox" must not be translated
    pProgressDlg->setLabelText(tr("Exporting to Pioneer Rekordbox USB..."));
    pProgressDlg->setMinimumDuration(0);
    connect(pJobThread,
            &RekordboxExportJob::jobMaximum,
            pProgressDlg,
            &QProgressDialog::setMaximum);
    connect(pJobThread,
            &RekordboxExportJob::jobProgress,
            pProgressDlg,
            &QProgressDialog::setValue);
    connect(pJobThread, &RekordboxExportJob::finished, pProgressDlg, &QObject::deleteLater);
    connect(pProgressDlg,
            &QProgressDialog::canceled,
            pJobThread,
            &RekordboxExportJob::slotCancel);

    pJobThread->start();
}

} // namespace mixxx

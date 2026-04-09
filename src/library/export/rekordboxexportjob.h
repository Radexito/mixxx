#pragma once

#include <QAtomicInteger>
#include <QSet>
#include <QSharedPointer>
#include <QThread>

#include "library/trackset/crate/crateid.h"
#include "track/track_decl.h"
#include "track/trackid.h"

class TrackCollectionManager;

namespace mixxx {

struct RekordboxExportRequest;

/// RekordboxExportJob exports the Mixxx library to a Pioneer CDJ-compatible
/// USB drive layout:
///
///   USB_ROOT/
///   ├── PIONEER/
///   │   ├── USBANLZ/P{3hex}/{8hex}/
///   │   │   ├── ANLZ0000.DAT   (beatgrid + overview waveform)
///   │   │   ├── ANLZ0000.EXT   (colour waveform + extended beatgrid)
///   │   │   └── ANLZ0000.2EX   (CDJ-3000 RGB waveform)
///   │   ├── MYSETTING.DAT
///   │   ├── MYSETTING2.DAT
///   │   └── DEVSETTING.DAT
///   └── export.pdb             (DeviceSQL binary track database)
///
/// The job runs on a background thread.
class RekordboxExportJob : public QThread {
    Q_OBJECT
  public:
    RekordboxExportJob(
            QObject* parent,
            TrackCollectionManager* pTrackCollectionManager,
            QSharedPointer<RekordboxExportRequest> pRequest);
    ~RekordboxExportJob() override;

    void run() override;

  signals:
    void jobMaximum(int maximum);
    void jobProgress(int progress);
    void completed(int numTracksExported);
    void failed(const QString& message);

  public slots:
    void slotCancel();

  private slots:
    void slotLoadTracksFromDb();

  private:
    /// Compute the Pioneer path-hash folder name for a USB-relative track path.
    /// Returns e.g. "P036/00006A74".
    static QString computeAnlzFolder(const QString& usbRelativePath);

    /// Write PIONEER/MYSETTING.DAT, MYSETTING2.DAT, DEVSETTING.DAT.
    bool writeSettingsFiles(const QString& pioneerDir);

    /// Write the PMAI-container ANLZ analysis files for a single track.
    bool writeAnlzFiles(
            const QString& anlzDir,
            const QString& usbRelativePath,
            TrackPointer pTrack,
            qint64 audioFileSizeBytes);

    /// Build and write export.pdb at the USB root.
    bool writePdb(
            const QString& pdbPath,
            const QList<TrackPointer>& tracks,
            const QStringList& usbRelativePaths,
            const QStringList& anlzFolderPaths);

    TrackCollectionManager* m_pTrackCollectionManager;
    QSharedPointer<RekordboxExportRequest> m_pRequest;
    QAtomicInteger<int> m_cancelRequested;

    QList<TrackPointer> m_tracks;
    QStringList m_usbPaths;
};

} // namespace mixxx

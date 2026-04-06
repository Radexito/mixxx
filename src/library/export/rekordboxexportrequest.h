#pragma once

#include <QDir>
#include <QSet>

#include "library/trackset/crate/crateid.h"

namespace mixxx {

/// A request to export the Mixxx library to a Pioneer CDJ-compatible USB drive
/// (Rekordbox format).  The output structure matches the binary layout expected
/// by CDJ-3000 / CDJ-NXS2 / CDJ-900 hardware running Rekordbox 6+.
struct RekordboxExportRequest {
    /// Root directory of the target USB drive (or any writable directory).
    /// The exporter will create the PIONEER/ sub-tree and export.pdb here.
    QDir usbRootDir;

    /// Directory where source audio files will be copied relative to
    /// usbRootDir.  Typically "music" or left empty to copy alongside
    /// PIONEER/.
    QString musicSubDir;

    /// Set of crates to export.  An empty set together with an empty
    /// playlistIdsToExport implies the whole library is exported.
    QSet<CrateId> crateIdsToExport;

    /// Set of playlists to export.
    QSet<int> playlistIdsToExport;
};

} // namespace mixxx

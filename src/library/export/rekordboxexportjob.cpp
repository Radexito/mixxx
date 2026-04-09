#include "library/export/rekordboxexportjob.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QtEndian>

#include "audio/frame.h"
#include "library/export/rekordboxexportrequest.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/crate/crate.h"
#include "library/trackset/crate/cratestorage.h"
#include "moc_rekordboxexportjob.cpp"
#include "track/track.h"
#include "waveform/waveformfactory.h"

namespace mixxx {

namespace {

// ---------------------------------------------------------------------------
// Low-level binary helpers
// ---------------------------------------------------------------------------

/// Write a big-endian uint32 into a QDataStream.
void writeU32BE(QDataStream& ds, quint32 v) {
    ds << v;
}

/// Write a big-endian uint16 into a QDataStream.
void writeU16BE(QDataStream& ds, quint16 v) {
    ds << v;
}

// ---------------------------------------------------------------------------
// Pioneer path-hash — "getFolderName"
//
// Algorithm (from protocol_rekordbox.md / DjManager anlzWriter.js):
//
//   hash  = 0  (uint32, wraps)
//   for each char c in normalised path:
//       hash = (hash * 0x34F5501D + c * 0x93B6) & 0xFFFFFFFF
//   part2 = hash % 0x30D43          → 8-hex component
//   part1 = (hash / 0x30D43) & 0xFFF → 3-hex component
//
// Note: normalised path has forward-slashes and a leading '/'.
// ---------------------------------------------------------------------------

QString computeAnlzFolder(const QString& usbRelativePath) {
    QString normalised = usbRelativePath;
    normalised.replace('\\', '/');
    if (!normalised.startsWith('/')) {
        normalised.prepend('/');
    }

    quint32 hash = 0;
    for (const QChar ch : normalised) {
        quint32 c = static_cast<quint32>(ch.unicode());
        hash = (hash * 0x34F5501Du) + (c * 0x93B6u);
    }

    quint32 part2 = hash % 0x30D43u;
    quint32 part1 = (hash / 0x30D43u) & 0xFFFu;

    return QStringLiteral("P") +
            QString::number(part1, 16).rightJustified(3, '0').toUpper() +
            QStringLiteral("/") +
            QString::number(part2, 16).rightJustified(8, '0').toUpper();
}

// ---------------------------------------------------------------------------
// PMAI container helpers
// ---------------------------------------------------------------------------

/// Write the 28-byte PMAI file header.
void writePmaiFileHeader(QDataStream& ds, quint32 totalFileSize) {
    ds.writeRawData("PMAI", 4);
    writeU32BE(ds, 0x0000001Cu); // len_header = 28
    writeU32BE(ds, totalFileSize);
    writeU32BE(ds, 0x00000001u);
    writeU32BE(ds, 0x00010000u);
    writeU32BE(ds, 0x00010000u);
    writeU32BE(ds, 0x00000000u);
}

/// Write a section envelope (12-byte common header).
/// lenHeader: offset from section start to payload (section-type specific).
/// lenTag:    total section size including this header.
void writeSectionHeader(QDataStream& ds, const char* tag, quint32 lenHeader, quint32 lenTag) {
    ds.writeRawData(tag, 4);
    writeU32BE(ds, lenHeader);
    writeU32BE(ds, lenTag);
}

// ---------------------------------------------------------------------------
// Section writers
// ---------------------------------------------------------------------------

/// PPTH — file path, UTF-16BE null-terminated.
/// lenHeader = 16, lenTag = 16 + lenPath.
QByteArray buildPpth(const QString& usbRelativePath) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    const QString& p = usbRelativePath;
    // UTF-16BE encoding including null terminator
    QByteArray utf16;
    for (const QChar ch : p) {
        utf16.append(static_cast<char>((ch.unicode() >> 8) & 0xFF));
        utf16.append(static_cast<char>(ch.unicode() & 0xFF));
    }
    // null terminator
    utf16.append('\0');
    utf16.append('\0');

    quint32 lenPath = static_cast<quint32>(utf16.size());
    quint32 lenHeader = 16;
    quint32 lenTag = lenHeader + lenPath;

    writeSectionHeader(ds, "PPTH", lenHeader, lenTag);
    writeU32BE(ds, lenPath); // offset 12
    ds.writeRawData(utf16.constData(), utf16.size());
    return out;
}

/// PVBR — VBR seek index (mandatory, 400 entries).
/// lenHeader = 16, lenTag = 16 + 4 + 400*4 = 1620.
QByteArray buildPvbr(qint64 fileSizeBytes) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 kEntries = 400;
    constexpr quint32 lenHeader = 16;
    constexpr quint32 lenTag = lenHeader + 4 + kEntries * 4;

    writeSectionHeader(ds, "PVBR", lenHeader, lenTag);
    writeU32BE(ds, 0u); // unknown field at offset 12

    // Linear approximation: entry[i] = i * fileSize / 400
    for (quint32 i = 0; i < kEntries; ++i) {
        quint32 offset = static_cast<quint32>(
                (static_cast<qint64>(i) * fileSizeBytes) / kEntries);
        writeU32BE(ds, offset);
    }
    return out;
}

struct BeatEntry {
    quint16 beatNumber; // 1-4
    quint16 tempo;      // BPM * 100
    quint32 timeMs;
};

/// PQTZ — beat grid.
/// lenHeader = 24, lenTag = 24 + beatCount * 8.
QByteArray buildPqtz(const QList<BeatEntry>& beats) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    quint32 beatCount = static_cast<quint32>(beats.size());
    constexpr quint32 lenHeader = 24;
    quint32 lenTag = lenHeader + beatCount * 8;

    writeSectionHeader(ds, "PQTZ", lenHeader, lenTag);
    writeU32BE(ds, 0x00000000u);
    writeU32BE(ds, 0x00080000u);
    writeU32BE(ds, beatCount);

    for (const auto& b : beats) {
        writeU16BE(ds, b.beatNumber);
        writeU16BE(ds, b.tempo);
        writeU32BE(ds, b.timeMs);
    }
    return out;
}

/// PWAV — monochrome 400-byte overview waveform.
/// lenHeader = 20, lenTag = 20 + 400.
QByteArray buildPwav(const QByteArray& waveformData400) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 lenHeader = 20;
    constexpr quint32 lenTag = lenHeader + 400;

    writeSectionHeader(ds, "PWAV", lenHeader, lenTag);
    writeU32BE(ds, 400u);
    writeU32BE(ds, 0x00010000u);

    QByteArray data = waveformData400;
    data.resize(400, '\0');
    ds.writeRawData(data.constData(), 400);
    return out;
}

/// PWV2 — tiny monochrome preview for CDJ-900 (100 bytes).
/// lenHeader = 20, lenTag = 20 + 100.
QByteArray buildPwv2(const QByteArray& waveformData100) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 lenHeader = 20;
    constexpr quint32 lenTag = lenHeader + 100;

    writeSectionHeader(ds, "PWV2", lenHeader, lenTag);
    writeU32BE(ds, 100u);
    writeU32BE(ds, 0x00010000u);

    QByteArray data = waveformData100;
    data.resize(100, '\0');
    ds.writeRawData(data.constData(), 100);
    return out;
}

/// PCOB — empty cue object stub (24 bytes).
/// flag=1 for the first, flag=0 for the second.
QByteArray buildPcob(bool isFirst) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 lenHeader = 24;
    constexpr quint32 lenTag = 24;

    writeSectionHeader(ds, "PCOB", lenHeader, lenTag);
    writeU32BE(ds, isFirst ? 1u : 0u); // flag
    writeU32BE(ds, 0xFFFFFFFFu);       // value
    writeU32BE(ds, 0x00000000u);
    return out;
}

/// PWV3 — monochrome scroll waveform for EXT.
/// lenHeader = 24, lenTag = 24 + numEntries.
QByteArray buildPwv3(const QByteArray& scrollData) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    quint32 numEntries = static_cast<quint32>(scrollData.size());
    constexpr quint32 lenHeader = 24;
    quint32 lenTag = lenHeader + numEntries;

    writeSectionHeader(ds, "PWV3", lenHeader, lenTag);
    writeU32BE(ds, 1u);            // bytes per entry
    writeU32BE(ds, numEntries);
    writeU32BE(ds, 0x00960000u);

    ds.writeRawData(scrollData.constData(), scrollData.size());
    return out;
}

/// PCO2 — extended cue stub (20 bytes).
QByteArray buildPco2(bool isFirst) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 lenHeader = 20;
    constexpr quint32 lenTag = 20;

    writeSectionHeader(ds, "PCO2", lenHeader, lenTag);
    writeU32BE(ds, isFirst ? 1u : 0u); // flag
    writeU32BE(ds, 0x00000000u);
    return out;
}

/// PQT2 — extended beat grid for Rekordbox 6 (EXT file).
/// entry_count MUST be > 0.  Body: one u16BE per beat = (timeMs % 1000).
QByteArray buildPqt2(const QList<BeatEntry>& beats) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    quint32 entryCount = static_cast<quint32>(beats.size());
    // Ensure entry_count > 0 as required by Rekordbox 6
    if (entryCount == 0) {
        // Write a synthetic single entry at t=0 to satisfy the requirement.
        // This prevents Rekordbox 6 from displaying a flat beat grid.
        entryCount = 1;
    }
    constexpr quint32 lenHeader = 56;
    quint32 lenTag = lenHeader + entryCount * 2;

    writeSectionHeader(ds, "PQT2", lenHeader, lenTag);
    writeU32BE(ds, 0x00000000u);
    writeU32BE(ds, 0x01000002u);
    writeU32BE(ds, 0x00000000u);

    if (!beats.isEmpty()) {
        // First beat
        writeU16BE(ds, beats.first().beatNumber);
        writeU16BE(ds, beats.first().tempo);
        writeU32BE(ds, beats.first().timeMs);
        // Last beat
        writeU16BE(ds, beats.last().beatNumber);
        writeU16BE(ds, beats.last().tempo);
        writeU32BE(ds, beats.last().timeMs);
    } else {
        // Synthetic placeholder
        writeU16BE(ds, 1u);
        writeU16BE(ds, 12000u); // 120.00 BPM placeholder
        writeU32BE(ds, 0u);
        writeU16BE(ds, 1u);
        writeU16BE(ds, 12000u);
        writeU32BE(ds, 0u);
    }

    writeU32BE(ds, entryCount);
    writeU32BE(ds, 0x00000000u);
    // 8 bytes reserved zeros
    writeU32BE(ds, 0u);
    writeU32BE(ds, 0u);

    // Body: one u16BE per beat = timeMs % 1000
    if (!beats.isEmpty()) {
        for (const auto& b : beats) {
            writeU16BE(ds, static_cast<quint16>(b.timeMs % 1000u));
        }
    } else {
        writeU16BE(ds, 0u);
    }

    return out;
}

/// PWV5 — colour scroll waveform for NXS2/CDJ-3000 (EXT file).
QByteArray buildPwv5(const QList<quint16>& columns) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    quint32 numEntries = static_cast<quint32>(columns.size());
    constexpr quint32 lenHeader = 24;
    quint32 lenTag = lenHeader + numEntries * 2;

    writeSectionHeader(ds, "PWV5", lenHeader, lenTag);
    writeU32BE(ds, 2u);            // bytes per entry
    writeU32BE(ds, numEntries);
    writeU32BE(ds, 0x00960305u);

    for (quint16 col : columns) {
        writeU16BE(ds, col);
    }
    return out;
}

/// PWV4 — colour preview waveform for CDJ-NXS2 touch strip (EXT file).
/// Always 1200 columns × 6 bytes.
QByteArray buildPwv4(const QByteArray& data7200) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 kCols = 1200;
    constexpr quint32 lenHeader = 24;
    constexpr quint32 lenTag = lenHeader + kCols * 6;

    writeSectionHeader(ds, "PWV4", lenHeader, lenTag);
    writeU32BE(ds, 6u);
    writeU32BE(ds, kCols);
    writeU32BE(ds, 0x00000000u);

    QByteArray d = data7200;
    d.resize(kCols * 6, '\0');
    ds.writeRawData(d.constData(), kCols * 6);
    return out;
}

/// PWV7 — RGB scroll waveform (CDJ-3000, 2EX file).
QByteArray buildPwv7(const QByteArray& rgbData) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    quint32 numCols = static_cast<quint32>(rgbData.size() / 3);
    constexpr quint32 lenHeader = 24;
    quint32 lenTag = lenHeader + numCols * 3;

    writeSectionHeader(ds, "PWV7", lenHeader, lenTag);
    writeU32BE(ds, 3u);
    writeU32BE(ds, numCols);
    writeU32BE(ds, 0x00960000u);

    ds.writeRawData(rgbData.constData(), numCols * 3);
    return out;
}

/// PWV6 — RGB overview waveform (CDJ-3000, 2EX file). Always 1200 columns.
QByteArray buildPwv6(const QByteArray& rgbData3600) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 kCols = 1200;
    constexpr quint32 lenHeader = 20;
    constexpr quint32 lenTag = lenHeader + kCols * 3;

    writeSectionHeader(ds, "PWV6", lenHeader, lenTag);
    writeU32BE(ds, 3u);
    writeU32BE(ds, kCols);

    QByteArray d = rgbData3600;
    d.resize(kCols * 3, '\0');
    ds.writeRawData(d.constData(), kCols * 3);
    return out;
}

/// PWVC — colour waveform calibration (2EX file).
QByteArray buildPwvc() {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);

    constexpr quint32 lenHeader = 14;
    constexpr quint32 lenTag = 20;

    writeSectionHeader(ds, "PWVC", lenHeader, lenTag);
    writeU16BE(ds, 0x0000u); // padding
    writeU16BE(ds, 0x0064u); // calibration 1 = 100
    writeU16BE(ds, 0x0068u); // calibration 2 = 104
    writeU16BE(ds, 0x00C5u); // calibration 3 = 197
    return out;
}

// ---------------------------------------------------------------------------
// Build beat entries from a Mixxx track
// ---------------------------------------------------------------------------

QList<BeatEntry> buildBeatEntries(TrackPointer pTrack) {
    QList<BeatEntry> entries;
    if (!pTrack) {
        return entries;
    }

    const auto pBeats = pTrack->getBeats();
    if (!pBeats) {
        return entries;
    }

    const double bpm = pTrack->getBpm();
    const quint16 tempoRaw = static_cast<quint16>(qRound(bpm * 100.0));
    const double sampleRate = pTrack->getSampleRate();
    const double durationFrames = pTrack->getDuration() * sampleRate;

    quint16 beatNum = 1;
    for (auto it = pBeats->iteratorFrom(audio::kStartFramePos);
            it != pBeats->cend();
            ++it) {
        const audio::FramePos pos = *it;
        if (pos.value() > durationFrames) {
            break;
        }
        const double timeMs = (pos.value() / sampleRate) * 1000.0;
        BeatEntry e;
        e.beatNumber = beatNum;
        e.tempo = tempoRaw;
        e.timeMs = static_cast<quint32>(qRound(timeMs));
        entries.append(e);
        beatNum = static_cast<quint16>((beatNum % 4) + 1);
    }
    return entries;
}

// ---------------------------------------------------------------------------
// Waveform helpers — derive compact waveform from Mixxx waveform data
// ---------------------------------------------------------------------------

/// Downsample a Mixxx waveform to `targetCols` monochrome bytes.
/// Each output byte: (whiteness[0-7] << 5) | height[0-31].
QByteArray downsampleMono(TrackPointer pTrack, int targetCols) {
    QByteArray result(targetCols, '\0');
    if (!pTrack) {
        return result;
    }

    ConstWaveformPointer pWaveform = pTrack->getWaveformSummary();
    if (!pWaveform || pWaveform->getDataSize() == 0) {
        return result;
    }

    const int srcLen = pWaveform->getDataSize();
    for (int i = 0; i < targetCols; ++i) {
        int srcIdx = (i * srcLen) / targetCols;
        // Mixxx waveform stores [left_low, left_mid, left_high, right_low...] per frame
        // Use channel 0 (left) all, take the max of low/mid/high as the height.
        const WaveformData& d = pWaveform->get(srcIdx);
        int height = qMax((int)d.filtered.low, qMax((int)d.filtered.mid, (int)d.filtered.high));
        height = qMin(height, 31);
        result[i] = static_cast<char>(height & 0x1F);
    }
    return result;
}

/// Build PWV5 colour scroll columns from Mixxx waveform.
QList<quint16> buildColourScrollColumns(TrackPointer pTrack, int numCols) {
    QList<quint16> cols(numCols, 0u);
    if (!pTrack) {
        return cols;
    }

    ConstWaveformPointer pWaveform = pTrack->getWaveform();
    if (!pWaveform || pWaveform->getDataSize() == 0) {
        return cols;
    }

    const int srcLen = pWaveform->getDataSize();
    for (int i = 0; i < numCols; ++i) {
        int srcIdx = (i * srcLen) / numCols;
        const WaveformData& d = pWaveform->get(srcIdx);

        quint16 red   = qMin((int)d.filtered.high, 7); // treble → red
        quint16 green = qMin((int)d.filtered.mid,  7); // mid    → green
        quint16 blue  = qMin((int)d.filtered.low,  7); // bass   → blue
        quint16 height = static_cast<quint16>(qMin(
                qMax((int)d.filtered.low, qMax((int)d.filtered.mid, (int)d.filtered.high)),
                31));

        quint16 col = static_cast<quint16>(
                (red << 13) | (green << 10) | (blue << 7) | (height << 2));
        cols[i] = col;
    }
    return cols;
}

/// Build PWV7 RGB scroll data (3 bytes per column) from Mixxx waveform.
QByteArray buildRgbScrollData(TrackPointer pTrack, int numCols) {
    QByteArray data(numCols * 3, '\0');
    if (!pTrack) {
        return data;
    }

    ConstWaveformPointer pWaveform = pTrack->getWaveform();
    if (!pWaveform || pWaveform->getDataSize() == 0) {
        return data;
    }

    const int srcLen = pWaveform->getDataSize();
    for (int i = 0; i < numCols; ++i) {
        int srcIdx = (i * srcLen) / numCols;
        const WaveformData& d = pWaveform->get(srcIdx);
        data[i * 3 + 0] = static_cast<char>(d.filtered.high); // treble → R
        data[i * 3 + 1] = static_cast<char>(d.filtered.mid);  // mid    → G
        data[i * 3 + 2] = static_cast<char>(d.filtered.low);  // bass   → B
    }
    return data;
}

// ---------------------------------------------------------------------------
// CRC-16/XMODEM — poly 0x1021, init 0x0000, no reflection
// ---------------------------------------------------------------------------

quint16 crc16Xmodem(const QByteArray& data) {
    quint16 crc = 0x0000;
    for (unsigned char byte : data) {
        crc ^= (static_cast<quint16>(byte) << 8);
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Settings file writer
// ---------------------------------------------------------------------------

bool writeSettingFile(const QString& path, const QByteArray& settingsPayload) {
    // Format: magic(4) + len_header(2) + crc(2) + payload
    // CRC covers bytes 0..(len_header-3), i.e. magic + len_header field.
    QByteArray pre;
    {
        QDataStream ds(&pre, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << quint32(0x00100000u);
        quint16 lenHeader = static_cast<quint16>(8 + settingsPayload.size());
        ds << lenHeader;
    }
    quint16 crc = crc16Xmodem(pre);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << quint32(0x00100000u);
    ds << quint16(static_cast<quint16>(8 + settingsPayload.size()));
    ds << crc;
    f.write(settingsPayload);
    f.close();
    return true;
}

// ---------------------------------------------------------------------------
// DeviceSQL string encoding helpers
// ---------------------------------------------------------------------------

/// Encode a string as a DeviceSQL ASCII or UTF-16BE length-prefixed value.
QByteArray encodeDeviceSqlString(const QString& s) {
    QByteArray out;
    if (s.isNull() || s.isEmpty()) {
        out.append('\x40');
        out.append('\x00');
        return out;
    }

    // Try ASCII
    const QByteArray ascii = s.toLatin1();
    bool isAscii = true;
    for (unsigned char c : ascii) {
        if (c > 127) {
            isAscii = false;
            break;
        }
    }

    if (isAscii && s.size() < 128) {
        out.append(static_cast<char>(0x40u));
        out.append(static_cast<char>(ascii.size() & 0xFF));
        out.append(ascii);
    } else {
        // UTF-16BE
        QByteArray utf16;
        for (const QChar ch : s) {
            utf16.append(static_cast<char>((ch.unicode() >> 8) & 0xFF));
            utf16.append(static_cast<char>(ch.unicode() & 0xFF));
        }
        int byteLen = utf16.size();
        out.append(static_cast<char>(0x90u));
        out.append(static_cast<char>((byteLen >> 8) & 0xFF));
        out.append(static_cast<char>(byteLen & 0xFF));
        out.append(utf16);
    }
    return out;
}

// ---------------------------------------------------------------------------
// export.pdb writer
// ---------------------------------------------------------------------------

/// Minimal export.pdb with a single tracks table.
/// Writes one track row per entry.
bool writePdb(
        const QString& pdbPath,
        const QList<TrackPointer>& tracks,
        const QStringList& usbRelativePaths,
        const QStringList& anlzFolderPaths) {
    // Pioneer DeviceSQL pages are 4096 bytes.
    constexpr int kPageSize = 4096;
    constexpr int kPageHeaderSize = 32;
    constexpr int kRowHeapSize = kPageSize - kPageHeaderSize; // 4064 bytes

    // We write:
    //  page 0: file header (padded to 4096)
    //  pages 1..N: track rows packed into pages
    //
    // Table type 0 = tracks.

    // -- Serialise all track rows first to work out page count --
    struct TrackRow {
        QByteArray data;
        int id;
    };
    QList<TrackRow> rows;
    for (int i = 0; i < tracks.size(); ++i) {
        const TrackPointer& pTrack = tracks[i];
        if (!pTrack) {
            continue;
        }

        QByteArray rowData;
        QDataStream rd(&rowData, QIODevice::WriteOnly);
        rd.setByteOrder(QDataStream::BigEndian);

        int trackId = i + 1;
        QString filePath = usbRelativePaths.value(i);
        QString filename = QFileInfo(filePath).fileName();
        QString anlzFolder = anlzFolderPaths.value(i);
        QString anlzPath = QStringLiteral("/PIONEER/USBANLZ/") + anlzFolder;

        quint32 bpmRaw = static_cast<quint32>(qRound(pTrack->getBpm() * 100.0));
        quint32 duration = static_cast<quint32>(pTrack->getDuration());
        quint32 sampleRate = static_cast<quint32>(pTrack->getSampleRate());
        quint32 bitRate = static_cast<quint32>(pTrack->getBitrate());
        quint8 rating = static_cast<quint8>(pTrack->getRating());

        // Row binary layout (simplified DeviceSQL row):
        //   u32 id
        //   u32 bpm
        //   u32 duration
        //   u32 sampleRate
        //   u32 bitRate
        //   u8  rating
        //   DeviceSqlString analyzePath
        //   DeviceSqlString filename
        //   DeviceSqlString filePath
        rd << quint32(trackId);
        rd << bpmRaw;
        rd << duration;
        rd << sampleRate;
        rd << bitRate;
        rd << rating;
        rowData.append(encodeDeviceSqlString(anlzPath));
        rowData.append(encodeDeviceSqlString(filename));
        rowData.append(encodeDeviceSqlString(filePath));

        rows.append({rowData, trackId});
    }

    // -- Pack rows into 4096-byte pages --
    // Page layout:
    //   [0..3]  page_index (u32BE)
    //   [4..7]  type = 0 (u32BE)
    //   [8..11] next_page (u32BE, 0xFFFFFFFF for last)
    //   [12..15] unknown = 0
    //   [16..19] num_rows_large (u32BE)
    //   [20..21] num_rows (u16BE)
    //   [22..23] free_size (u16BE)
    //   [24..25] used_size (u16BE)
    //   [26..27] unknown
    //   [28..29] free_list_size (u16BE)
    //   [30..31] num_rows_large_2 (u16BE)
    //   [32..4095] row heap + row offset table at end

    // Row offsets are stored from the END of the heap, 2 bytes each.
    // Bit 15 of each offset = present flag.

    struct Page {
        QList<int> rowIndices; // indices into `rows`
    };
    QList<Page> pages;

    {
        int heapAvail = kRowHeapSize;
        Page current;
        for (int i = 0; i < rows.size(); ++i) {
            int rowBytes = rows[i].data.size();
            int needed = rowBytes + 2; // row data + 2-byte offset entry
            if (heapAvail < needed && !current.rowIndices.isEmpty()) {
                pages.append(current);
                current = Page{};
                heapAvail = kRowHeapSize;
            }
            current.rowIndices.append(i);
            heapAvail -= needed;
        }
        if (!current.rowIndices.isEmpty()) {
            pages.append(current);
        }
    }

    QFile f(pdbPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::BigEndian);

    // -- File header (page 0, padded to 4096) --
    int numTables = 1; // tracks only
    int firstTrackPage = 1;
    int lastTrackPage = qMax(1, pages.size()); // 1-based page indices
    int totalPages = 1 + pages.size();

    QByteArray headerPage(kPageSize, '\0');
    {
        QDataStream hds(&headerPage, QIODevice::WriteOnly);
        hds.setByteOrder(QDataStream::BigEndian);
        hds << quint32(0x00000000u); // magic
        hds << quint32(kPageSize);
        hds << quint32(numTables);
        hds << quint32(totalPages);  // next_unused_page
        hds << quint32(0x00000000u); // unknown
        hds << quint32(0x00000001u); // sequence
        hds << quint32(0x00000000u);

        // Table pointer for tracks (type=0), 20 bytes
        hds << quint32(0u);                              // type = tracks
        hds << quint32(0u);                              // empty_candidate
        hds << quint32(firstTrackPage);
        hds << quint32(lastTrackPage);
        hds << quint32(0x00000000u);
    }
    f.write(headerPage);

    // -- Track data pages --
    for (int pi = 0; pi < pages.size(); ++pi) {
        const Page& pg = pages[pi];
        int pageIndex = pi + 1;
        int nextPage = (pi + 1 < pages.size()) ? (pageIndex + 1) : 0xFFFFFFFF;
        quint16 numRows = static_cast<quint16>(pg.rowIndices.size());

        QByteArray rowHeap;
        QList<quint16> offsets;

        for (int ri : pg.rowIndices) {
            quint16 offset = static_cast<quint16>(rowHeap.size()) | 0x8000u; // present flag
            offsets.append(offset);
            rowHeap.append(rows[ri].data);
        }

        // Pad heap + offset table to kRowHeapSize
        QByteArray pageBody(kRowHeapSize, '\0');
        // Write rows at start
        memcpy(pageBody.data(), rowHeap.constData(),
                qMin(rowHeap.size(), kRowHeapSize));
        // Write offsets at end (in reverse order from the back)
        for (int i = 0; i < offsets.size(); ++i) {
            int pos = kRowHeapSize - 2 * (i + 1);
            pageBody[pos]     = static_cast<char>((offsets[i] >> 8) & 0xFF);
            pageBody[pos + 1] = static_cast<char>(offsets[i] & 0xFF);
        }

        // Write page header
        ds << quint32(pageIndex);
        ds << quint32(0u); // type = tracks
        ds << quint32(nextPage);
        ds << quint32(0u); // unknown
        ds << quint32(numRows); // num_rows_large
        ds << quint16(numRows);
        ds << quint16(static_cast<quint16>(kRowHeapSize - rowHeap.size())); // free_size
        ds << quint16(static_cast<quint16>(rowHeap.size()));                // used_size
        ds << quint16(0u);
        ds << quint16(0u); // free_list_size
        ds << quint16(numRows); // num_rows_large_2
        f.write(pageBody);
    }

    f.close();
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RekordboxExportJob
// ---------------------------------------------------------------------------

RekordboxExportJob::RekordboxExportJob(
        QObject* parent,
        TrackCollectionManager* pTrackCollectionManager,
        QSharedPointer<RekordboxExportRequest> pRequest)
        : QThread(parent),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_pRequest(std::move(pRequest)),
          m_cancelRequested(0) {
}

RekordboxExportJob::~RekordboxExportJob() = default;

void RekordboxExportJob::slotCancel() {
    m_cancelRequested.storeRelease(1);
}

QString RekordboxExportJob::computeAnlzFolder(const QString& usbRelativePath) {
    return ::mixxx::computeAnlzFolder(usbRelativePath);
}

void RekordboxExportJob::run() {
    // Load tracks on the main thread via a blocking queued connection.
    QMetaObject::invokeMethod(
            this, "slotLoadTracksFromDb", Qt::BlockingQueuedConnection);

    if (m_cancelRequested.loadAcquire()) {
        return;
    }

    const QString usbRoot = m_pRequest->usbRootDir.absolutePath();
    const QString pioneerDir = usbRoot + QStringLiteral("/PIONEER");
    const QString anlzRoot = pioneerDir + QStringLiteral("/USBANLZ");
    const QString musicRoot = usbRoot + QStringLiteral("/") +
            (m_pRequest->musicSubDir.isEmpty()
                    ? QStringLiteral("music")
                    : m_pRequest->musicSubDir);

    QDir dir;
    if (!dir.mkpath(anlzRoot) || !dir.mkpath(musicRoot)) {
        emit failed(tr("Could not create PIONEER directory structure at %1").arg(usbRoot));
        return;
    }

    if (!writeSettingsFiles(pioneerDir)) {
        emit failed(tr("Could not write settings files to %1").arg(pioneerDir));
        return;
    }

    const int total = m_tracks.size();
    emit jobMaximum(total);

    QStringList anlzFolders;

    for (int i = 0; i < total; ++i) {
        if (m_cancelRequested.loadAcquire()) {
            return;
        }
        emit jobProgress(i);

        TrackPointer pTrack = m_tracks[i];
        if (!pTrack) {
            anlzFolders.append(QString{});
            continue;
        }

        const QString& usbPath = m_usbPaths[i];
        const QString folder = computeAnlzFolder(usbPath);
        anlzFolders.append(folder);

        const QString anlzDir = anlzRoot + QStringLiteral("/") + folder;
        dir.mkpath(anlzDir);

        // Copy audio file
        const QString srcFilePath = pTrack->getLocation();
        const QString dstFilePath = musicRoot + QStringLiteral("/") +
                QFileInfo(srcFilePath).fileName();
        if (!QFile::exists(dstFilePath)) {
            QFile::copy(srcFilePath, dstFilePath);
        }

        qint64 fileSize = QFileInfo(srcFilePath).size();
        if (!writeAnlzFiles(anlzDir, usbPath, pTrack, fileSize)) {
            emit failed(tr("Could not write analysis files for track: %1").arg(srcFilePath));
            return;
        }
    }

    // Write export.pdb
    const QString pdbPath = usbRoot + QStringLiteral("/export.pdb");
    if (!writePdb(pdbPath, m_tracks, m_usbPaths, anlzFolders)) {
        emit failed(tr("Could not write export.pdb to %1").arg(pdbPath));
        return;
    }

    emit jobProgress(total);
    emit completed(total);
}

void RekordboxExportJob::slotLoadTracksFromDb() {
    VERIFY_OR_DEBUG_ASSERT(m_pTrackCollectionManager) {
        return;
    }

    TrackCollection* pColl = m_pTrackCollectionManager->internalCollection();
    VERIFY_OR_DEBUG_ASSERT(pColl) {
        return;
    }

    const bool exportAll = m_pRequest->crateIdsToExport.isEmpty() &&
            m_pRequest->playlistIdsToExport.isEmpty();

    QSet<TrackId> trackIds;

    if (exportAll) {
        QSqlQuery q(pColl->database());
        q.prepare(QStringLiteral("SELECT id FROM library WHERE mixxx_deleted = 0"));
        if (q.exec()) {
            while (q.next()) {
                trackIds.insert(TrackId(q.value(0)));
            }
        }
    } else {
        for (const CrateId& crateId : m_pRequest->crateIdsToExport) {
            auto result = pColl->crates().selectCrateTracksSorted(crateId);
            while (result.next()) {
                trackIds.insert(result.trackId());
            }
        }
        for (int plId : m_pRequest->playlistIdsToExport) {
            const QList<TrackId> plTracks = pColl->getPlaylistDAO().getTrackIds(plId);
            for (const TrackId& tid : plTracks) {
                trackIds.insert(tid);
            }
        }
    }

    const QString musicSubDir = m_pRequest->musicSubDir.isEmpty()
            ? QStringLiteral("music")
            : m_pRequest->musicSubDir;

    m_tracks.clear();
    m_usbPaths.clear();

    for (const TrackId& tid : trackIds) {
        TrackPointer pTrack = m_pTrackCollectionManager->getTrackById(tid);
        if (!pTrack) {
            continue;
        }
        const QString filename = QFileInfo(pTrack->getLocation()).fileName();
        const QString usbPath = QStringLiteral("/") + musicSubDir +
                QStringLiteral("/") + filename;
        m_tracks.append(pTrack);
        m_usbPaths.append(usbPath);
    }
}

bool RekordboxExportJob::writeSettingsFiles(const QString& pioneerDir) {
    // Minimal valid settings payloads (all zeros — CDJs use built-in defaults).
    QByteArray emptyPayload(8, '\0');

    return writeSettingFile(pioneerDir + QStringLiteral("/MYSETTING.DAT"), emptyPayload) &&
            writeSettingFile(pioneerDir + QStringLiteral("/MYSETTING2.DAT"), emptyPayload) &&
            writeSettingFile(pioneerDir + QStringLiteral("/DEVSETTING.DAT"), emptyPayload);
}

bool RekordboxExportJob::writeAnlzFiles(
        const QString& anlzDir,
        const QString& usbRelativePath,
        TrackPointer pTrack,
        qint64 audioFileSizeBytes) {
    const QList<BeatEntry> beats = buildBeatEntries(pTrack);

    // Duration in ms for scroll waveform column count (1 col per 10 ms)
    const int durationMs = static_cast<int>(pTrack->getDuration() * 1000.0);
    const int numScrollCols = qMax(1, durationMs / 10);

    // ---- ANLZ0000.DAT ----
    {
        QByteArray ppth = buildPpth(usbRelativePath);
        QByteArray pvbr = buildPvbr(audioFileSizeBytes);
        QByteArray pqtz = buildPqtz(beats);
        QByteArray pwav = buildPwav(downsampleMono(pTrack, 400));
        QByteArray pwv2 = buildPwv2(downsampleMono(pTrack, 100));
        QByteArray pcob0 = buildPcob(true);
        QByteArray pcob1 = buildPcob(false);

        quint32 totalSize = 28 + ppth.size() + pvbr.size() + pqtz.size() +
                pwav.size() + pwv2.size() + pcob0.size() + pcob1.size();

        QFile f(anlzDir + QStringLiteral("/ANLZ0000.DAT"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        QDataStream ds(&f);
        ds.setByteOrder(QDataStream::BigEndian);
        writePmaiFileHeader(ds, totalSize);
        f.write(ppth);
        f.write(pvbr);
        f.write(pqtz);
        f.write(pwav);
        f.write(pwv2);
        f.write(pcob0);
        f.write(pcob1);
        f.close();
    }

    // ---- ANLZ0000.EXT ----
    {
        QByteArray ppth = buildPpth(usbRelativePath);
        QByteArray pwv3 = buildPwv3(downsampleMono(pTrack, numScrollCols));
        QByteArray pcob0 = buildPcob(true);
        QByteArray pcob1 = buildPcob(false);
        QByteArray pco2a = buildPco2(true);
        QByteArray pco2b = buildPco2(false);
        QByteArray pqt2 = buildPqt2(beats);
        QByteArray pwv5 = buildPwv5(buildColourScrollColumns(pTrack, numScrollCols));
        QByteArray pwv4 = buildPwv4(QByteArray(1200 * 6, '\0'));

        quint32 totalSize = 28 + ppth.size() + pwv3.size() +
                pcob0.size() + pcob1.size() + pco2a.size() + pco2b.size() +
                pqt2.size() + pwv5.size() + pwv4.size();

        QFile f(anlzDir + QStringLiteral("/ANLZ0000.EXT"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        QDataStream ds(&f);
        ds.setByteOrder(QDataStream::BigEndian);
        writePmaiFileHeader(ds, totalSize);
        f.write(ppth);
        f.write(pwv3);
        f.write(pcob0);
        f.write(pcob1);
        f.write(pco2a);
        f.write(pco2b);
        f.write(pqt2);
        f.write(pwv5);
        f.write(pwv4);
        f.close();
    }

    // ---- ANLZ0000.2EX (CDJ-3000 only — only written when waveform data available) ----
    {
        ConstWaveformPointer pWaveform = pTrack->getWaveform();
        if (pWaveform && pWaveform->getDataSize() > 0) {
            QByteArray ppth = buildPpth(usbRelativePath);
            QByteArray pwv7 = buildPwv7(buildRgbScrollData(pTrack, numScrollCols));
            QByteArray pwv6 = buildPwv6(buildRgbScrollData(pTrack, 1200));
            QByteArray pwvc = buildPwvc();

            quint32 totalSize = 28 + ppth.size() + pwv7.size() + pwv6.size() + pwvc.size();

            QFile f(anlzDir + QStringLiteral("/ANLZ0000.2EX"));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            QDataStream ds(&f);
            ds.setByteOrder(QDataStream::BigEndian);
            writePmaiFileHeader(ds, totalSize);
            f.write(ppth);
            f.write(pwv7);
            f.write(pwv6);
            f.write(pwvc);
            f.close();
        }
    }

    return true;
}

bool RekordboxExportJob::writePdb(
        const QString& pdbPath,
        const QList<TrackPointer>& tracks,
        const QStringList& usbRelativePaths,
        const QStringList& anlzFolderPaths) {
    return ::mixxx::writePdb(pdbPath, tracks, usbRelativePaths, anlzFolderPaths);
}

} // namespace mixxx

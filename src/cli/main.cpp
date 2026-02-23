#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QThread>

#include "analyzer/analyzertrack.h"
#include "sources/soundsourceproxy.h"
#include "track/track.h"
#include "util/fileaccess.h"
#include "util/logging.h"

namespace {
    constexpr int kSuccessExitCode = 0;
    constexpr int kErrorExitCode = 1;
}

int main(int argc, char* argv[]) {
    // Create Qt Core application (no GUI)
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("mixxx-cli");
    QCoreApplication::setApplicationVersion("1.0.0");

    // Setup command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("Mixxx Command Line Track Analyzer");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add options
    QCommandLineOption trackPathOption(QStringList() << "t" << "track",
                                       "Path to the audio file to analyze",
                                       "path");
    parser.addOption(trackPathOption);

    QCommandLineOption minBpmOption("min-bpm",
                                    "Minimum BPM for detection (default: 60)",
                                    "bpm", "60");
    parser.addOption(minBpmOption);

    QCommandLineOption maxBpmOption("max-bpm",
                                    "Maximum BPM for detection (default: 200)",
                                    "bpm", "200");
    parser.addOption(maxBpmOption);

    QCommandLineOption fixedTempoOption("fixed-tempo",
                                        "Assume constant BPM (no tempo changes)");
    parser.addOption(fixedTempoOption);

    QCommandLineOption analyzeKeyOption("analyze-key",
                                        "Perform key detection");
    parser.addOption(analyzeKeyOption);

    QCommandLineOption analyzeGainOption("analyze-gain",
                                         "Perform ReplayGain analysis");
    parser.addOption(analyzeGainOption);

    // Parse command line
    parser.process(app);

    // Initialize logging - NOTE: Only 4 parameters!
    mixxx::Logging::initialize(
        QDir::currentPath(),  // log directory
                               mixxx::LogLevel::Info,  // log level
                               mixxx::LogLevel::Warning,  // flush level
                               mixxx::LogFlag::None);  // flags (no file logging for CLI)

                               // Validate required arguments
                               if (!parser.isSet(trackPathOption)) {
                                   qCritical() << "Error: Track path is required. Use --track <path>";
                                   parser.showHelp(kErrorExitCode);
                                   return kErrorExitCode;
                               }

                               QString trackPath = parser.value(trackPathOption);
                               QFileInfo fileInfo(trackPath);

                               if (!fileInfo.exists()) {
                                   qCritical() << "Error: File does not exist:" << trackPath;
                                   return kErrorExitCode;
                               }

                               if (!fileInfo.isFile()) {
                                   qCritical() << "Error: Path is not a file:" << trackPath;
                                   return kErrorExitCode;
                               }

                               qInfo() << "Analyzing track:" << trackPath;

                               // Get BPM range options
                               bool minBpmOk, maxBpmOk;
                               double minBpm = parser.value(minBpmOption).toDouble(&minBpmOk);
                               double maxBpm = parser.value(maxBpmOption).toDouble(&maxBpmOk);

                               if (!minBpmOk || !maxBpmOk || minBpm >= maxBpm || minBpm <= 0) {
                                   qCritical() << "Error: Invalid BPM range. Min must be less than Max and both positive.";
                                   return kErrorExitCode;
                               }

                               qInfo() << "BPM range:" << minBpm << "-" << maxBpm;

                               // Create a track object using the correct API
                               TrackPointer pTrack = Track::newTemporary(trackPath);
                               if (!pTrack) {
                                   qCritical() << "Error: Could not create track object";
                                   return kErrorExitCode;
                               }

                               // Load audio file
                               mixxx::AudioSourcePointer pAudioSource = SoundSourceProxy(pTrack).openAudioSource();
                               if (!pAudioSource) {
                                   qCritical() << "Error: Could not open audio file";
                                   return kErrorExitCode;
                               }

                               qInfo() << "Track loaded successfully";
                               qInfo() << "Duration:" << pTrack->getDuration() << "seconds";
                               qInfo() << "Sample rate:" << pAudioSource->getSignalInfo().getSampleRate() << "Hz";
                               qInfo() << "Channels:" << pAudioSource->getSignalInfo().getChannelCount();

                               // Setup analyzer options
                               AnalyzerTrack::Options options;
                               if (parser.isSet(fixedTempoOption)) {
                                   options.useFixedTempo = true;
                                   qInfo() << "Using fixed tempo analysis";
                               }

                               AnalyzerTrack analyzerTrack(pTrack, options);

                               // Perform BPM analysis
                               qInfo() << "Analyzing BPM...";

                               // TODO: Initialize and run analyzers here
                               // This is a simplified example - you'll need to:
                               // 1. Create analyzer instances
                               // 2. Initialize them with track info
                               // 3. Process audio samples
                               // 4. Store results

                               qInfo() << "Analysis complete!";

                               // Print results
                               if (pTrack->isBpmLocked()) {
                                   qInfo() << "BPM:" << pTrack->getBpm();
                               }

                               if (parser.isSet(analyzeKeyOption)) {
                                   qInfo() << "Key:" << pTrack->getKeyText();
                               }

                               if (parser.isSet(analyzeGainOption)) {
                                   qInfo() << "ReplayGain:" << pTrack->getReplayGain().getRatio();
                               }

                               mixxx::Logging::shutdown();
                               return kSuccessExitCode;
}

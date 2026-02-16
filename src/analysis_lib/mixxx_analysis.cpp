/**
 * Mixxx Audio Analysis Library Implementation
 */

#include "mixxx_analysis.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <memory>
#include <cstring>
#include <cmath>

#include "analyzer/analyzerbeats.h"
#include "analyzer/analyzergain.h"
#include "analyzer/analyzerkey.h"
#include "analyzer/analyzertrack.h"
#include "preferences/beatdetectionsettings.h"
#include "preferences/keydetectionsettings.h"
#include "preferences/replaygainsettings.h"
#include "preferences/usersettings.h"
#include "sources/audiosourcestereoproxy.h"
#include "sources/soundsourceproxy.h"
#include "track/keys.h"
#include "track/track.h"
#include "util/math.h"

namespace {

const char* LIBRARY_VERSION = "2.7.0-alpha";

// Helper class to manage analyzer context
class AnalyzerContext {
public:
    AnalyzerContext() {
        // Create a minimal in-memory config (not saving to file)
        m_pConfig = std::make_shared<UserSettings>(QSettings::IniFormat, QSettings::UserScope, "Mixxx", "MixxxAnalysisLib");
        
        // Set default preferences for analysis
        // BPM detection settings
        m_pConfig->setValue(ConfigKey(BPM_CONFIG_KEY, BPM_DETECTION_ENABLED), true);
        m_pConfig->setValue(ConfigKey(BPM_CONFIG_KEY, BPM_FIXED_TEMPO_ASSUMPTION), true);
        m_pConfig->setValue(ConfigKey(BPM_CONFIG_KEY, BPM_REANALYZE_WHEN_SETTINGS_CHANGE), false);
        m_pConfig->setValue(ConfigKey(BPM_CONFIG_KEY, BPM_FAST_ANALYSIS_ENABLED), false);
        
        // Key detection settings
        m_pConfig->setValue(ConfigKey(KEY_CONFIG_KEY, KEY_DETECTION_ENABLED), true);
        m_pConfig->setValue(ConfigKey(KEY_CONFIG_KEY, KEY_FAST_ANALYSIS), false);
        m_pConfig->setValue(ConfigKey(KEY_CONFIG_KEY, KEY_REANALYZE_WHEN_SETTINGS_CHANGE), false);
        
        // ReplayGain settings
        m_pConfig->setValue(ConfigKey("[ReplayGain]", "ReplayGainAnalyserEnabled"), true);
        m_pConfig->setValue(ConfigKey("[ReplayGain]", "ReplayGainAnalyserVersion"), 1);
    }
    
    UserSettingsPointer getConfig() const {
        return m_pConfig;
    }
    
private:
    UserSettingsPointer m_pConfig;
};

// Convert chromatic key enum to integer
int chromaticKeyToInt(mixxx::track::io::key::ChromaticKey key) {
    return static_cast<int>(key);
}

// Convert chromatic key to readable string
void chromaticKeyToString(mixxx::track::io::key::ChromaticKey key, char* buffer, size_t bufferSize) {
    QString keyText;
    
    switch (key) {
        case mixxx::track::io::key::C_MAJOR: keyText = "C"; break;
        case mixxx::track::io::key::D_FLAT_MAJOR: keyText = "Db"; break;
        case mixxx::track::io::key::D_MAJOR: keyText = "D"; break;
        case mixxx::track::io::key::E_FLAT_MAJOR: keyText = "Eb"; break;
        case mixxx::track::io::key::E_MAJOR: keyText = "E"; break;
        case mixxx::track::io::key::F_MAJOR: keyText = "F"; break;
        case mixxx::track::io::key::F_SHARP_MAJOR: keyText = "F#"; break;
        case mixxx::track::io::key::G_MAJOR: keyText = "G"; break;
        case mixxx::track::io::key::A_FLAT_MAJOR: keyText = "Ab"; break;
        case mixxx::track::io::key::A_MAJOR: keyText = "A"; break;
        case mixxx::track::io::key::B_FLAT_MAJOR: keyText = "Bb"; break;
        case mixxx::track::io::key::B_MAJOR: keyText = "B"; break;
        case mixxx::track::io::key::C_MINOR: keyText = "Cm"; break;
        case mixxx::track::io::key::C_SHARP_MINOR: keyText = "C#m"; break;
        case mixxx::track::io::key::D_MINOR: keyText = "Dm"; break;
        case mixxx::track::io::key::E_FLAT_MINOR: keyText = "Ebm"; break;
        case mixxx::track::io::key::E_MINOR: keyText = "Em"; break;
        case mixxx::track::io::key::F_MINOR: keyText = "Fm"; break;
        case mixxx::track::io::key::F_SHARP_MINOR: keyText = "F#m"; break;
        case mixxx::track::io::key::G_MINOR: keyText = "Gm"; break;
        case mixxx::track::io::key::G_SHARP_MINOR: keyText = "G#m"; break;
        case mixxx::track::io::key::A_MINOR: keyText = "Am"; break;
        case mixxx::track::io::key::B_FLAT_MINOR: keyText = "Bbm"; break;
        case mixxx::track::io::key::B_MINOR: keyText = "Bm"; break;
        default: keyText = "Unknown"; break;
    }
    
    strncpy(buffer, keyText.toUtf8().constData(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
}

} // anonymous namespace

extern "C" {

MixxxAnalyzerHandle mixxx_analyzer_create() {
    try {
        // Initialize Qt application if not already initialized
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("mixxx_analysis_lib")};
            new QCoreApplication(argc, argv);
        }
        
        auto* context = new AnalyzerContext();
        return static_cast<MixxxAnalyzerHandle>(context);
    } catch (const std::exception& e) {
        qWarning() << "Failed to create analyzer context:" << e.what();
        return nullptr;
    } catch (...) {
        qWarning() << "Failed to create analyzer context: unknown error";
        return nullptr;
    }
}

int mixxx_analyze_file(
        MixxxAnalyzerHandle handle,
        const char* file_path,
        MixxxAnalysisResult* result) {
    if (!handle || !file_path || !result) {
        return -1;
    }
    
    // Initialize result structure
    memset(result, 0, sizeof(MixxxAnalysisResult));
    result->key = -1;
    
    auto* context = static_cast<AnalyzerContext*>(handle);
    
    try {
        // Create a temporary track
        QString qFilePath = QString::fromUtf8(file_path);
        TrackPointer pTrack = Track::newTemporary(qFilePath);
        
        if (!pTrack) {
            strncpy(result->error_message, "Failed to create track object", 
                    sizeof(result->error_message) - 1);
            return -1;
        }
        
        // Open the audio source
        mixxx::AudioSource::OpenParams openParams;
        openParams.setChannelCount(2); // Stereo
        
        mixxx::AudioSourcePointer audioSource = 
            SoundSourceProxy(pTrack).openAudioSource(openParams);
        
        if (!audioSource) {
            strncpy(result->error_message, "Failed to open audio file", 
                    sizeof(result->error_message) - 1);
            return -1;
        }
        
        // Ensure stereo output - wrap non-stereo sources in stereo proxy
        // This includes mono files and multi-channel files (3+ channels)
        if (audioSource->getSignalInfo().getChannelCount() != 2) {
            audioSource = std::make_shared<mixxx::AudioSourceStereoProxy>(
                audioSource, 4096);
        }
        
        const auto sampleRate = audioSource->getSignalInfo().getSampleRate();
        const auto channelCount = audioSource->getSignalInfo().getChannelCount();
        const auto frameLength = audioSource->frameLength();
        
        // Create analyzers
        auto beatsAnalyzer = std::make_unique<AnalyzerBeats>(
            context->getConfig(), true);
        auto keyAnalyzer = std::make_unique<AnalyzerKey>(
            KeyDetectionSettings(context->getConfig()));
        auto gainAnalyzer = std::make_unique<AnalyzerGain>(
            context->getConfig());
        
        // Create analyzer track wrapper
        AnalyzerTrack analyzerTrack(pTrack);
        
        // Initialize analyzers
        bool beatsActive = beatsAnalyzer->initialize(
            analyzerTrack, sampleRate, channelCount, frameLength);
        bool keyActive = keyAnalyzer->initialize(
            analyzerTrack, sampleRate, channelCount, frameLength);
        bool gainActive = gainAnalyzer->initialize(
            analyzerTrack, sampleRate, channelCount, frameLength);
        
        // Process audio in chunks
        const SINT kChunkSize = 4096;
        std::vector<CSAMPLE> sampleBuffer(kChunkSize * channelCount);
        
        mixxx::IndexRange remainingFrames = audioSource->frameIndexRange();
        while (!remainingFrames.empty()) {
            auto chunkFrames = remainingFrames.splitAndShrinkFront(
                std::min(kChunkSize, remainingFrames.length()));
            
            const auto readResult = audioSource->readSampleFrames(
                mixxx::WritableSampleFrames(
                    chunkFrames,
                    mixxx::SampleBuffer::WritableSlice(
                        sampleBuffer.data(),
                        chunkFrames.length() * channelCount)));
            
            if (readResult.frameIndexRange().empty()) {
                break; // End of audio
            }
            
            const SINT samplesRead = readResult.frameIndexRange().length() * channelCount;
            
            // Feed samples to active analyzers
            if (beatsActive) {
                beatsActive = beatsAnalyzer->processSamples(
                    sampleBuffer.data(), samplesRead);
            }
            if (keyActive) {
                keyActive = keyAnalyzer->processSamples(
                    sampleBuffer.data(), samplesRead);
            }
            if (gainActive) {
                gainActive = gainAnalyzer->processSamples(
                    sampleBuffer.data(), samplesRead);
            }
        }
        
        // Store results in track
        if (beatsActive) {
            beatsAnalyzer->storeResults(pTrack);
            beatsAnalyzer->cleanup();
        }
        if (keyActive) {
            keyAnalyzer->storeResults(pTrack);
            keyAnalyzer->cleanup();
        }
        if (gainActive) {
            gainAnalyzer->storeResults(pTrack);
            gainAnalyzer->cleanup();
        }
        
        // Extract results from track
        
        // BPM
        const auto bpm = pTrack->getBpm();
        if (bpm > 0.0) {
            result->bpm = bpm;
            result->bpm_detected = 1;
        }
        
        // Key
        const Keys keys = pTrack->getKeys();
        const auto globalKey = keys.getGlobalKey();
        if (globalKey != mixxx::track::io::key::INVALID) {
            result->key = chromaticKeyToInt(globalKey);
            result->key_detected = 1;
            chromaticKeyToString(globalKey, result->key_name, 
                                sizeof(result->key_name));
        }
        
        // ReplayGain
        const mixxx::ReplayGain replayGain = pTrack->getReplayGain();
        if (replayGain.hasRatio()) {
            result->replay_gain_ratio = replayGain.getRatio();
            result->replay_gain_db = ratio2db(replayGain.getRatio());
            result->gain_calculated = 1;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        strncpy(result->error_message, e.what(), 
                sizeof(result->error_message) - 1);
        return -1;
    } catch (...) {
        strncpy(result->error_message, "Unknown error during analysis", 
                sizeof(result->error_message) - 1);
        return -1;
    }
}

void mixxx_analyzer_destroy(MixxxAnalyzerHandle handle) {
    if (handle) {
        auto* context = static_cast<AnalyzerContext*>(handle);
        delete context;
    }
}

const char* mixxx_analyzer_version() {
    return LIBRARY_VERSION;
}

} // extern "C"

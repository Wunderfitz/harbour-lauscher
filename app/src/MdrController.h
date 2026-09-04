/*
    Copyright (C) 2026 Sebastian J. Wolf and other contributors

    This file is part of Lauscher.

    Lauscher is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Lauscher is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Lauscher. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MDRCONTROLLER_H
#define MDRCONTROLLER_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <mdr-c/Headphones.h>

class BluezTransport;
class QTimer;

/**
 * QML-facing facade over libmdr's C ABI.
 *
 * libmdr is a poll-driven state machine: every request is a coroutine that only
 * advances inside mdrHeadphonesPoll(), so a timer ticks it and turns the events
 * it reports into Qt property notifications. Setters stage a change locally and
 * mdrHeadphonesRequestCommit() pushes the whole dirty set to the device, which is
 * why nothing here writes to the socket directly.
 */
class MdrController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList pairedDevices READ pairedDevices NOTIFY pairedDevicesChanged)

    Q_PROPERTY(QString deviceName READ deviceName NOTIFY identityChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY identityChanged)
    Q_PROPERTY(QString serialNumber READ serialNumber NOTIFY identityChanged)
    Q_PROPERTY(QString codec READ codec NOTIFY identityChanged)
    Q_PROPERTY(int protocolVersion READ protocolVersion NOTIFY identityChanged)

    Q_PROPERTY(QVariantList batteries READ batteries NOTIFY batteriesChanged)

    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY featuresChanged)
    Q_PROPERTY(bool playbackControlAvailable READ playbackControlAvailable NOTIFY featuresChanged)
    Q_PROPERTY(int volume READ volume NOTIFY playbackChanged)
    Q_PROPERTY(int maximumVolume READ maximumVolume CONSTANT)
    Q_PROPERTY(int volumePercent READ volumePercent NOTIFY playbackChanged)
    Q_PROPERTY(int playbackStatus READ playbackStatus NOTIFY playbackChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY playbackChanged)
    Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY playbackChanged)
    Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY playbackChanged)

    Q_PROPERTY(bool noiseControlAvailable READ noiseControlAvailable NOTIFY featuresChanged)
    Q_PROPERTY(bool ambientLevelAvailable READ ambientLevelAvailable NOTIFY featuresChanged)
    Q_PROPERTY(int noiseMode READ noiseMode NOTIFY noiseControlChanged)
    Q_PROPERTY(int ambientLevel READ ambientLevel NOTIFY noiseControlChanged)
    Q_PROPERTY(bool focusOnVoice READ focusOnVoice NOTIFY noiseControlChanged)

    Q_PROPERTY(bool listeningModeAvailable READ listeningModeAvailable NOTIFY featuresChanged)
    Q_PROPERTY(QVariantList listeningModes READ listeningModes NOTIFY featuresChanged)
    Q_PROPERTY(bool backgroundRoomAvailable READ backgroundRoomAvailable NOTIFY featuresChanged)
    Q_PROPERTY(int listeningMode READ listeningMode NOTIFY listeningChanged)
    Q_PROPERTY(int backgroundRoom READ backgroundRoom NOTIFY listeningChanged)

    Q_PROPERTY(bool multipointAvailable READ multipointAvailable NOTIFY featuresChanged)
    Q_PROPERTY(bool sourceSwitchingAvailable READ sourceSwitchingAvailable NOTIFY featuresChanged)
    Q_PROPERTY(QVariantList multipointDevices READ multipointDevices NOTIFY multipointChanged)
    Q_PROPERTY(bool sourceSwitchingEnabled READ sourceSwitchingEnabled NOTIFY multipointChanged)
    Q_PROPERTY(QString multipointMessage READ multipointMessage NOTIFY multipointChanged)

public:
    enum State {
        Idle,
        Connecting,
        Initializing,
        Ready,
        Error
    };
    Q_ENUM(State)

    /** Mirrors MDR_PLAYBACK_*. What the source device is doing, not the headset. */
    enum PlaybackStatus {
        PlaybackUnknown = MDR_PLAYBACK_UNKNOWN,
        PlaybackStopped = MDR_PLAYBACK_STOPPED,
        PlaybackPlaying = MDR_PLAYBACK_PLAYING,
        PlaybackPaused = MDR_PLAYBACK_PAUSED
    };
    Q_ENUM(PlaybackStatus)

    /** Mirrors MDR_NOISE_MODE_*, exposed so QML need not include the C header. */
    enum NoiseMode {
        NoiseOff = MDR_NOISE_MODE_OFF,
        NoiseCancelling = MDR_NOISE_MODE_CANCELLING,
        AmbientSound = MDR_NOISE_MODE_AMBIENT
    };
    Q_ENUM(NoiseMode)

    /** Mirrors MDR_LISTENING_*. */
    enum ListeningMode {
        Standard = MDR_LISTENING_STANDARD,
        BackgroundMusic = MDR_LISTENING_BACKGROUND_MUSIC,
        Cinema = MDR_LISTENING_CINEMA,
        VoiceBoost = MDR_LISTENING_VOICE_BOOST,
        SoundLeakageReduction = MDR_LISTENING_SOUND_LEAKAGE_REDUCTION
    };
    Q_ENUM(ListeningMode)

    /** Mirrors MDR_ROOM_*, the distance background music is mixed for. */
    enum RoomSize {
        RoomUnknown = MDR_ROOM_UNKNOWN,
        RoomSmall = MDR_ROOM_SMALL,
        RoomMedium = MDR_ROOM_MEDIUM,
        RoomLarge = MDR_ROOM_LARGE
    };
    Q_ENUM(RoomSize)

    explicit MdrController(QObject *parent = nullptr);
    ~MdrController() override;

    State state() const { return m_state; }
    QString statusMessage() const { return m_statusMessage; }
    QVariantList pairedDevices() const { return m_pairedDevices; }

    QString deviceName() const { return m_deviceName; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    QString serialNumber() const { return m_serialNumber; }
    QString codec() const { return m_codec; }
    int protocolVersion() const { return m_protocolVersion; }

    QVariantList batteries() const { return m_batteries; }

    bool volumeAvailable() const { return m_volumeAvailable; }
    bool playbackControlAvailable() const { return m_playbackControlAvailable; }
    int volume() const { return m_volume; }
    int maximumVolume() const;
    int volumePercent() const { return volumeToPercent(m_volume); }

    /* The volume the device speaks in is 0..30; everything the user sees is a
     * percentage of that, so both pages convert through here. */
    Q_INVOKABLE int volumeToPercent(int volume) const;
    int playbackStatus() const { return m_playbackStatus; }
    QString trackTitle() const { return m_trackTitle; }
    QString trackAlbum() const { return m_trackAlbum; }
    QString trackArtist() const { return m_trackArtist; }

    bool noiseControlAvailable() const { return m_noiseControlAvailable; }
    bool ambientLevelAvailable() const { return m_ambientLevelAvailable; }
    int noiseMode() const { return m_noiseMode; }
    int ambientLevel() const { return m_ambientLevel; }
    bool focusOnVoice() const { return m_focusOnVoice; }

    bool listeningModeAvailable() const { return m_listeningModeAvailable; }
    QVariantList listeningModes() const { return m_listeningModes; }
    bool backgroundRoomAvailable() const { return m_backgroundRoomAvailable; }
    int listeningMode() const { return m_listeningMode; }
    int backgroundRoom() const { return m_backgroundRoom; }

    bool multipointAvailable() const { return m_multipointAvailable; }
    bool sourceSwitchingAvailable() const { return m_sourceSwitchingAvailable; }
    QVariantList multipointDevices() const { return m_multipointDevices; }
    bool sourceSwitchingEnabled() const { return m_sourceSwitchingEnabled; }
    QString multipointMessage() const { return m_multipointMessage; }

public slots:
    void refreshPairedDevices();
    void connectToDevice(const QString &address);
    void disconnectDevice();

    void setVolume(int volume);
    void play();
    void pause();
    void nextTrack();
    void previousTrack();
    void setNoiseMode(int mode);
    void setAmbientLevel(int level);
    void setFocusOnVoice(bool enabled);
    void setListeningMode(int mode);
    void setBackgroundRoom(int room);

    void selectPlaybackDevice(const QString &address);
    void connectPairedDevice(const QString &address);
    void disconnectPairedDevice(const QString &address);
    void setSourceSwitchingEnabled(bool enabled);

signals:
    void stateChanged();
    void statusMessageChanged();
    void pairedDevicesChanged();
    void identityChanged();
    void batteriesChanged();
    void featuresChanged();
    void playbackChanged();
    void noiseControlChanged();
    void listeningChanged();
    void multipointChanged();

private slots:
    void tick();

private:
    void setState(State state);
    void setStatus(const QString &message);
    void fail(const QString &message);

    void openDevice();
    void closeDevice();

    void pumpConnection();
    void pumpDevice();

    void refreshIdentity();
    void refreshBatteries();
    void refreshFeatures();
    void refreshPlayback();
    void refreshNoiseControl();
    void refreshListening();
    void refreshMultipoint();
    void refreshAll();

    void sendPlaybackAction(MDRPlaybackAction action);
    void sendPairedDeviceCommand(MDRPairedDeviceCommand command, const QString &address);
    QString sourceSwitchMessage(MDRSourceSwitchControlResult result) const;

    QString textOf(MDRText text, uint32_t index = 0) const;
    bool featureAvailable(MDRFeature feature) const;

    BluezTransport *m_transport = nullptr;
    MDRHeadphones *m_device = nullptr;
    QTimer *m_timer = nullptr;

    State m_state = Idle;
    QString m_statusMessage;
    QVariantList m_pairedDevices;

    QString m_address;
    /* XM5-and-newer UUID first, legacy second; index into kServiceUuids. */
    int m_serviceIndex = 0;

    QString m_deviceName;
    QString m_firmwareVersion;
    QString m_serialNumber;
    QString m_codec;
    int m_protocolVersion = 0;

    QVariantList m_batteries;

    bool m_volumeAvailable = false;
    bool m_playbackControlAvailable = false;
    /* The device's own scale, not a percentage: 0..30, as libmdr validates. */
    int m_volume = 0;
    int m_playbackStatus = MDR_PLAYBACK_UNKNOWN;
    QString m_trackTitle;
    QString m_trackAlbum;
    QString m_trackArtist;

    bool m_noiseControlAvailable = false;
    bool m_ambientLevelAvailable = false;
    int m_noiseMode = MDR_NOISE_MODE_OFF;
    int m_ambientLevel = 0;
    bool m_focusOnVoice = false;

    bool m_listeningModeAvailable = false;
    /* The MDR_LISTENING_* values this device offers, Standard first. Each mode is
     * advertised on its own, so the picker is built from this rather than from a
     * fixed list. */
    QVariantList m_listeningModes;
    bool m_backgroundRoomAvailable = false;
    int m_listeningMode = MDR_LISTENING_STANDARD;
    int m_backgroundRoom = MDR_ROOM_UNKNOWN;

    bool m_multipointAvailable = false;
    bool m_sourceSwitchingAvailable = false;
    /* The devices the headset itself knows about - phones and computers it is
     * paired with - not the BlueZ list above. One of them holds playback. */
    QVariantList m_multipointDevices;
    /* True while the headset may hand playback to the other device on its own.
     * Sound Connect shows the false case as a padlock on the playing device. */
    bool m_sourceSwitchingEnabled = true;
    /* Why the headset refused the last playback-device request, or empty. */
    QString m_multipointMessage;

    /* The mode asked of the device and not yet seen coming back, or -1. The device
     * passes through "every mode off" on its way between two listening modes, and
     * that reading is indistinguishable from Standard - see refreshListening(). */
    int m_requestedListeningMode = -1;
    QElapsedTimer m_listeningRequestAge;
};

#endif // MDRCONTROLLER_H

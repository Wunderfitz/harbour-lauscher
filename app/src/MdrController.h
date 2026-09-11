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
    /* Whether a control session is up or on its way, which is what the pulley menu's
     * Connect/Disconnect entry says. Idle and Error are the two states that are not. */
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    /* True while the app is waiting for a headset that went away, or is in the middle
     * of an attempt to pick it up again. What separates it from Connecting is that
     * nobody asked for this one, so the page says so quietly rather than as an error. */
    Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY reconnectingChanged)
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
    Q_PROPERTY(QVariantList noiseModes READ noiseModes NOTIFY featuresChanged)
    Q_PROPERTY(int noiseMode READ noiseMode NOTIFY noiseControlChanged)
    Q_PROPERTY(int ambientLevel READ ambientLevel NOTIFY noiseControlChanged)
    Q_PROPERTY(bool focusOnVoice READ focusOnVoice NOTIFY noiseControlChanged)

    Q_PROPERTY(bool listeningModeAvailable READ listeningModeAvailable NOTIFY featuresChanged)
    Q_PROPERTY(QVariantList listeningModes READ listeningModes NOTIFY featuresChanged)
    Q_PROPERTY(bool backgroundRoomAvailable READ backgroundRoomAvailable NOTIFY featuresChanged)
    Q_PROPERTY(int listeningMode READ listeningMode NOTIFY listeningChanged)
    Q_PROPERTY(int backgroundRoom READ backgroundRoom NOTIFY listeningChanged)

    /* Two separate things, and the UI treats them differently: the feature bit says
     * the headset has an equalizer at all, MDREqualizer.available says it will act on
     * changes right now - it switches the equalizer off while a listening mode other
     * than Standard is active. Hence available/usable rather than one flag. */
    Q_PROPERTY(bool equalizerAvailable READ equalizerAvailable NOTIFY featuresChanged)
    Q_PROPERTY(QVariantList equalizerPresets READ equalizerPresets NOTIFY equalizerPresetsChanged)
    Q_PROPERTY(bool equalizerUsable READ equalizerUsable NOTIFY equalizerChanged)
    Q_PROPERTY(int equalizerPreset READ equalizerPreset NOTIFY equalizerChanged)
    Q_PROPERTY(int equalizerBandCount READ equalizerBandCount NOTIFY equalizerChanged)
    Q_PROPERTY(QVariantList equalizerBands READ equalizerBands NOTIFY equalizerChanged)
    Q_PROPERTY(int equalizerBandMinimum READ equalizerBandMinimum NOTIFY equalizerChanged)
    Q_PROPERTY(int equalizerBandMaximum READ equalizerBandMaximum NOTIFY equalizerChanged)
    Q_PROPERTY(bool clearBassAvailable READ clearBassAvailable NOTIFY equalizerChanged)
    Q_PROPERTY(int clearBass READ clearBass NOTIFY equalizerChanged)
    Q_PROPERTY(int clearBassMinimum READ clearBassMinimum CONSTANT)
    Q_PROPERTY(int clearBassMaximum READ clearBassMaximum CONSTANT)

    /* DSEE rides in the same struct as the equalizer and arrives on the same event, but
     * it is its own feature bit and its own switch - the two are gated separately. */
    Q_PROPERTY(bool dseeAvailable READ dseeAvailable NOTIFY featuresChanged)
    Q_PROPERTY(bool dseeUsable READ dseeUsable NOTIFY dseeChanged)
    Q_PROPERTY(bool dseeEnabled READ dseeEnabled NOTIFY dseeChanged)
    Q_PROPERTY(QString dseeName READ dseeName NOTIFY dseeChanged)

    /* The device's own settings page: the booleans it defines itself and the
     * Bluetooth connection quality. Two unrelated features, and the page exists for
     * whichever of them this headset has. */
    Q_PROPERTY(QVariantList generalSettings READ generalSettings NOTIFY generalSettingsChanged)
    Q_PROPERTY(bool connectionModeAvailable READ connectionModeAvailable NOTIFY featuresChanged)
    Q_PROPERTY(int audioPriority READ audioPriority NOTIFY connectionModeChanged)

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

    /** Mirrors MDR_AUDIO_PRIORITY_*, what the Bluetooth link is tuned for. */
    enum AudioPriority {
        PriorityUnknown = MDR_AUDIO_PRIORITY_UNKNOWN,
        PriorityQuality = MDR_AUDIO_PRIORITY_QUALITY,
        PriorityStability = MDR_AUDIO_PRIORITY_STABILITY
    };
    Q_ENUM(AudioPriority)

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
    bool connected() const { return m_state != Idle && m_state != Error; }
    bool reconnecting() const;
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
    QVariantList noiseModes() const { return m_noiseModes; }
    int noiseMode() const { return m_noiseMode; }
    int ambientLevel() const { return m_ambientLevel; }
    bool focusOnVoice() const { return m_focusOnVoice; }

    bool listeningModeAvailable() const { return m_listeningModeAvailable; }
    QVariantList listeningModes() const { return m_listeningModes; }
    bool backgroundRoomAvailable() const { return m_backgroundRoomAvailable; }
    int listeningMode() const { return m_listeningMode; }
    int backgroundRoom() const { return m_backgroundRoom; }

    bool equalizerAvailable() const { return m_equalizerAvailable; }
    QVariantList equalizerPresets() const { return m_equalizerPresets; }
    bool equalizerUsable() const { return m_equalizerUsable; }
    int equalizerPreset() const { return m_equalizerPreset; }
    int equalizerBandCount() const { return m_equalizerBands.size(); }
    QVariantList equalizerBands() const { return m_equalizerBands; }
    int equalizerBandMinimum() const;
    int equalizerBandMaximum() const;
    bool clearBassAvailable() const { return m_clearBassAvailable; }
    int clearBass() const { return m_clearBass; }
    int clearBassMinimum() const;
    int clearBassMaximum() const;

    /* Which frequency a band sits on. A function of the layout the device reports, not
     * of the value, so the strip asks once. The short form is what fits under a band a
     * tenth of the screen wide; the spoken one is for the readout, which has room. */
    Q_INVOKABLE QString equalizerBandLabel(int index) const;
    Q_INVOKABLE QString equalizerBandFrequency(int index) const;

    bool dseeAvailable() const { return m_dseeAvailable; }
    bool dseeUsable() const { return m_dseeUsable; }
    bool dseeEnabled() const { return m_dseeEnabled; }
    QString dseeName() const { return m_dseeName; }

    QVariantList generalSettings() const { return m_generalSettings; }
    bool connectionModeAvailable() const { return m_connectionModeAvailable; }
    int audioPriority() const { return m_audioPriority; }

    bool multipointAvailable() const { return m_multipointAvailable; }
    bool sourceSwitchingAvailable() const { return m_sourceSwitchingAvailable; }
    QVariantList multipointDevices() const { return m_multipointDevices; }
    bool sourceSwitchingEnabled() const { return m_sourceSwitchingEnabled; }
    QString multipointMessage() const { return m_multipointMessage; }

public slots:
    void refreshPairedDevices();
    void connectToDevice(const QString &address);
    /** Open the channel to the device that was last asked for, if there was one. */
    void reconnectDevice();
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

    void setEqualizerPreset(int preset);
    void setEqualizerBand(int index, int value);
    void setClearBass(int value);
    void setDseeEnabled(bool enabled);

    void setGeneralSetting(int index, bool value);
    void setAudioPriority(int priority);

    void selectPlaybackDevice(const QString &address);
    void connectPairedDevice(const QString &address);
    void disconnectPairedDevice(const QString &address);
    void setSourceSwitchingEnabled(bool enabled);

signals:
    void stateChanged();
    void reconnectingChanged();
    void statusMessageChanged();
    void pairedDevicesChanged();
    void identityChanged();
    void batteriesChanged();
    void featuresChanged();
    void playbackChanged();
    void noiseControlChanged();
    void listeningChanged();
    void equalizerChanged();
    /* Separate from equalizerChanged: the list arrives once and the page's picker is built
     * from it, so it must not be restated every time a band moves. */
    void equalizerPresetsChanged();
    void dseeChanged();
    void generalSettingsChanged();
    void connectionModeChanged();
    void multipointChanged();

private slots:
    void tick();
    void handleLinkLost();
    void handleDeviceReturned();
    void attemptReconnect();

private:
    void setState(State state);
    void setStatus(const QString &message);
    void fail(const QString &message);

    void openDevice();
    void closeDevice();

    void startConnection();
    void connectAttemptFailed(const QString &reason);
    void scheduleReconnect(int delayMs);
    void setReconnectPending(bool pending);
    QString pendingStatus() const;

    void pumpConnection();
    void pumpDevice();

    void refreshIdentity();
    void refreshBatteries();
    void refreshFeatures();
    void refreshPlayback();
    void refreshNoiseControl();
    void refreshListening();
    void refreshEqualizer();
    void refreshGeneralSettings();
    void refreshConnectionMode();
    void refreshMultipoint();
    void refreshAll();

    void sendPlaybackAction(MDRPlaybackAction action);
    void sendPairedDeviceCommand(MDRPairedDeviceCommand command, const QString &address);
    QString sourceSwitchMessage(MDRSourceSwitchControlResult result) const;

    /* The device names its own settings, but in tokens rather than sentences -
     * MULTIPOINT_SETTING, not "Connect to two devices". These turn the ones we know
     * into words and fall back to the token, tidied up, for the ones we do not. */
    QString generalSettingTitle(const QString &subject) const;
    QString generalSettingDescription(const QString &summary) const;

    QVariantList equalizerPresetList() const;
    QString equalizerPresetName(MDREqualizerPreset preset) const;
    QString dseeTypeName(MDRDSEEType type) const;

    QString codecName(MDRAudioCodec codec) const;
    QString batteryPartName(MDRBatteryPart part) const;
    QString textOf(MDRText text, uint32_t index = 0) const;
    bool featureAvailable(MDRFeature feature) const;

    BluezTransport *m_transport = nullptr;
    MDRHeadphones *m_device = nullptr;
    QTimer *m_timer = nullptr;
    /* Single-shot: the next reconnect attempt, armed only while one is wanted. */
    QTimer *m_reconnectTimer = nullptr;

    State m_state = Idle;
    QString m_statusMessage;
    QVariantList m_pairedDevices;

    QString m_address;
    /* XM5-and-newer UUID first, legacy second; index into kServices. */
    int m_serviceIndex = 0;

    /* True from the moment a device is asked for until the session is given up on
     * deliberately - leaving the device page, or disconnecting from its menu. While
     * it holds, a headset that comes back is connected to again by itself. */
    bool m_autoReconnect = false;
    /* Attempts made since the device was last seen; reset when it turns up again, so
     * a headset that is there but not answering is not chased forever. */
    int m_reconnectAttempts = 0;
    /* True from a lost connection until the session is back or given up on. What it
     * buys is a steady message: the reason an attempt failed is only worth showing
     * once there are no attempts left, so while this holds the page says what the
     * app is doing rather than flicking through the failures on the way. */
    bool m_reconnectPending = false;

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
    QVariantList m_noiseModes;
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

    bool m_equalizerAvailable = false;
    /* What the picker offers, each {preset, name}: the presets the device advertised, or
     * everything this protocol family can encode where it advertised nothing - see
     * equalizerPresetList(). */
    QVariantList m_equalizerPresets;
    /* Whether the device will act on equalizer changes at the moment, as opposed to
     * having an equalizer at all. Both read true until the device says otherwise. */
    bool m_equalizerUsable = true;
    int m_equalizerPreset = MDR_EQ_OFF;
    /* One entry per band, in the device's own steps: five bands step +-10, ten step
     * +-6, and a device with neither reports none at all. */
    QVariantList m_equalizerBands;
    /* Only the five-band layout carries it; the ten-band frames have no room for it
     * and libmdr reports 0 there. */
    bool m_clearBassAvailable = false;
    int m_clearBass = 0;

    bool m_dseeAvailable = false;
    /* As with the equalizer: available is the feature bit, usable is what the device
     * will act on now. It switches DSEE off alongside the equalizer while a listening
     * mode other than Standard is active. */
    bool m_dseeUsable = true;
    bool m_dseeEnabled = false;
    /* What this device calls it - DSEE, DSEE HX, and so on. Read from its capability, so
     * the switch is labelled the way the headset's own app labels it. */
    QString m_dseeName;

    /* One entry per device-defined boolean the ABI can read and write, each
     * {index, title, description, value}. Lists and read-only entries are left out
     * of it, so this is what the page can show rather than what was advertised. */
    QVariantList m_generalSettings;
    bool m_connectionModeAvailable = false;
    int m_audioPriority = MDR_AUDIO_PRIORITY_UNKNOWN;

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

    /* Set when the device asks whether it may apply a change it is holding, and
     * cleared once that has been answered - see MDR_EVENT_ALERT in pumpDevice(). */
    bool m_alertPending = false;

    /* The mode asked of the device and not yet seen coming back, or -1. The device
     * passes through "every mode off" on its way between two listening modes, and
     * that reading is indistinguishable from Standard - see refreshListening(). */
    int m_requestedListeningMode = -1;
    QElapsedTimer m_listeningRequestAge;
};

#endif // MDRCONTROLLER_H

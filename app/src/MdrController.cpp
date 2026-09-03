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

#include "MdrController.h"
#include "BluezTransport.h"

#include <QDebug>
#include <QTimer>
#include <QVector>

#include <mdr-c/Base.h>

namespace {

/* Sony uses one RFCOMM service for XM5-and-newer and another for XM4-and-older.
 * Which one a device answers on is the practical protocol-family discriminator,
 * so we try them in turn. LinkBuds Clip is a V2 device and answers on the first. */
const char *const kServiceUuids[] = {
    MDR_SERVICE_UUID_XM5,
    MDR_SERVICE_UUID_LEGACY
};
const int kServiceUuidCount = int(sizeof(kServiceUuids) / sizeof(kServiceUuids[0]));

/* 30 ms keeps the protocol's coroutines responsive without busy-spinning the
 * phone; libmdr does no work of its own between polls. */
const int kPollIntervalMs = 30;

/* The headset's own volume scale is 0..30 - mdrHeadphonesSetPlayback rejects
 * anything above that. The UI shows percent instead, so this is also the divisor
 * volumeToPercent() works from. */
const int kMaxVolume = 30;

/* How long a listening mode the user picked outranks the device reporting Standard.
 * Switching between two listening modes, the device reports every mode off first and
 * the new one on 0.2-0.4 s later; this is that window with room to spare, after which
 * whatever the device says is taken at face value again. */
const int kListeningSettleMs = 2000;

QString codecName(MDRAudioCodec codec)
{
    switch (codec) {
    case MDR_AUDIO_CODEC_SBC: return QStringLiteral("SBC");
    case MDR_AUDIO_CODEC_AAC: return QStringLiteral("AAC");
    case MDR_AUDIO_CODEC_LDAC: return QStringLiteral("LDAC");
    case MDR_AUDIO_CODEC_APTX: return QStringLiteral("aptX");
    case MDR_AUDIO_CODEC_APTX_HD: return QStringLiteral("aptX HD");
    case MDR_AUDIO_CODEC_LC3: return QStringLiteral("LC3");
    case MDR_AUDIO_CODEC_OTHER: return QStringLiteral("Other");
    default: return QString();
    }
}

QString batteryPartName(MDRBatteryPart part)
{
    switch (part) {
    case MDR_BATTERY_LEFT: return QStringLiteral("Left");
    case MDR_BATTERY_RIGHT: return QStringLiteral("Right");
    case MDR_BATTERY_CASE: return QStringLiteral("Case");
    default: return QStringLiteral("Battery");
    }
}

} // namespace

MdrController::MdrController(QObject *parent)
    : QObject(parent)
{
    m_transport = new BluezTransport(this);
    QObject::connect(m_transport, &BluezTransport::failed, this, &MdrController::fail);

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    QObject::connect(m_timer, &QTimer::timeout, this, &MdrController::tick);

    refreshPairedDevices();
}

MdrController::~MdrController()
{
    closeDevice();
}

/* --------------------------------------------------------------- bookkeeping */

void MdrController::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void MdrController::setStatus(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void MdrController::fail(const QString &message)
{
    qWarning() << "[lauscher]" << message;
    closeDevice();
    setStatus(message);
    setState(Error);
}

/* ------------------------------------------------------------------ devices */

void MdrController::refreshPairedDevices()
{
    m_pairedDevices = m_transport->pairedDevices();
    emit pairedDevicesChanged();
}

void MdrController::connectToDevice(const QString &address)
{
    closeDevice();

    m_address = address;
    m_serviceIndex = 0;
    setStatus(tr("Connecting…"));
    setState(Connecting);

    const MDRResult result = mdrConnectionConnect(
        m_transport->connection(), address.toUtf8().constData(), kServiceUuids[m_serviceIndex]);
    if (result != MDR_RESULT_OK && result != MDR_RESULT_INPROGRESS) {
        fail(m_transport->lastError());
        return;
    }
    m_timer->start();
}

void MdrController::disconnectDevice()
{
    closeDevice();
    setStatus(QString());
    setState(Idle);
}

void MdrController::closeDevice()
{
    m_timer->stop();
    if (m_device) {
        mdrHeadphonesDestroy(m_device);
        m_device = nullptr;
    }
    mdrConnectionDisconnect(m_transport->connection());

    m_batteries.clear();
    emit batteriesChanged();
    m_volumeAvailable = false;
    m_playbackControlAvailable = false;
    m_noiseControlAvailable = false;
    m_ambientLevelAvailable = false;
    m_listeningModeAvailable = false;
    m_listeningModes.clear();
    m_backgroundRoomAvailable = false;
    emit featuresChanged();

    /* The next device is a different one until it says otherwise; leaving the last
     * one's mode standing would make the repopulated picker send it back out. */
    m_listeningMode = MDR_LISTENING_STANDARD;
    m_backgroundRoom = MDR_ROOM_UNKNOWN;
    m_requestedListeningMode = -1;
    emit listeningChanged();

    m_volume = 0;
    m_playbackStatus = MDR_PLAYBACK_UNKNOWN;
    m_trackTitle.clear();
    m_trackAlbum.clear();
    m_trackArtist.clear();
    emit playbackChanged();
}

void MdrController::openDevice()
{
    if (mdrHeadphonesCreate(MDR_ABI_VERSION, m_transport->connection(), &m_device) != MDR_RESULT_OK) {
        fail(tr("libmdr rejected this build's ABI version"));
        return;
    }
    if (mdrHeadphonesRequestInit(m_device) != MDR_RESULT_OK) {
        fail(tr("Could not start the handshake with the device"));
        return;
    }
    setStatus(tr("Reading device capabilities…"));
    setState(Initializing);
}

/* --------------------------------------------------------------------- pump */

void MdrController::tick()
{
    if (m_device)
        pumpDevice();
    else
        pumpConnection();
}

void MdrController::pumpConnection()
{
    const MDRResult result = mdrConnectionPoll(m_transport->connection(), 0);
    switch (result) {
    case MDR_RESULT_OK:
        openDevice();
        return;
    case MDR_RESULT_INPROGRESS:
    case MDR_RESULT_ERROR_TIMEOUT:
        return;
    default:
        break;
    }

    /* A device that refuses the modern service may still be an older one, so
     * fall back to the legacy UUID before giving up. */
    if (++m_serviceIndex < kServiceUuidCount) {
        qInfo() << "[lauscher] retrying with service" << kServiceUuids[m_serviceIndex];
        mdrConnectionDisconnect(m_transport->connection());
        const MDRResult retry = mdrConnectionConnect(
            m_transport->connection(), m_address.toUtf8().constData(), kServiceUuids[m_serviceIndex]);
        if (retry == MDR_RESULT_OK || retry == MDR_RESULT_INPROGRESS)
            return;
    }
    fail(m_transport->lastError());
}

void MdrController::pumpDevice()
{
    MDREvent event = MDR_EVENT_NONE;
    if (mdrHeadphonesPoll(m_device, &event) != MDR_RESULT_OK) {
        const QString reason = textOf(MDR_TEXT_LAST_ERROR);
        fail(reason.isEmpty() ? tr("The device disconnected") : reason);
        return;
    }

    switch (event) {
    case MDR_EVENT_INITIALIZE_COMPLETE:
        /* Capabilities are known; ask for the values that are not pushed to us. */
        if (mdrHeadphonesRequestFetch(m_device) != MDR_RESULT_OK) {
            fail(tr("Could not read the device state"));
            return;
        }
        refreshFeatures();
        refreshIdentity();
        setStatus(QString());
        setState(Ready);
        break;
    case MDR_EVENT_SYNC_COMPLETE:
        refreshAll();
        break;
    case MDR_EVENT_IDENTITY_CHANGED:
        refreshIdentity();
        break;
    case MDR_EVENT_BATTERY_CHANGED:
        refreshBatteries();
        break;
    /* Also arrives unprompted: the headset mirrors the volume the phone sets, and
     * its own touch controls change it too. */
    case MDR_EVENT_PLAYBACK_CHANGED:
        refreshPlayback();
        break;
    case MDR_EVENT_NOISE_CONTROL_CHANGED:
        refreshNoiseControl();
        break;
    case MDR_EVENT_LISTENING_MODE_CHANGED:
        refreshListening();
        break;
    case MDR_EVENT_APPLY_COMPLETE:
        refreshPlayback();
        refreshNoiseControl();
        refreshListening();
        break;
    default:
        break;
    }

    /* Setters only stage values; this is where they reach the device. */
    if (mdrHeadphonesIsReady(m_device) && mdrHeadphonesIsDirty(m_device) &&
        mdrHeadphonesRequestCommit(m_device) != MDR_RESULT_OK)
        fail(tr("Could not apply the change"));

    /* A listening mode the device never came back on has to be given up on here: the
     * events that would notice have already been suppressed, and a device that is not
     * going to answer sends nothing more to trigger another read. */
    if (m_requestedListeningMode >= 0 &&
        m_listeningRequestAge.elapsed() >= kListeningSettleMs)
        refreshListening();
}

/* ------------------------------------------------------------------ getters */

QString MdrController::textOf(MDRText text, uint32_t index) const
{
    if (!m_device)
        return QString();

    uint32_t size = 0;
    if (mdrHeadphonesGetText(m_device, text, index, nullptr, &size) != MDR_RESULT_OK || size == 0)
        return QString();

    QByteArray buffer(int(size), '\0');
    if (mdrHeadphonesGetText(m_device, text, index, buffer.data(), &size) != MDR_RESULT_OK)
        return QString();

    return QString::fromUtf8(buffer.constData());
}

bool MdrController::featureAvailable(MDRFeature feature) const
{
    MDRFeatureAvailability availability = MDR_AVAILABILITY_UNKNOWN;
    return m_device &&
           mdrHeadphonesGetFeature(m_device, feature, &availability) == MDR_RESULT_OK &&
           availability == MDR_AVAILABILITY_AVAILABLE;
}

void MdrController::refreshIdentity()
{
    m_deviceName = textOf(MDR_TEXT_MODEL_NAME);
    m_firmwareVersion = textOf(MDR_TEXT_FIRMWARE_VERSION);
    m_serialNumber = textOf(MDR_TEXT_UNIQUE_ID);

    MDRModel model;
    memset(&model, 0, sizeof(model));
    if (m_device && mdrHeadphonesGetModel(m_device, &model) == MDR_RESULT_OK) {
        m_codec = codecName(model.audio_codec);
        m_protocolVersion = int(model.protocol_version);
    }
    emit identityChanged();
}

void MdrController::refreshBatteries()
{
    QVariantList batteries;

    /* Four is the ABI's ceiling: main, left, right, case. */
    MDRBattery values[4];
    uint32_t count = 4;
    if (m_device && mdrHeadphonesGetBatteries(m_device, values, &count) == MDR_RESULT_OK) {
        for (uint32_t i = 0; i < count; ++i) {
            if (!values[i].present)
                continue;
            QVariantMap entry;
            entry.insert(QStringLiteral("part"), int(values[i].part));
            entry.insert(QStringLiteral("name"), batteryPartName(MDRBatteryPart(values[i].part)));
            entry.insert(QStringLiteral("level"), int(values[i].level_percent));
            entry.insert(QStringLiteral("charging"),
                         values[i].charging == MDR_CHARGING_YES ||
                             values[i].charging == MDR_CHARGING_COMPLETE);
            entry.insert(QStringLiteral("chargingComplete"),
                         values[i].charging == MDR_CHARGING_COMPLETE);
            batteries.append(entry);
        }
    }

    if (batteries == m_batteries)
        return;
    m_batteries = batteries;
    emit batteriesChanged();
}

void MdrController::refreshFeatures()
{
    m_volumeAvailable = featureAvailable(MDR_FEATURE_PLAYBACK_VOLUME);
    m_playbackControlAvailable = featureAvailable(MDR_FEATURE_PLAYBACK_CONTROL);
    m_noiseControlAvailable = featureAvailable(MDR_FEATURE_NOISE_CANCELLING) ||
                              featureAvailable(MDR_FEATURE_AMBIENT_SOUND);
    m_ambientLevelAvailable = featureAvailable(MDR_FEATURE_AMBIENT_SOUND);

    /* MDR_FEATURE_LISTENING_MODE only says the device groups these into one
     * exclusive setting; each mode is advertised separately, so the picker offers
     * whichever subset this device actually has. Standard is every mode turned
     * off, which makes it reachable whenever any of them exists. */
    const bool backgroundMusic = featureAvailable(MDR_FEATURE_LISTENING_BACKGROUND_MUSIC);
    QVariantList modes;
    if (backgroundMusic)
        modes.append(int(MDR_LISTENING_BACKGROUND_MUSIC));
    if (featureAvailable(MDR_FEATURE_LISTENING_CINEMA))
        modes.append(int(MDR_LISTENING_CINEMA));
    if (featureAvailable(MDR_FEATURE_LISTENING_VOICE_BOOST))
        modes.append(int(MDR_LISTENING_VOICE_BOOST));
    if (featureAvailable(MDR_FEATURE_LISTENING_SOUND_LEAKAGE_REDUCTION))
        modes.append(int(MDR_LISTENING_SOUND_LEAKAGE_REDUCTION));

    /* A group with nothing in it would be a picker offering only Standard. */
    m_listeningModeAvailable = featureAvailable(MDR_FEATURE_LISTENING_MODE) && !modes.isEmpty();
    if (m_listeningModeAvailable)
        modes.prepend(int(MDR_LISTENING_STANDARD));
    else
        modes.clear();
    m_listeningModes = modes;
    m_backgroundRoomAvailable = m_listeningModeAvailable && backgroundMusic;
    emit featuresChanged();
}

void MdrController::refreshPlayback()
{
    MDRPlayback playback;
    memset(&playback, 0, sizeof(playback));
    if (!m_device || mdrHeadphonesGetPlayback(m_device, &playback) != MDR_RESULT_OK)
        return;

    /* One event covers all of it: volume, play/pause and the track names each
     * report MDR_EVENT_PLAYBACK_CHANGED. The names are only ever as good as what
     * the phone pushes to the headset, so they are routinely empty. */
    const QString title = textOf(MDR_TEXT_TRACK_TITLE);
    const QString album = textOf(MDR_TEXT_TRACK_ALBUM);
    const QString artist = textOf(MDR_TEXT_TRACK_ARTIST);

    const bool changed = m_volume != int(playback.volume) ||
                         m_playbackStatus != int(playback.status) ||
                         m_trackTitle != title || m_trackAlbum != album ||
                         m_trackArtist != artist;
    m_volume = int(playback.volume);
    m_playbackStatus = int(playback.status);
    m_trackTitle = title;
    m_trackAlbum = album;
    m_trackArtist = artist;
    if (changed)
        emit playbackChanged();
}

void MdrController::refreshNoiseControl()
{
    MDRNoiseControl noise;
    memset(&noise, 0, sizeof(noise));
    if (!m_device || mdrHeadphonesGetNoiseControl(m_device, &noise) != MDR_RESULT_OK)
        return;

    const bool changed = m_noiseMode != int(noise.mode) ||
                         m_ambientLevel != int(noise.ambient_level) ||
                         m_focusOnVoice != bool(noise.focus_on_voice);
    m_noiseMode = int(noise.mode);
    m_ambientLevel = int(noise.ambient_level);
    m_focusOnVoice = noise.focus_on_voice != MDR_FALSE;
    if (changed)
        emit noiseControlChanged();
}

void MdrController::refreshListening()
{
    MDRListening listening;
    memset(&listening, 0, sizeof(listening));
    if (!m_device || mdrHeadphonesGetListening(m_device, &listening) != MDR_RESULT_OK)
        return;

    /* The device does not move between two listening modes in one step: it reports
     * every mode off first and the new one 0.2-0.4 s later, which is visible in the
     * WF-LC900 listening capture upstream. Nothing marks that reading as transitional
     * - every mode off is exactly what Standard looks like - so a mode the user asked
     * for holds against it until the window is up.
     *
     * The window is what ends the hold, never a reading that agrees with the request:
     * libmdr takes a staged value as current the moment the change is sent, so it
     * reports the requested mode straight away and the device's confirmation cannot be
     * told from our own echo of the request. A reading of some other mode can end it,
     * though - that is the device saying it did something we did not ask for. */
    if (m_requestedListeningMode >= 0) {
        if (m_listeningRequestAge.elapsed() >= kListeningSettleMs)
            m_requestedListeningMode = -1;
        else if (listening.mode == MDR_LISTENING_STANDARD &&
                 m_requestedListeningMode != MDR_LISTENING_STANDARD)
            return;
        else if (int(listening.mode) != m_requestedListeningMode)
            m_requestedListeningMode = -1;
    }

    if (m_listeningMode == int(listening.mode) &&
        m_backgroundRoom == int(listening.background_room))
        return;
    m_listeningMode = int(listening.mode);
    m_backgroundRoom = int(listening.background_room);
    emit listeningChanged();
}

void MdrController::refreshAll()
{
    refreshIdentity();
    refreshBatteries();
    refreshPlayback();
    refreshNoiseControl();
    refreshListening();
}

int MdrController::maximumVolume() const
{
    return kMaxVolume;
}

/* Both the slider and the cover say the volume in percent; the device counts in
 * 31 steps, so the two scales meet here and nowhere else. */
int MdrController::volumeToPercent(int volume) const
{
    return qRound(qBound(0, volume, kMaxVolume) * 100.0 / kMaxVolume);
}

/* ------------------------------------------------------------------ setters */

void MdrController::setVolume(int volume)
{
    MDRPlayback playback;
    memset(&playback, 0, sizeof(playback));
    if (!m_device || mdrHeadphonesGetPlayback(m_device, &playback) != MDR_RESULT_OK)
        return;

    /* The status has to go back out as it came in: libmdr refuses a playback struct
     * that asks for a state change, and this call is only about the volume. */
    playback.volume = uint8_t(qBound(0, volume, kMaxVolume));
    if (mdrHeadphonesSetPlayback(m_device, &playback) != MDR_RESULT_OK)
        return;

    m_volume = playback.volume;
    emit playbackChanged();
}

void MdrController::sendPlaybackAction(MDRPlaybackAction action)
{
    MDRPlaybackCommand command;
    memset(&command, 0, sizeof(command));
    command.action = action;
    if (!m_device || mdrHeadphonesPlayback(m_device, &command) != MDR_RESULT_OK)
        return;

    /* Deliberately not reflected locally the way the settings are. Whether the
     * music actually starts is up to the phone's media player, not the headset,
     * so the button follows the device's answer rather than predicting it. */
}

void MdrController::play()
{
    sendPlaybackAction(MDR_PLAYBACK_PLAY);
}

void MdrController::pause()
{
    sendPlaybackAction(MDR_PLAYBACK_PAUSE);
}

void MdrController::nextTrack()
{
    sendPlaybackAction(MDR_PLAYBACK_NEXT);
}

void MdrController::previousTrack()
{
    sendPlaybackAction(MDR_PLAYBACK_PREVIOUS);
}

void MdrController::setNoiseMode(int mode)
{
    MDRNoiseControl noise;
    memset(&noise, 0, sizeof(noise));
    if (!m_device || mdrHeadphonesGetNoiseControl(m_device, &noise) != MDR_RESULT_OK)
        return;

    noise.mode = MDRNoiseMode(mode);
    if (mdrHeadphonesSetNoiseControl(m_device, &noise) != MDR_RESULT_OK)
        return;

    /* Reflect the request immediately; the device confirms it via
     * MDR_EVENT_NOISE_CONTROL_CHANGED once the commit lands. */
    m_noiseMode = mode;
    emit noiseControlChanged();
}

void MdrController::setAmbientLevel(int level)
{
    MDRNoiseControl noise;
    memset(&noise, 0, sizeof(noise));
    if (!m_device || mdrHeadphonesGetNoiseControl(m_device, &noise) != MDR_RESULT_OK)
        return;

    noise.ambient_level = uint8_t(qBound(0, level, 20));
    if (mdrHeadphonesSetNoiseControl(m_device, &noise) != MDR_RESULT_OK)
        return;

    m_ambientLevel = noise.ambient_level;
    emit noiseControlChanged();
}

void MdrController::setFocusOnVoice(bool enabled)
{
    MDRNoiseControl noise;
    memset(&noise, 0, sizeof(noise));
    if (!m_device || mdrHeadphonesGetNoiseControl(m_device, &noise) != MDR_RESULT_OK)
        return;

    noise.focus_on_voice = enabled ? MDR_TRUE : MDR_FALSE;
    if (mdrHeadphonesSetNoiseControl(m_device, &noise) != MDR_RESULT_OK)
        return;

    m_focusOnVoice = enabled;
    emit noiseControlChanged();
}

void MdrController::setListeningMode(int mode)
{
    MDRListening listening;
    memset(&listening, 0, sizeof(listening));
    if (!m_device || mdrHeadphonesGetListening(m_device, &listening) != MDR_RESULT_OK)
        return;

    listening.mode = MDRListeningMode(mode);
    /* Background music always travels with a distance and libmdr rejects the pair
     * without one, which a device that reports its room size as out of range would
     * otherwise turn into a control that silently does nothing. */
    if (listening.mode == MDR_LISTENING_BACKGROUND_MUSIC &&
        listening.background_room == MDR_ROOM_UNKNOWN)
        listening.background_room = MDR_ROOM_SMALL;

    if (mdrHeadphonesSetListening(m_device, &listening) != MDR_RESULT_OK) {
        /* The picker has already moved, so restate what is actually active. */
        emit listeningChanged();
        return;
    }

    /* Reflect the request immediately; the device confirms it via
     * MDR_EVENT_LISTENING_MODE_CHANGED once the commit lands, and until then
     * refreshListening() keeps it from being undone by the device's way of getting
     * there. */
    m_requestedListeningMode = mode;
    m_listeningRequestAge.start();
    m_listeningMode = mode;
    m_backgroundRoom = int(listening.background_room);
    emit listeningChanged();
}

void MdrController::setBackgroundRoom(int room)
{
    MDRListening listening;
    memset(&listening, 0, sizeof(listening));
    if (!m_device || mdrHeadphonesGetListening(m_device, &listening) != MDR_RESULT_OK)
        return;

    listening.background_room = MDRRoomSize(room);
    if (mdrHeadphonesSetListening(m_device, &listening) != MDR_RESULT_OK) {
        emit listeningChanged();
        return;
    }

    m_backgroundRoom = room;
    emit listeningChanged();
}

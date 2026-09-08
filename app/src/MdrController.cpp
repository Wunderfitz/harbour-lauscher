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
 * so we try them in turn and hand mdrHeadphonesCreate() the family that goes with
 * the one that answered. LinkBuds Clip is a V2 device and answers on the first. */
struct MdrService {
    const char *uuid;
    MDRProtocolVersion protocol;
};
const MdrService kServices[] = {
    { MDR_SERVICE_UUID_XM5, MDR_PROTOCOL_V2 },
    { MDR_SERVICE_UUID_LEGACY, MDR_PROTOCOL_V1 }
};
const int kServiceCount = int(sizeof(kServices) / sizeof(kServices[0]));

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
        m_transport->connection(), address.toUtf8().constData(), kServices[m_serviceIndex].uuid);
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
    m_equalizerAvailable = false;
    m_multipointAvailable = false;
    m_sourceSwitchingAvailable = false;
    emit featuresChanged();

    /* Both flags read true until a device says otherwise, so the next one starts
     * where a fresh MDREqualizer would rather than under this one's answer. */
    m_equalizerPresets.clear();
    emit equalizerPresetsChanged();

    m_equalizerUsable = true;
    m_equalizerPreset = MDR_EQ_OFF;
    m_equalizerBands.clear();
    m_clearBassAvailable = false;
    m_clearBass = 0;
    emit equalizerChanged();

    m_multipointDevices.clear();
    m_sourceSwitchingEnabled = true;
    m_multipointMessage.clear();
    emit multipointChanged();

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
    if (mdrHeadphonesCreate(MDR_ABI_VERSION, m_transport->connection(),
                            kServices[m_serviceIndex].protocol, &m_device) != MDR_RESULT_OK) {
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
    if (++m_serviceIndex < kServiceCount) {
        qInfo() << "[lauscher] retrying with service" << kServices[m_serviceIndex].uuid;
        mdrConnectionDisconnect(m_transport->connection());
        const MDRResult retry = mdrConnectionConnect(
            m_transport->connection(), m_address.toUtf8().constData(), kServices[m_serviceIndex].uuid);
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
        if (mdrHeadphonesRequestSync(m_device) != MDR_RESULT_OK) {
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
    /* Covers the preset, the band steps, clear bass and whether the device will act
     * on any of them, so one refresh reads the lot. It also arrives unprompted: the
     * device turns the equalizer off when a listening mode is switched on. */
    case MDR_EVENT_EQUALIZER_CHANGED:
        refreshEqualizer();
        break;
    /* A notification that carries only its discriminator - V1 announces the track
     * names this way. The values follow a sync, so ask for one; a device that is
     * still busy with something else answers MDR_RESULT_INPROGRESS, and the next
     * notification asks again. */
    case MDR_EVENT_NEED_SYNC:
        mdrHeadphonesRequestSync(m_device);
        break;
    /* Covers the whole multipoint area: the device list, which of them holds
     * playback, and whether the headset may move it by itself. */
    case MDR_EVENT_PAIRED_DEVICES_CHANGED:
        refreshMultipoint();
        break;
    case MDR_EVENT_APPLY_COMPLETE:
        refreshPlayback();
        refreshNoiseControl();
        refreshListening();
        refreshEqualizer();
        refreshMultipoint();
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

    m_equalizerAvailable = featureAvailable(MDR_FEATURE_EQUALIZER);

    /* Two separate things: the headset keeping a list of the devices it is paired
     * with, and it letting playback be pinned to one of them. A device can have
     * the first without the second. */
    m_multipointAvailable = featureAvailable(MDR_FEATURE_PAIRED_DEVICE_MANAGEMENT);
    m_sourceSwitchingAvailable = featureAvailable(MDR_FEATURE_SOURCE_SWITCH_CONTROL);
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

/* Preset, band steps, clear bass and whether the device will act on them at all -
 * MDR_EVENT_EQUALIZER_CHANGED reports every one of them, so they are read together.
 * The band count is the device's: five bands step +-10 with a clear-bass control
 * beside them, ten step +-6 with none, and a preset-only device reports no bands. */
void MdrController::refreshEqualizer()
{
    MDREqualizer equalizer;
    memset(&equalizer, 0, sizeof(equalizer));
    if (!m_device || mdrHeadphonesGetEqualizer(m_device, &equalizer) != MDR_RESULT_OK)
        return;

    QVariantList bands;
    if (equalizer.band_count > 0) {
        QVector<int8_t> values;
        values.resize(int(equalizer.band_count));
        uint32_t count = equalizer.band_count;
        if (mdrHeadphonesGetEqualizerBands(m_device, values.data(), &count) == MDR_RESULT_OK)
            for (uint32_t i = 0; i < count; ++i)
                bands.append(int(values[int(i)]));
    }

    /* The list is answered by the same capability the device reports here, and it can
     * arrive at any point - libmdr raises MDR_EVENT_EQUALIZER_CHANGED for it like everything
     * else about the equalizer - so it is read here rather than once with the features. */
    const QVariantList presets = equalizerPresetList();
    if (presets != m_equalizerPresets) {
        m_equalizerPresets = presets;
        emit equalizerPresetsChanged();
    }

    const bool usable = equalizer.available != MDR_FALSE;
    /* Only the five-band layout carries clear bass; libmdr reports 0 for the other. */
    const bool clearBassAvailable = bands.size() == 5;

    if (m_equalizerUsable == usable && m_equalizerPreset == int(equalizer.preset) &&
        m_equalizerBands == bands && m_clearBassAvailable == clearBassAvailable &&
        m_clearBass == int(equalizer.clear_bass))
        return;
    m_equalizerUsable = usable;
    m_equalizerPreset = int(equalizer.preset);
    m_equalizerBands = bands;
    m_clearBassAvailable = clearBassAvailable;
    m_clearBass = int(equalizer.clear_bass);
    emit equalizerChanged();
}

/* The headset's own view of what it is paired with: names, which of them are
 * connected, and which one currently gets the audio. All of it arrives on
 * MDR_EVENT_PAIRED_DEVICES_CHANGED, including the automatic-switching flag,
 * which is why one refresh covers the lot. */
void MdrController::refreshMultipoint()
{
    QVariantList devices;
    if (m_device) {
        /* Two passes: asked with no buffer, the ABI reports how many there are. */
        uint32_t count = 0;
        if (mdrHeadphonesGetPairedDevices(m_device, nullptr, &count) == MDR_RESULT_OK && count > 0) {
            /* Resized rather than constructed with the count: QVector values(int(count))
             * parses as a function declaration, not a vector. */
            QVector<MDRPairedDevice> values;
            values.resize(int(count));
            if (mdrHeadphonesGetPairedDevices(m_device, values.data(), &count) == MDR_RESULT_OK) {
                for (uint32_t i = 0; i < count; ++i) {
                    QVariantMap entry;
                    entry.insert(QStringLiteral("name"), QString::fromUtf8(values[int(i)].name));
                    entry.insert(QStringLiteral("address"),
                                 QString::fromLatin1(values[int(i)].macAddress));
                    entry.insert(QStringLiteral("connected"),
                                 values[int(i)].connected != MDR_FALSE);
                    entry.insert(QStringLiteral("playbackDevice"),
                                 values[int(i)].playback_device != MDR_FALSE);
                    devices.append(entry);
                }
            }
        }
    }

    bool switching = m_sourceSwitchingEnabled;
    MDRBoolean enabled = MDR_FALSE;
    if (m_device && mdrHeadphonesGetSourceSwitchControl(m_device, &enabled) == MDR_RESULT_OK)
        switching = enabled != MDR_FALSE;

    /* A refused request leaves the previous state standing, so without this the UI
     * would show a tap that simply did nothing. */
    QString message;
    MDRSourceSwitchControlResult result = MDR_SOURCE_SWITCH_CONTROL_SUCCESS;
    if (m_device &&
        mdrHeadphonesGetSourceSwitchControlResult(m_device, &result) == MDR_RESULT_OK)
        message = sourceSwitchMessage(result);

    if (devices == m_multipointDevices && switching == m_sourceSwitchingEnabled &&
        message == m_multipointMessage)
        return;
    m_multipointDevices = devices;
    m_sourceSwitchingEnabled = switching;
    m_multipointMessage = message;
    emit multipointChanged();
}

void MdrController::refreshAll()
{
    refreshIdentity();
    refreshBatteries();
    refreshPlayback();
    refreshNoiseControl();
    refreshListening();
    refreshEqualizer();
    refreshMultipoint();
}

int MdrController::maximumVolume() const
{
    return kMaxVolume;
}

/* The steps libmdr accepts for each layout, and it refuses anything outside them:
 * a five-band device runs +-10, a ten-band one +-6. Clear bass is +-10 either way,
 * but only the five-band layout has one at all. */
int MdrController::equalizerBandMaximum() const
{
    return m_equalizerBands.size() == 5 ? 10 : 6;
}

int MdrController::equalizerBandMinimum() const
{
    return -equalizerBandMaximum();
}

int MdrController::clearBassMaximum() const
{
    return 10;
}

int MdrController::clearBassMinimum() const
{
    return -clearBassMaximum();
}

QString MdrController::equalizerBandLabel(int index) const
{
    /* The frequencies the two layouts sit on, in the order the device reports them -
     * libmdr names them beside the band steps it unpacks. Written the way an equalizer
     * writes them, with the unit left to the k: a band is a tenth of the screen wide and
     * "16 kHz" does not fit under one. Numbers in every language, so nothing to translate. */
    static const char *const kFiveBand[] = { "400", "1k", "2.5k", "6.3k", "16k" };
    static const char *const kTenBand[] = { "31", "63", "125", "250", "500",
                                            "1k", "2k", "4k", "8k", "16k" };
    if (index < 0 || index >= m_equalizerBands.size())
        return QString();
    if (m_equalizerBands.size() == 5)
        return QString::fromLatin1(kFiveBand[index]);
    if (m_equalizerBands.size() == 10)
        return QString::fromLatin1(kTenBand[index]);
    return QString();
}

/* The same frequency spelled out, for the readout above the strip. Derived from the
 * label rather than tabled a second time: the trailing k is the unit. */
QString MdrController::equalizerBandFrequency(int index) const
{
    const QString label = equalizerBandLabel(index);
    if (label.isEmpty())
        return QString();
    if (label.endsWith(QLatin1Char('k')))
        return tr("%1 kHz").arg(label.left(label.size() - 1));
    return tr("%1 Hz").arg(label);
}

/* Both of these end up on screen, so they are members and not the file-local
 * helpers they used to be: only inside the class does tr() put them in the
 * MdrController context where lupdate and the translators can reach them. */
QString MdrController::codecName(MDRAudioCodec codec) const
{
    switch (codec) {
    /* Codec names are the names of the codecs in every language. */
    case MDR_AUDIO_CODEC_SBC: return QStringLiteral("SBC");
    case MDR_AUDIO_CODEC_AAC: return QStringLiteral("AAC");
    case MDR_AUDIO_CODEC_LDAC: return QStringLiteral("LDAC");
    case MDR_AUDIO_CODEC_APTX: return QStringLiteral("aptX");
    case MDR_AUDIO_CODEC_APTX_HD: return QStringLiteral("aptX HD");
    case MDR_AUDIO_CODEC_LC3: return QStringLiteral("LC3");
    /* This one is a word, not a name. */
    case MDR_AUDIO_CODEC_OTHER: return tr("Other");
    default: return QString();
    }
}

/*
 * What the picker offers. The device's own capability list is the only account of what it
 * will accept, so it wins where there is one; unknown ids are dropped, since libmdr has no
 * way to ask for them.
 *
 * An empty list from libmdr means the device has not said - an equalizer variant whose
 * capability carries no preset list, or one that never answered - and it refuses nothing in
 * that case, so the fallback is everything the C ABI can encode for this family. V1 has no
 * Heavy, Clear, Hard, Soft, Gaming or FPS preset and refuses those outright, hence the split.
 * The family is the one mdrHeadphonesCreate() was told rather than MDRModel's reading of it,
 * because this can run before identity has been refreshed.
 *
 * The names are ours, not the device's: MDR_TEXT_EQUALIZER_PRESET_NAME carries what the
 * headset calls each preset, but only in the language it was asked for, and libmdr asks in
 * English. A translated UI is better served by its own strings.
 */
QVariantList MdrController::equalizerPresetList() const
{
    QVector<MDREqualizerPreset> presets;

    uint32_t count = 0;
    if (m_device &&
        mdrHeadphonesGetEqualizerPresets(m_device, nullptr, &count) == MDR_RESULT_OK && count > 0) {
        QVector<MDREqualizerPreset> advertised;
        advertised.resize(int(count));
        if (mdrHeadphonesGetEqualizerPresets(m_device, advertised.data(), &count) == MDR_RESULT_OK)
            for (uint32_t i = 0; i < count; ++i)
                if (advertised[int(i)] != MDR_EQ_UNKNOWN)
                    presets.append(advertised[int(i)]);
    }

    if (presets.isEmpty() && m_equalizerAvailable) {
        static const MDREqualizerPreset kCommon[] = {
            MDR_EQ_OFF, MDR_EQ_ROCK, MDR_EQ_POP, MDR_EQ_JAZZ, MDR_EQ_DANCE, MDR_EQ_EDM,
            MDR_EQ_R_AND_B_HIP_HOP, MDR_EQ_ACOUSTIC, MDR_EQ_BRIGHT, MDR_EQ_EXCITED,
            MDR_EQ_MELLOW, MDR_EQ_RELAXED, MDR_EQ_VOCAL, MDR_EQ_TREBLE, MDR_EQ_BASS,
            MDR_EQ_SPEECH };
        static const MDREqualizerPreset kV2Only[] = {
            MDR_EQ_HEAVY, MDR_EQ_CLEAR, MDR_EQ_HARD, MDR_EQ_SOFT, MDR_EQ_GAMING,
            MDR_EQ_FPS_1, MDR_EQ_FPS_2, MDR_EQ_FPS_3 };
        static const MDREqualizerPreset kCustom[] = {
            MDR_EQ_CUSTOM, MDR_EQ_USER_1, MDR_EQ_USER_2, MDR_EQ_USER_3, MDR_EQ_USER_4,
            MDR_EQ_USER_5 };

        for (size_t i = 0; i < sizeof(kCommon) / sizeof(kCommon[0]); ++i)
            presets.append(kCommon[i]);
        if (kServices[m_serviceIndex].protocol == MDR_PROTOCOL_V2)
            for (size_t i = 0; i < sizeof(kV2Only) / sizeof(kV2Only[0]); ++i)
                presets.append(kV2Only[i]);
        for (size_t i = 0; i < sizeof(kCustom) / sizeof(kCustom[0]); ++i)
            presets.append(kCustom[i]);
    }

    QVariantList list;
    for (int i = 0; i < presets.size(); ++i) {
        QVariantMap entry;
        entry.insert(QStringLiteral("preset"), int(presets.at(i)));
        entry.insert(QStringLiteral("name"), equalizerPresetName(presets.at(i)));
        list.append(entry);
    }
    return list;
}

/* A member for the same reason as the two below: tr() only puts these in the
 * MdrController context from inside the class. Genre names are left as they are -
 * Rock is Rock everywhere - and the rest are ordinary words. */
QString MdrController::equalizerPresetName(MDREqualizerPreset preset) const
{
    switch (preset) {
    case MDR_EQ_OFF: return tr("Off");
    case MDR_EQ_ROCK: return QStringLiteral("Rock");
    case MDR_EQ_POP: return QStringLiteral("Pop");
    case MDR_EQ_JAZZ: return QStringLiteral("Jazz");
    case MDR_EQ_DANCE: return QStringLiteral("Dance");
    case MDR_EQ_EDM: return QStringLiteral("EDM");
    case MDR_EQ_R_AND_B_HIP_HOP: return QStringLiteral("R&B / Hip-Hop");
    case MDR_EQ_ACOUSTIC: return tr("Acoustic");
    case MDR_EQ_BRIGHT: return tr("Bright");
    case MDR_EQ_EXCITED: return tr("Excited");
    case MDR_EQ_MELLOW: return tr("Mellow");
    case MDR_EQ_RELAXED: return tr("Relaxed");
    case MDR_EQ_VOCAL: return tr("Vocal");
    case MDR_EQ_TREBLE: return tr("Treble");
    case MDR_EQ_BASS: return tr("Bass");
    case MDR_EQ_SPEECH: return tr("Speech");
    case MDR_EQ_HEAVY: return tr("Heavy");
    case MDR_EQ_CLEAR: return tr("Clear");
    case MDR_EQ_HARD: return tr("Hard");
    case MDR_EQ_SOFT: return tr("Soft");
    case MDR_EQ_GAMING: return tr("Gaming");
    case MDR_EQ_FPS_1: return tr("FPS 1");
    case MDR_EQ_FPS_2: return tr("FPS 2");
    case MDR_EQ_FPS_3: return tr("FPS 3");
    case MDR_EQ_CUSTOM: return tr("Custom");
    case MDR_EQ_USER_1: return tr("User 1");
    case MDR_EQ_USER_2: return tr("User 2");
    case MDR_EQ_USER_3: return tr("User 3");
    case MDR_EQ_USER_4: return tr("User 4");
    case MDR_EQ_USER_5: return tr("User 5");
    default: return QString();
    }
}

QString MdrController::batteryPartName(MDRBatteryPart part) const
{
    switch (part) {
    case MDR_BATTERY_LEFT: return tr("Left");
    case MDR_BATTERY_RIGHT: return tr("Right");
    case MDR_BATTERY_CASE: return tr("Case");
    default: return tr("Battery");
    }
}

/* Why the headset would not move playback. A member rather than a file-local
 * helper so the strings land in the MdrController translation context with the
 * rest of the class. */
QString MdrController::sourceSwitchMessage(MDRSourceSwitchControlResult result) const
{
    switch (result) {
    case MDR_SOURCE_SWITCH_CONTROL_FAILED_ON_CALL:
        return tr("Not while a call is going on");
    case MDR_SOURCE_SWITCH_CONTROL_FAILED_NOT_CONNECTED:
        return tr("That device is not connected to the headset");
    case MDR_SOURCE_SWITCH_CONTROL_FAILED_VOICE_ASSISTANT:
        return tr("Not while the voice assistant is listening");
    case MDR_SOURCE_SWITCH_CONTROL_FAILED:
        return tr("The headset would not change the playback device");
    default:
        return QString();
    }
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

/* Every equalizer setter reads the whole struct back before changing its one field:
 * mdrHeadphonesSetEqualizer stages the preset, clear bass and DSEE together and
 * validates each of them, so the fields this call is not about have to go out as they
 * came in. Nothing here is put back on failure - the QML reads these properties
 * directly, so an unchanged property leaves the control showing the device's value. */
void MdrController::setEqualizerPreset(int preset)
{
    MDREqualizer equalizer;
    memset(&equalizer, 0, sizeof(equalizer));
    if (!m_device || mdrHeadphonesGetEqualizer(m_device, &equalizer) != MDR_RESULT_OK)
        return;

    equalizer.preset = MDREqualizerPreset(preset);
    if (mdrHeadphonesSetEqualizer(m_device, &equalizer) != MDR_RESULT_OK)
        return;

    /* Reflect the request immediately - a staged value only becomes libmdr's current
     * one when the commit goes out a tick later, so reading it back here would answer
     * with the old preset. The device's own answer follows on the next
     * MDR_EVENT_EQUALIZER_CHANGED and overrules this. */
    m_equalizerPreset = preset;
    emit equalizerChanged();
}

void MdrController::setEqualizerBand(int index, int value)
{
    /* The bands travel as one array, so the others go back out as the device last
     * reported them. libmdr takes only a full five- or ten-band set. */
    if (!m_device || index < 0 || index >= m_equalizerBands.size() ||
        (m_equalizerBands.size() != 5 && m_equalizerBands.size() != 10))
        return;

    QVector<int8_t> values;
    values.reserve(m_equalizerBands.size());
    for (int i = 0; i < m_equalizerBands.size(); ++i) {
        const int step = i == index
                             ? qBound(equalizerBandMinimum(), value, equalizerBandMaximum())
                             : m_equalizerBands.at(i).toInt();
        values.append(int8_t(step));
    }

    if (mdrHeadphonesSetEqualizerBands(m_device, values.constData(),
                                       uint32_t(values.size())) != MDR_RESULT_OK)
        return;

    m_equalizerBands[index] = int(values.at(index));
    emit equalizerChanged();
}

void MdrController::setClearBass(int value)
{
    MDREqualizer equalizer;
    memset(&equalizer, 0, sizeof(equalizer));
    if (!m_device || mdrHeadphonesGetEqualizer(m_device, &equalizer) != MDR_RESULT_OK)
        return;

    equalizer.clear_bass = int8_t(qBound(clearBassMinimum(), value, clearBassMaximum()));
    if (mdrHeadphonesSetEqualizer(m_device, &equalizer) != MDR_RESULT_OK)
        return;

    m_clearBass = int(equalizer.clear_bass);
    emit equalizerChanged();
}

/* Connect, disconnect and "play here" are all one staged MAC address in libmdr,
 * which the next commit turns into the matching frame. The address has to be the
 * 17-character form the headset reported; anything else is refused there. */
void MdrController::sendPairedDeviceCommand(MDRPairedDeviceCommand command,
                                            const QString &address)
{
    const QByteArray id = address.toLatin1();

    MDRPairedDeviceAction action;
    memset(&action, 0, sizeof(action));
    action.command = command;
    action.device_id = id.constData();
    action.device_id_size = uint32_t(id.size());

    /* Nothing is reflected locally on purpose. The headset answers every one of
     * these with a paired-device notification carrying what it actually did -
     * including a refusal, which is the whole point of multipointMessage - so
     * predicting the outcome here would only get in the way of the truth. */
    if (!m_device || mdrHeadphonesSetPairedDevice(m_device, &action) != MDR_RESULT_OK)
        return;
}

void MdrController::selectPlaybackDevice(const QString &address)
{
    sendPairedDeviceCommand(MDR_PAIRED_DEVICE_SELECT_PLAYBACK, address);
}

void MdrController::connectPairedDevice(const QString &address)
{
    sendPairedDeviceCommand(MDR_PAIRED_DEVICE_CONNECT, address);
}

void MdrController::disconnectPairedDevice(const QString &address)
{
    sendPairedDeviceCommand(MDR_PAIRED_DEVICE_DISCONNECT, address);
}

void MdrController::setSourceSwitchingEnabled(bool enabled)
{
    if (!m_device ||
        mdrHeadphonesSetSourceSwitchControl(m_device, enabled ? MDR_TRUE : MDR_FALSE) !=
            MDR_RESULT_OK) {
        /* The switch has already moved; put it back where the device has it. */
        emit multipointChanged();
        return;
    }

    /* Staging this clears the last refusal in libmdr, so clear ours with it. */
    m_multipointMessage.clear();
    m_sourceSwitchingEnabled = enabled;
    emit multipointChanged();
}

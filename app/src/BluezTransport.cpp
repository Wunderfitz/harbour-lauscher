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

#include "BluezTransport.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mdr-c/Base.h>

namespace {

const char *const kBluezService = "org.bluez";
const char *const kProfileManagerPath = "/org/bluez";
const char *const kProfileManagerIface = "org.bluez.ProfileManager1";
const char *const kDeviceIface = "org.bluez.Device1";
const char *const kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";
const char *const kPropertiesIface = "org.freedesktop.DBus.Properties";

/* Qt hands an "as" property straight back as a QStringList, but a variant that
 * came through a nested demarshalling arrives as a QDBusArgument instead. */
QStringList stringListProperty(const QVariantMap &props, const QString &key)
{
    const QVariant value = props.value(key);
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        QStringList list;
        value.value<QDBusArgument>() >> list;
        return list;
    }
    return value.toStringList();
}

/* Only devices this app can actually drive belong in the picker, and the MDR
 * service UUID in bluetoothd's cached SDP records answers that exactly: it is
 * the RFCOMM channel the app connects on.
 *
 * The test has to be this and nothing else. bluetoothd builds a service object
 * per device from that same cached list when the Profile1 is registered, so a
 * device the list does not cover has no service for ConnectProfile to reach -
 * it answers br-connection-not-supported however headset-shaped the device
 * looks. Guessing from the class of device (Icon audio-headset) therefore
 * offers entries that cannot connect, which is the opposite of the point. */
bool speaksMdr(const QStringList &uuids)
{
    for (int i = 0; i < uuids.size(); ++i) {
        if (uuids.at(i).compare(QLatin1String(MDR_SERVICE_UUID_XM5), Qt::CaseInsensitive) == 0
            || uuids.at(i).compare(QLatin1String(MDR_SERVICE_UUID_LEGACY), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/* BlueZ's own reading of the class of device: audio-headset covers the
 * wearable-headset and hands-free minor classes, audio-headphones the
 * headphones one. Not a criterion for listing - only for saying, in the log,
 * why a device that plainly is a headset was left out. */
bool looksLikeHeadset(const QVariantMap &props)
{
    const QString icon = props.value(QStringLiteral("Icon")).toString();
    return icon == QLatin1String("audio-headset")
        || icon == QLatin1String("audio-headphones");
}

/* a{oa{sa{sv}}} as returned by GetManagedObjects. */
typedef QMap<QString, QVariantMap> InterfaceList;
typedef QMap<QDBusObjectPath, InterfaceList> ManagedObjectList;

void registerBluezTypes()
{
    static bool done = false;
    if (done)
        return;
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjectList>();
    done = true;
}

ManagedObjectList managedObjects()
{
    registerBluezTypes();
    QDBusMessage call = QDBusMessage::createMethodCall(
        kBluezService, QStringLiteral("/"), kObjectManagerIface,
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return ManagedObjectList();

    ManagedObjectList objects;
    const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
    arg >> objects;
    return objects;
}

} // namespace

/* ------------------------------------------------------------------ adaptor */

Profile1Adaptor::Profile1Adaptor(BluezTransport *transport)
    : QDBusAbstractAdaptor(transport), m_transport(transport)
{
    setAutoRelaySignals(false);
}

/* The Profile1 object is registered for a UUID, not for a device, so bluetoothd
 * offers it every device that connects on that UUID - including a second headset,
 * or the one this app has just walked away from waking up again. Taking whatever
 * arrives would hand the session a socket to a headset nobody asked for, and the
 * app would go on naming the one it thinks it called. So the path is checked, and
 * anything else is refused the way BlueZ expects a profile to refuse. */
void Profile1Adaptor::NewConnection(const QDBusObjectPath &device, const QDBusUnixFileDescriptor &fd,
                                    const QVariantMap &properties)
{
    Q_UNUSED(properties)
    if (!m_transport->ownsDevicePath(device.path())) {
        qWarning() << "[lauscher] bluez: refusing a connection from" << device.path()
                   << "- not the device we asked for";
        sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                       QStringLiteral("Not the device this profile asked for"));
        return;
    }
    m_transport->handleNewConnection(fd);
}

void Profile1Adaptor::RequestDisconnection(const QDBusObjectPath &device)
{
    /* Same reasoning as NewConnection: another headset's profile going away is not
     * this session ending, and acting on it would drop a link that is up. */
    if (!m_transport->ownsDevicePath(device.path())) {
        qInfo() << "[lauscher] bluez: ignoring a disconnection request for" << device.path();
        return;
    }
    m_transport->handleRequestDisconnection();
}

void Profile1Adaptor::Release()
{
    m_transport->handleRelease();
}

/* ---------------------------------------------------------------- transport */

BluezTransport::BluezTransport(QObject *parent)
    : QObject(parent)
{
    m_conn.user = this;
    m_conn.connect = &BluezTransport::vtConnect;
    m_conn.disconnect = &BluezTransport::vtDisconnect;
    m_conn.recv = &BluezTransport::vtRecv;
    m_conn.send = &BluezTransport::vtSend;
    m_conn.poll = &BluezTransport::vtPoll;
    m_conn.getDevicesList = &BluezTransport::vtGetDevicesList;
    m_conn.freeDevicesList = &BluezTransport::vtFreeDevicesList;
    m_conn.getLastError = &BluezTransport::vtGetLastError;

    m_lastError = QByteArrayLiteral("");

    new Profile1Adaptor(this);
    /* One profile object per process instance; the pid keeps it unique enough. */
    m_profilePath = QStringLiteral("/de/ygriega/lauscher/profile%1")
                        .arg(QCoreApplication::applicationPid());
}

BluezTransport::~BluezTransport()
{
    doDisconnect();
    unwatchDevice();
    unregisterProfile();
}

void BluezTransport::setError(const QString &message)
{
    m_lastError = message.toUtf8();
    qWarning() << "[lauscher] bluez:" << message;
}

/* ------------------------------------------------------------- enumeration */

QVariantList BluezTransport::pairedDevices()
{
    QVariantList result;
    m_addressToPath.clear();

    const ManagedObjectList objects = managedObjects();
    for (ManagedObjectList::const_iterator it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const InterfaceList &ifaces = it.value();
        if (!ifaces.contains(QString::fromLatin1(kDeviceIface)))
            continue;
        const QVariantMap props = ifaces.value(QString::fromLatin1(kDeviceIface));
        if (!props.value(QStringLiteral("Paired")).toBool())
            continue;

        const QString address = props.value(QStringLiteral("Address")).toString();
        if (address.isEmpty())
            continue;
        const QStringList uuids = stringListProperty(props, QStringLiteral("UUIDs"));
        if (!speaksMdr(uuids)) {
            /* A headset bluetoothd has no MDR record for is the one case worth
             * a word: from the picker it is simply absent, and this says why. */
            if (looksLikeHeadset(props))
                qInfo() << "[lauscher] bluez: skipping" << address
                        << "- none of its" << uuids.size()
                        << "services is the MDR profile";
            continue;
        }
        m_addressToPath.insert(address.toUpper(), it.key().path());

        /* BlueZ makes up an Alias for a device that never told it a name, and
         * what it makes up is the address with dashes for colons. The UI does
         * not show addresses, so that spelling of one is dropped here rather
         * than being passed off as a name. */
        QString name = props.value(QStringLiteral("Alias"),
                                   props.value(QStringLiteral("Name"))).toString();
        if (name.compare(QString(address).replace(QLatin1Char(':'), QLatin1Char('-')),
                         Qt::CaseInsensitive) == 0)
            name.clear();

        QVariantMap entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("address"), address);
        entry.insert(QStringLiteral("connected"), props.value(QStringLiteral("Connected")).toBool());
        result.append(entry);
    }
    return result;
}

QString BluezTransport::devicePathForAddress(const QString &macAddress)
{
    const QString key = macAddress.toUpper();
    if (m_addressToPath.contains(key))
        return m_addressToPath.value(key);
    pairedDevices();
    return m_addressToPath.value(key);
}

/* ---------------------------------------------------------------- watching */

/* Whether the headset is there at all is BlueZ's business, not the RFCOMM
 * channel's, and bluetoothd says so on its own: a device that connects to the
 * phone again - buds taken out of the case, headphones switched on - gets its
 * Device1 properties updated. Subscribing to that one device's PropertiesChanged
 * is cheaper and quicker than asking BlueZ on a timer, and it is the same signal
 * the Bluetooth settings react to. */
void BluezTransport::watchDevice(const QString &macAddress)
{
    const QString path = devicePathForAddress(macAddress);
    if (path == m_watchedPath)
        return;
    unwatchDevice();
    if (path.isEmpty())
        return;

    if (!QDBusConnection::systemBus().connect(
            QString::fromLatin1(kBluezService), path, QString::fromLatin1(kPropertiesIface),
            QStringLiteral("PropertiesChanged"), this,
            SLOT(onWatchedDeviceChanged(QString,QVariantMap,QStringList)))) {
        qWarning() << "[lauscher] bluez: could not watch" << path;
        return;
    }
    m_watchedPath = path;
}

void BluezTransport::unwatchDevice()
{
    if (m_watchedPath.isEmpty())
        return;
    QDBusConnection::systemBus().disconnect(
        QString::fromLatin1(kBluezService), m_watchedPath, QString::fromLatin1(kPropertiesIface),
        QStringLiteral("PropertiesChanged"), this,
        SLOT(onWatchedDeviceChanged(QString,QVariantMap,QStringList)));
    m_watchedPath.clear();
}

/* Whether it is worth reaching for the headset at all. Connected is the property
 * that answers it and ServicesResolved is not: bluetoothd leaves the ACL link up
 * after a failed ConnectProfile, so a headset that is there but not offering the
 * MDR record reads as connected-but-unresolved, and that is exactly the case a
 * retry is for. Nothing to poll for here - the caller asks when it is about to
 * try. */
bool BluezTransport::isDeviceConnected(const QString &macAddress)
{
    const QString path = devicePathForAddress(macAddress);
    if (path.isEmpty())
        return false;

    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(kBluezService), path, QString::fromLatin1(kPropertiesIface),
        QStringLiteral("Get"));
    call << QString::fromLatin1(kDeviceIface) << QStringLiteral("Connected");

    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    return reply.arguments().first().value<QDBusVariant>().variant().toBool();
}

/* ServicesResolved is the property worth acting on: Connected goes true as soon
 * as the radio link is up, which is before bluetoothd has read the device's SDP
 * records back and therefore before there is an MDR record to connect on. A
 * device that reports Connected without ever resolving is still reported, since
 * the caller retries anyway and a headset held back here would never reconnect. */
void BluezTransport::onWatchedDeviceChanged(const QString &interface, const QVariantMap &changed,
                                            const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (interface != QLatin1String(kDeviceIface))
        return;
    if (changed.value(QStringLiteral("ServicesResolved")).toBool()
        || changed.value(QStringLiteral("Connected")).toBool())
        emit watchedDeviceReturned();
}

/* -------------------------------------------------------------- profile/fd */

bool BluezTransport::ensureProfileRegistered(const QString &serviceUUID)
{
    if (m_profileUuid == serviceUUID)
        return true;
    unregisterProfile();

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        setError(QStringLiteral("System bus is not available"));
        return false;
    }
    if (!bus.registerObject(m_profilePath, this)) {
        setError(QStringLiteral("Could not export the Profile1 object at %1").arg(m_profilePath));
        return false;
    }

    /* Role=client makes bluetoothd resolve the RFCOMM channel over SDP for us,
     * which is the whole reason we are not opening the socket ourselves. */
    QVariantMap options;
    options.insert(QStringLiteral("Name"), QStringLiteral("Lauscher MDR"));
    options.insert(QStringLiteral("Role"), QStringLiteral("client"));
    options.insert(QStringLiteral("AutoConnect"), false);
    options.insert(QStringLiteral("RequireAuthentication"), true);
    options.insert(QStringLiteral("RequireAuthorization"), false);

    QDBusMessage call = QDBusMessage::createMethodCall(
        kBluezService, kProfileManagerPath, kProfileManagerIface, QStringLiteral("RegisterProfile"));
    call << QVariant::fromValue(QDBusObjectPath(m_profilePath)) << serviceUUID << options;

    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        bus.unregisterObject(m_profilePath);
        setError(QStringLiteral("RegisterProfile failed: %1").arg(reply.errorMessage()));
        return false;
    }

    m_profileUuid = serviceUUID;
    return true;
}

/* The descriptor BlueZ hands over is a second reference to the same socket, not
 * a handover: bluetoothd keeps its own and watches it, so closing ours leaves the
 * RFCOMM channel up, the headset's one control session occupied, and the service
 * marked connected on BlueZ's books. The next ConnectProfile for that device is
 * then answered "Already Connected" and no NewConnection follows, because there
 * is no new channel to hand over - which is a session that can never be opened
 * again short of restarting the app. So a channel we let go of is one BlueZ has
 * to be told about.
 *
 * NoBlock, not a blocking call: BlueZ takes an external profile down by calling
 * RequestDisconnection on us and waiting for the reply, so blocking here would be
 * this process waiting on a reply to a question it cannot answer until it stops
 * waiting. The callback that follows is ignored by ownsDevicePath() - the path is
 * cleared before it can arrive - which is exactly right, since this teardown is
 * ours and not the headset going away. */
void BluezTransport::disconnectProfile(const QString &devicePath)
{
    if (devicePath.isEmpty() || m_profileUuid.isEmpty())
        return;

    QDBusMessage call = QDBusMessage::createMethodCall(
        kBluezService, devicePath, kDeviceIface, QStringLiteral("DisconnectProfile"));
    call << m_profileUuid;
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
}

void BluezTransport::unregisterProfile()
{
    if (m_profileUuid.isEmpty())
        return;

    QDBusMessage call = QDBusMessage::createMethodCall(
        kBluezService, kProfileManagerPath, kProfileManagerIface, QStringLiteral("UnregisterProfile"));
    call << QVariant::fromValue(QDBusObjectPath(m_profilePath));
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    QDBusConnection::systemBus().unregisterObject(m_profilePath);
    m_profileUuid.clear();
}

void BluezTransport::adoptSocket(int fd)
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = fd;
    /* libmdr drives everything from non-blocking recv/send plus poll(). */
    const int flags = ::fcntl(m_fd, F_GETFL, 0);
    ::fcntl(m_fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
    m_connecting = false;
    m_pendingResult = MDR_RESULT_OK;
    m_lastError = QByteArrayLiteral("Connected");
    emit socketConnected();
}

void BluezTransport::handleNewConnection(const QDBusUnixFileDescriptor &fd)
{
    /* The descriptor is only valid for as long as the QDBusUnixFileDescriptor
     * lives, and that is the lifetime of this call - so take our own copy. */
    const int borrowed = fd.fileDescriptor();
    if (borrowed < 0) {
        setError(QStringLiteral("BlueZ handed over an invalid file descriptor"));
        m_pendingResult = MDR_RESULT_ERROR_NET;
        return;
    }
    const int owned = ::dup(borrowed);
    if (owned < 0) {
        setError(QStringLiteral("dup() on the RFCOMM descriptor failed: %1")
                     .arg(QString::fromLocal8Bit(strerror(errno))));
        m_pendingResult = MDR_RESULT_ERROR_NET;
        return;
    }
    qInfo("[lauscher] bluez: RFCOMM socket established");
    adoptSocket(owned);
}

void BluezTransport::handleRequestDisconnection()
{
    const bool wasConnected = m_fd >= 0;
    if (wasConnected) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_pendingResult = MDR_RESULT_ERROR_NO_CONNECTION;

    /* Only an unsolicited drop is a failure; BlueZ also calls this while we are
     * tearing the profile down ourselves. */
    if (wasConnected) {
        setError(QStringLiteral("The device closed the connection"));
        emit linkLost();
    }
}

/* -------------------------------------------------------------- connecting */

MDRResult BluezTransport::doConnect(const QString &macAddress, const QString &serviceUUID)
{
    doDisconnect();

    if (!ensureProfileRegistered(serviceUUID))
        return MDR_RESULT_ERROR_GENERAL;

    m_devicePath = devicePathForAddress(macAddress);
    if (m_devicePath.isEmpty()) {
        setError(QStringLiteral("%1 is not a paired device").arg(macAddress));
        return MDR_RESULT_ERROR_NOT_FOUND;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        kBluezService, m_devicePath, kDeviceIface, QStringLiteral("ConnectProfile"));
    call << serviceUUID;

    /* ConnectProfile blocks for seconds while BlueZ does SDP and opens the
     * channel, so it has to be async - the reply and the NewConnection callback
     * both come back through the event loop. */
    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(call, 30000);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pending, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
                     this, &BluezTransport::onConnectProfileFinished);

    m_connecting = true;
    m_pendingResult = MDR_RESULT_OK;
    m_lastError = QByteArrayLiteral("Connecting to the device");
    return MDR_RESULT_INPROGRESS;
}

void BluezTransport::onConnectProfileFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<> reply = *watcher;
    watcher->deleteLater();

    if (!reply.isError())
        return; /* The socket arrives separately, via NewConnection. */

    /* NewConnection can land before the reply does, and a socket in hand settles
     * the question whatever the reply says. */
    if (m_fd >= 0)
        return;

    /* "Already Connected" is BlueZ saying it still holds the channel from the last
     * session - our own, most likely, since closing our copy of the descriptor does
     * not close its. There is no new channel to hand over, so no NewConnection is
     * coming and waiting for one is waiting forever. Tell BlueZ to drop it and let
     * this attempt fail: the retry a few seconds later is the one that gets a fresh
     * channel. */
    const QString message = reply.error().message();
    if (message.contains(QStringLiteral("Already Connected"), Qt::CaseInsensitive)) {
        disconnectProfile(m_devicePath);
        m_connecting = false;
        m_pendingResult = MDR_RESULT_ERROR_NET;
        /* Worded as a verdict, because that is the only way it is ever read: the
         * attempts in between keep the message the page already has. */
        setError(tr("The control channel was still open from the last session. "
                    "Lauscher has closed it; please try again."));
        return;
    }

    /* Reported through poll() rather than failed(), so the controller still
     * gets to retry on the other service UUID before giving up. */
    m_connecting = false;
    m_pendingResult = MDR_RESULT_ERROR_NET;
    qWarning() << "[lauscher] bluez: ConnectProfile failed:" << message;
    setError(explainConnectFailure(message));
}

/* BlueZ answers a failed ConnectProfile with a keyword meant for programs, and
 * putting that on screen tells the user nothing. Two of them are worth naming,
 * because both are states on the headset rather than faults here:
 *
 * br-connection-not-supported is what bluetoothd returns when its SDP search
 * for our profile came back empty ("No SDP records found" in its own log). The
 * headset publishes the MDR record only while it is properly awake - out of
 * its case, connected as an audio device - so a device that speaks MDR every
 * other day answers this one while it sits in the case. It is also what a
 * device that never spoke MDR at all would say, but the picker has already
 * ruled those out.
 *
 * br-connection-page-timeout is the headset not answering the radio at all. */
QString BluezTransport::explainConnectFailure(const QString &message) const
{
    if (message.contains(QStringLiteral("not-supported"))
        || message.contains(QStringLiteral("profile-unavailable"))
        || message.contains(QStringLiteral("sdp-search"))
        || message.contains(QStringLiteral("No more profiles"), Qt::CaseInsensitive))
        return tr("The headset is not offering its control channel. Take it out "
                  "of the charging case, connect it in the Bluetooth settings, "
                  "and try again.");

    if (message.contains(QStringLiteral("page-timeout"))
        || message.contains(QStringLiteral("timeout"), Qt::CaseInsensitive))
        return tr("The headset did not answer. Switch it on and keep it nearby.");

    if (message.contains(QStringLiteral("busy"))
        || message.contains(QStringLiteral("in-progress"), Qt::CaseInsensitive))
        return tr("Bluetooth is busy with this headset. Try again in a moment.");

    return tr("Could not open the control channel: %1").arg(message);
}

void BluezTransport::handleRelease()
{
    /* BlueZ dropped the registration; forget it so the next connect re-registers. */
    m_profileUuid.clear();
    handleRequestDisconnection();
}

bool BluezTransport::ownsDevicePath(const QString &path) const
{
    return !m_devicePath.isEmpty() && path == m_devicePath;
}

void BluezTransport::doDisconnect()
{
    /* An attempt still in flight counts: BlueZ may complete it after we have gone,
     * and then it holds a channel nobody adopted. */
    const bool heldChannel = m_fd >= 0 || m_connecting;

    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (heldChannel)
        disconnectProfile(m_devicePath);
    m_connecting = false;
    m_pendingResult = MDR_RESULT_OK;
    /* Nothing is ours from here until the next doConnect() names a device, which is
     * what keeps a late callback for the last one from being taken for this one. */
    m_devicePath.clear();
}

MDRResult BluezTransport::doPoll(int timeout)
{
    if (m_pendingResult != MDR_RESULT_OK)
        return m_pendingResult;

    if (m_fd < 0)
        return m_connecting ? MDR_RESULT_INPROGRESS : MDR_RESULT_ERROR_NO_CONNECTION;

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = m_fd;
    pfd.events = POLLIN | POLLOUT;

    const int res = ::poll(&pfd, 1, timeout);
    if (res < 0) {
        setError(QString::fromLocal8Bit(strerror(errno)));
        return MDR_RESULT_ERROR_NET;
    }
    if (res == 0)
        return MDR_RESULT_ERROR_TIMEOUT;
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
        setError(QStringLiteral("The device closed the connection"));
        return MDR_RESULT_ERROR_NO_CONNECTION;
    }
    return MDR_RESULT_OK;
}

/* ------------------------------------------------------------ vtable thunks */

MDRResult BluezTransport::vtConnect(void *user, const char *macAddress, const char *serviceUUID)
{
    return static_cast<BluezTransport *>(user)->doConnect(
        QString::fromUtf8(macAddress), QString::fromUtf8(serviceUUID));
}

void BluezTransport::vtDisconnect(void *user)
{
    static_cast<BluezTransport *>(user)->doDisconnect();
}

MDRResult BluezTransport::vtRecv(void *user, char *dst, int size, int *pReceived)
{
    BluezTransport *self = static_cast<BluezTransport *>(user);
    if (self->m_fd < 0)
        return MDR_RESULT_ERROR_NO_CONNECTION;

    const ssize_t received = ::recv(self->m_fd, dst, size, 0);
    if (received == 0)
        return MDR_RESULT_ERROR_NO_CONNECTION;
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return MDR_RESULT_INPROGRESS;
        self->setError(QString::fromLocal8Bit(strerror(errno)));
        return MDR_RESULT_ERROR_NET;
    }
    *pReceived = static_cast<int>(received);
    return MDR_RESULT_OK;
}

MDRResult BluezTransport::vtSend(void *user, const char *src, int size, int *pSent)
{
    BluezTransport *self = static_cast<BluezTransport *>(user);
    if (self->m_fd < 0)
        return MDR_RESULT_ERROR_NO_CONNECTION;

    const ssize_t sent = ::send(self->m_fd, src, size, MSG_NOSIGNAL);
    if (sent == 0)
        return MDR_RESULT_ERROR_NO_CONNECTION;
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return MDR_RESULT_INPROGRESS;
        self->setError(QString::fromLocal8Bit(strerror(errno)));
        return MDR_RESULT_ERROR_NET;
    }
    *pSent = static_cast<int>(sent);
    return MDR_RESULT_OK;
}

MDRResult BluezTransport::vtPoll(void *user, int timeout)
{
    return static_cast<BluezTransport *>(user)->doPoll(timeout);
}

MDRResult BluezTransport::vtGetDevicesList(void *user, MDRDeviceInfo **ppList, int *pCount)
{
    BluezTransport *self = static_cast<BluezTransport *>(user);
    const QVariantList devices = self->pairedDevices();

    *pCount = devices.size();
    *ppList = devices.isEmpty() ? nullptr : new MDRDeviceInfo[devices.size()];
    for (int i = 0; i < devices.size(); ++i) {
        const QVariantMap entry = devices.at(i).toMap();
        MDRDeviceInfo &info = (*ppList)[i];
        memset(&info, 0, sizeof(info));
        qstrncpy(info.szDeviceName, entry.value(QStringLiteral("name")).toString().toUtf8().constData(),
                 sizeof(info.szDeviceName));
        qstrncpy(info.szDeviceMacAddress,
                 entry.value(QStringLiteral("address")).toString().toUtf8().constData(),
                 sizeof(info.szDeviceMacAddress));
    }
    return MDR_RESULT_OK;
}

MDRResult BluezTransport::vtFreeDevicesList(void *user, MDRDeviceInfo **ppList)
{
    Q_UNUSED(user)
    delete[] *ppList;
    *ppList = nullptr;
    return MDR_RESULT_OK;
}

const char *BluezTransport::vtGetLastError(void *user)
{
    /* Contract: never null. m_lastError owns the storage. */
    return static_cast<BluezTransport *>(user)->m_lastError.constData();
}

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

void Profile1Adaptor::NewConnection(const QDBusObjectPath &device, const QDBusUnixFileDescriptor &fd,
                                    const QVariantMap &properties)
{
    Q_UNUSED(device)
    Q_UNUSED(properties)
    m_transport->handleNewConnection(fd);
}

void Profile1Adaptor::RequestDisconnection(const QDBusObjectPath &device)
{
    Q_UNUSED(device)
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
        m_addressToPath.insert(address.toUpper(), it.key().path());

        QVariantMap entry;
        entry.insert(QStringLiteral("name"),
                     props.value(QStringLiteral("Alias"),
                                 props.value(QStringLiteral("Name"), address)).toString());
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
        emit failed(lastError());
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

    /* "Already Connected" means the profile channel is up from an earlier run;
     * BlueZ still calls NewConnection in that case, so keep waiting. */
    const QString message = reply.error().message();
    if (message.contains(QStringLiteral("Already Connected"), Qt::CaseInsensitive))
        return;

    /* Reported through poll() rather than failed(), so the controller still
     * gets to retry on the other service UUID before giving up. */
    m_connecting = false;
    m_pendingResult = MDR_RESULT_ERROR_NET;
    setError(QStringLiteral("ConnectProfile failed: %1").arg(message));
}

void BluezTransport::handleRelease()
{
    /* BlueZ dropped the registration; forget it so the next connect re-registers. */
    m_profileUuid.clear();
    handleRequestDisconnection();
}

void BluezTransport::doDisconnect()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_connecting = false;
    m_pendingResult = MDR_RESULT_OK;
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

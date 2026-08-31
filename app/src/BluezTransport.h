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

#ifndef BLUEZTRANSPORT_H
#define BLUEZTRANSPORT_H

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariantList>

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>

#include <mdr-c/Connection.h>

class BluezTransport;
class QDBusPendingCallWatcher;

/**
 * Exports BluezTransport's Profile1 slots under the interface name bluetoothd
 * actually calls. QtDBus derives the interface from the adaptor class unless
 * told otherwise, hence the Q_CLASSINFO.
 */
class Profile1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Profile1")

public:
    explicit Profile1Adaptor(BluezTransport *transport);

public slots:
    void NewConnection(const QDBusObjectPath &device, const QDBusUnixFileDescriptor &fd,
                       const QVariantMap &properties);
    void RequestDisconnection(const QDBusObjectPath &device);
    void Release();

private:
    BluezTransport *m_transport;
};

/**
 * RFCOMM transport for libmdr built on BlueZ's D-Bus API.
 *
 * Sailfish ships neither QtBluetooth nor bluez5-libs-devel in the harbour-allowed
 * set, so the upstream Linux backend (libbluetooth SDP + raw AF_BLUETOOTH socket)
 * is not reusable here. Instead we let bluetoothd do the SDP lookup for us: we
 * export an org.bluez.Profile1 object, register it for the MDR service UUID with
 * Role=client, and ask Device1.ConnectProfile() to establish the channel. BlueZ
 * then hands the connected RFCOMM socket back through Profile1.NewConnection() as
 * a passed unix file descriptor, which is all libmdr ever needs.
 *
 * Everything below the fd is plain non-blocking read/write, matching what
 * MDRConnection's vtable expects.
 */
class BluezTransport : public QObject
{
    Q_OBJECT

public:
    explicit BluezTransport(QObject *parent = nullptr);
    ~BluezTransport() override;

    /** The vtable handed to mdrHeadphonesCreate(). Owned by this object. */
    MDRConnection *connection() { return &m_conn; }

    /** Paired devices as [{name, address}, ...], newest BlueZ state each call. */
    QVariantList pairedDevices();

    /** True once BlueZ has handed us a connected RFCOMM socket. */
    bool hasSocket() const { return m_fd >= 0; }

    QString lastError() const { return QString::fromUtf8(m_lastError); }

signals:
    void socketConnected();
    void failed(const QString &message);

public:
    /* org.bluez.Profile1 - called by bluetoothd via Profile1Adaptor, not by us. */
    void handleNewConnection(const QDBusUnixFileDescriptor &fd);
    void handleRequestDisconnection();
    void handleRelease();

private slots:
    void onConnectProfileFinished(QDBusPendingCallWatcher *watcher);

private:
    /* MDRConnection vtable thunks. */
    static MDRResult vtConnect(void *user, const char *macAddress, const char *serviceUUID);
    static void vtDisconnect(void *user);
    static MDRResult vtRecv(void *user, char *dst, int size, int *pReceived);
    static MDRResult vtSend(void *user, const char *src, int size, int *pSent);
    static MDRResult vtPoll(void *user, int timeout);
    static MDRResult vtGetDevicesList(void *user, MDRDeviceInfo **ppList, int *pCount);
    static MDRResult vtFreeDevicesList(void *user, MDRDeviceInfo **ppList);
    static const char *vtGetLastError(void *user);

    MDRResult doConnect(const QString &macAddress, const QString &serviceUUID);
    void doDisconnect();
    MDRResult doPoll(int timeout);

    bool ensureProfileRegistered(const QString &serviceUUID);
    void unregisterProfile();
    QString devicePathForAddress(const QString &macAddress);
    void setError(const QString &message);
    void adoptSocket(int fd);

    MDRConnection m_conn;
    QByteArray m_lastError;

    int m_fd = -1;
    /* Set once ConnectProfile fails or the peer hangs up; surfaced through poll(). */
    MDRResult m_pendingResult = MDR_RESULT_OK;
    bool m_connecting = false;

    QString m_profilePath;
    QString m_profileUuid;
    QString m_devicePath;
    QHash<QString, QString> m_addressToPath;
};

#endif // BLUEZTRANSPORT_H

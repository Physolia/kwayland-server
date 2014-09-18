/********************************************************************
Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) version 3, or any
later version accepted by the membership of KDE e.V. (or its
successor approved by the membership of KDE e.V.), which shall
act as a proxy defined in Section 6 of version 3 of the license.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "connection_thread.h"
// Qt
#include <QDebug>
#include <QDir>
#include <QFileSystemWatcher>
#include <QSocketNotifier>
// Wayland
#include <wayland-client-protocol.h>

namespace KWayland
{

namespace Client
{

class ConnectionThread::Private
{
public:
    Private(ConnectionThread *q);
    ~Private();
    void doInitConnection();
    void setupSocketNotifier();
    void setupSocketFileWatcher();

    wl_display *display = nullptr;
    QString socketName;
    QDir runtimeDir;
    QScopedPointer<QSocketNotifier> socketNotifier;
    QScopedPointer<QFileSystemWatcher> socketWatcher;
    bool serverDied = false;
private:
    ConnectionThread *q;
};

ConnectionThread::Private::Private(ConnectionThread *q)
    : socketName(QString::fromUtf8(qgetenv("WAYLAND_DISPLAY")))
    , runtimeDir(QString::fromUtf8(qgetenv("XDG_RUNTIME_DIR")))
    , q(q)
{
    if (socketName.isEmpty()) {
        socketName = QStringLiteral("wayland-0");
    }
}

ConnectionThread::Private::~Private()
{
    if (display) {
        wl_display_flush(display);
        wl_display_disconnect(display);
    }
}

void ConnectionThread::Private::doInitConnection()
{
    display = wl_display_connect(socketName.toUtf8().constData());
    if (!display) {
        qWarning() << "Failed connecting to Wayland display";
        emit q->failed();
        return;
    }
    qDebug() << "Connected to Wayland server at:" << socketName;

    // setup socket notifier
    setupSocketNotifier();
    setupSocketFileWatcher();
    emit q->connected();
}

void ConnectionThread::Private::setupSocketNotifier()
{
    const int fd = wl_display_get_fd(display);
    socketNotifier.reset(new QSocketNotifier(fd, QSocketNotifier::Read));
    QObject::connect(socketNotifier.data(), &QSocketNotifier::activated, q,
        [this]() {
            if (!display) {
                return;
            }
            wl_display_dispatch(display);
            emit q->eventsRead();
        }
    );
}

void ConnectionThread::Private::setupSocketFileWatcher()
{
    if (!runtimeDir.exists()) {
        return;
    }
    socketWatcher.reset(new QFileSystemWatcher);
    socketWatcher->addPath(runtimeDir.absoluteFilePath(socketName));
    QObject::connect(socketWatcher.data(), &QFileSystemWatcher::fileChanged, q,
        [this] (const QString &file) {
            if (QFile::exists(file) || serverDied) {
                return;
            }
            qWarning() << "Connection to server went away";
            serverDied = true;
            if (display) {
                free(display);
                display = nullptr;
            }
            socketNotifier.reset();

            // need a new filesystem watcher
            socketWatcher.reset(new QFileSystemWatcher);
            socketWatcher->addPath(runtimeDir.absolutePath());
            QObject::connect(socketWatcher.data(), &QFileSystemWatcher::directoryChanged, q,
                [this]() {
                    if (!serverDied) {
                        return;
                    }
                    if (runtimeDir.exists(socketName)) {
                        qDebug() << "Socket reappeared";
                        socketWatcher.reset();
                        serverDied = false;
                        q->initConnection();
                    }
                }
            );
            emit q->connectionDied();
        }
    );
}

ConnectionThread::ConnectionThread(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
}

ConnectionThread::~ConnectionThread() = default;

void ConnectionThread::initConnection()
{
    QMetaObject::invokeMethod(this, "doInitConnection", Qt::QueuedConnection);
}

void ConnectionThread::doInitConnection()
{
    d->doInitConnection();
}

void ConnectionThread::setSocketName(const QString &socketName)
{
    if (d->display) {
        // already initialized
        return;
    }
    d->socketName = socketName;
}

wl_display *ConnectionThread::display()
{
    return d->display;
}

QString ConnectionThread::socketName() const
{
    return d->socketName;
}

}
}
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
#include "display.h"
#include "compositor_interface.h"
#include "output_interface.h"
#include "seat_interface.h"
#include "shell_interface.h"

#include <QCoreApplication>
#include <QDebug>
#include <QAbstractEventDispatcher>
#include <QSocketNotifier>

#include <wayland-server.h>

namespace KWayland
{
namespace Server
{

class Display::Private
{
public:
    Private(Display *q);
    void flush();
    void setRunning(bool running);

    wl_display *display = nullptr;
    wl_event_loop *loop = nullptr;
    QString socketName = QStringLiteral("wayland-0");
    bool running = false;
    QList<OutputInterface*> outputs;

private:
    Display *q;
};

Display::Private::Private(Display *q)
    : q(q)
{
}

Display::Display(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
    connect(QCoreApplication::eventDispatcher(), &QAbstractEventDispatcher::aboutToBlock, this, [this] { d->flush(); });
}

Display::~Display()
{
    terminate();
}

void Display::Private::flush()
{
    if (!display || !loop) {
        return;
    }
    if (wl_event_loop_dispatch(loop, 0) != 0) {
        qWarning() << "Error on dispatching Wayland event loop";
    }
    wl_display_flush_clients(display);
}

void Display::setSocketName(const QString &name)
{
    if (d->socketName == name) {
        return;
    }
    d->socketName = name;
    emit socketNameChanged(d->socketName);
}

QString Display::socketName() const
{
    return d->socketName;
}

void Display::start()
{
    Q_ASSERT(!d->running);
    Q_ASSERT(!d->display);
    d->display = wl_display_create();
    if (wl_display_add_socket(d->display, qPrintable(d->socketName)) != 0) {
        return;
    }

    d->loop = wl_display_get_event_loop(d->display);
    int fd = wl_event_loop_get_fd(d->loop);
    if (fd == -1) {
        qWarning() << "Did not get the file descriptor for the event loop";
        return;
    }
    QSocketNotifier *m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { d->flush(); } );
    d->setRunning(true);
}

void Display::terminate()
{
    if (!d->running) {
        return;
    }
    emit aboutToTerminate();
    wl_display_terminate(d->display);
    wl_display_destroy(d->display);
    d->display = nullptr;
    d->loop = nullptr;
    d->setRunning(false);
}

void Display::Private::setRunning(bool r)
{
    Q_ASSERT(running != r);
    running = r;
    emit q->runningChanged(running);
}

OutputInterface *Display::createOutput(QObject *parent)
{
    OutputInterface *output = new OutputInterface(this, parent);
    connect(output, &QObject::destroyed, this, [this,output] { d->outputs.removeAll(output); });
    connect(this, &Display::aboutToTerminate, output, [this,output] { removeOutput(output); });
    d->outputs << output;
    return output;
}

CompositorInterface *Display::createCompositor(QObject *parent)
{
    CompositorInterface *compositor = new CompositorInterface(this, parent);
    connect(this, &Display::aboutToTerminate, compositor, [this,compositor] { delete compositor; });
    return compositor;
}

ShellInterface *Display::createShell(QObject *parent)
{
    ShellInterface *shell = new ShellInterface(this, parent);
    connect(this, &Display::aboutToTerminate, shell, [this,shell] { delete shell; });
    return shell;
}

SeatInterface *Display::createSeat(QObject *parent)
{
    SeatInterface *seat = new SeatInterface(this, parent);
    connect(this, &Display::aboutToTerminate, seat, [this,seat] { delete seat; });
    return seat;
}

void Display::createShm()
{
    Q_ASSERT(d->running);
    wl_display_init_shm(d->display);
}

void Display::removeOutput(OutputInterface *output)
{
    d->outputs.removeAll(output);
    delete output;
}

quint32 Display::nextSerial()
{
    return wl_display_next_serial(d->display);
}

quint32 Display::serial()
{
    return wl_display_get_serial(d->display);
}

bool Display::isRunning() const
{
    return d->running;
}

Display::operator wl_display*()
{
    return d->display;
}

Display::operator wl_display*() const
{
    return d->display;
}

QList< OutputInterface* > Display::outputs() const
{
    return d->outputs;
}


}
}
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

#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>

#include <sailfishapp.h>

#include "MdrController.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setApplicationName(QStringLiteral("harbour-lauscher"));
    app->setOrganizationName(QStringLiteral("harbour-lauscher"));

    /* Registered only so QML can name the enums (Mdr.NoiseCancelling and
     * friends); the instance itself comes in as a context property. */
    qmlRegisterUncreatableType<MdrController>(
        "de.ygriega.lauscher", 1, 0, "Mdr",
        QStringLiteral("Mdr is exposed as the 'mdr' context property"));

    MdrController controller;

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("mdr"), &controller);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    return app->exec();
}

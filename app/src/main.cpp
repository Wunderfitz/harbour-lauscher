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

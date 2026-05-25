#include "MidiEngine.h"
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);
  app.setApplicationName("LoopMidi");
  app.setApplicationVersion(APP_VERSION);
  app.setOrganizationName("LoopMidi");
  app.setDesktopFileName(
      "loopmidi"); // matches loopmidi.desktop — KDE uses this for taskbar icon
  app.setWindowIcon(QIcon(":/icons/app.png"));

  QQmlApplicationEngine engine;

  // Expose quit slot so QML Shortcut can call it reliably
  engine.rootContext()->setContextProperty("app", &app);

  const QUrl url(QStringLiteral("qrc:/LoopMidi/qml/main.qml"));
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreated, &app,
      [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
          QCoreApplication::exit(-1);
      },
      Qt::QueuedConnection);

  engine.load(url);
  return app.exec();
}

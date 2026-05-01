#pragma once

#include <SentryQml/sentryoptions.h>
#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qjsvalue.h>
#include <QtQml/qqmlengine.h>

#include <memory>

class SentryQmlEngine;

class SENTRYQML_EXPORT Sentry : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Sentry)
    QML_SINGLETON

    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)

public:
    explicit Sentry(QObject *parent = nullptr);
    ~Sentry() override;

    static Sentry *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    bool isInitialized() const;

    Q_INVOKABLE bool init(SentryOptions *options);
    Q_INVOKABLE bool flush(int timeoutMs = 2000);
    Q_INVOKABLE bool close();
    Q_INVOKABLE bool addBreadcrumb(const QVariantMap &breadcrumb);
    bool addBreadcrumb(const QString &message,
                       const QString &category = QString(),
                       const QString &type = QStringLiteral("default"),
                       const QString &level = QStringLiteral("info"),
                       const QVariantMap &data = {});
    Q_INVOKABLE QString captureMessage(const QString &message, const QString &level = QStringLiteral("info"));
    Q_INVOKABLE QString captureException(const QJSValue &exception);

signals:
    void initializedChanged();
    void errorOccurred(const QString &message);

private:
    void ensureQmlEngine(QQmlEngine *engine);

    std::unique_ptr<SentryQmlEngine> m_qmlEngine;
};

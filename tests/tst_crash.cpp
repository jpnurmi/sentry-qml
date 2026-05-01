#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qobject.h>
#include <QtCore/qprocess.h>
#include <QtCore/qstring.h>
#include <QtCore/qtemporarydir.h>
#include <QtCore/qurl.h>
#include <QtNetwork/qhostaddress.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>
#include <QtTest/qsignalspy.h>
#include <QtTest/qtest.h>

#ifdef NDEBUG
#    undef NDEBUG
#endif

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

#if defined(Q_OS_WIN)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    if defined(_MSC_VER)
#        include <intrin.h>
#    endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#    define SENTRY_QML_CRASH_TEST_ASAN_ACTIVE 1
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define SENTRY_QML_CRASH_TEST_ASAN_ACTIVE 1
#    endif
#endif

class SentryQmlCrashTest : public QObject
{
    Q_OBJECT

private slots:
    void capturesNativeCrashWithQmlScope_data();
    void capturesNativeCrashWithQmlScope();
};

class CrashEnvelopeServer : public QTcpServer
{
    Q_OBJECT

public:
    using QTcpServer::QTcpServer;

    QByteArray combinedBody() const
    {
        QByteArray body;
        for (const QByteArray &item : m_bodies) {
            body += item;
            body += '\n';
        }
        return body;
    }

    bool contains(const QByteArray &needle) const { return combinedBody().contains(needle); }

signals:
    void received();

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }

        auto request = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, request]
                {
                    request->append(socket->readAll());

                    const qsizetype headerEnd = request->indexOf("\r\n\r\n");
                    if (headerEnd < 0) {
                        return;
                    }

                    const QByteArray headers = request->left(headerEnd);
                    const QByteArray body = request->mid(headerEnd + 4);
                    qsizetype contentLength = 0;
                    const QList<QByteArray> lines = headers.split('\n');
                    for (const QByteArray &line : lines) {
                        const qsizetype separator = line.indexOf(':');
                        if (separator < 0) {
                            continue;
                        }
                        if (line.left(separator).trimmed().compare("content-length", Qt::CaseInsensitive) == 0) {
                            contentLength = line.mid(separator + 1).trimmed().toLongLong();
                        }
                    }

                    if (body.size() < contentLength) {
                        return;
                    }

                    m_bodies.append(body.left(contentLength));
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    emit received();
                });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    QList<QByteArray> m_bodies;
};

QByteArray clipped(const QByteArray &data)
{
    constexpr qsizetype limit = 8192;
    if (data.size() <= limit) {
        return data;
    }
    return data.left(limit) + "\n... truncated ...\n";
}

QByteArray missingCrashEnvelopeDiagnostics(const QByteArray &output, const QByteArray &body)
{
    return QByteArrayLiteral("child output:\n") + clipped(output)
        + QByteArrayLiteral("\nserver body:\n") + clipped(body);
}

void triggerSegfault()
{
#if defined(SENTRY_QML_CRASH_TEST_ASAN_ACTIVE)
    std::raise(SIGSEGV);
#else
    std::memset(reinterpret_cast<void *>(1), 1, 100);
#endif
}

void triggerAssertFailure()
{
    assert(!"Sentry QML crash test assertion");
}

void triggerAbort()
{
    std::abort();
}

void triggerUnhandledCppException()
{
    throw std::runtime_error("Sentry QML crash test unhandled C++ exception");
}

#if defined(Q_OS_WIN)
void triggerFastFail()
{
#    if defined(_MSC_VER)
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#    else
    RaiseFailFastException(nullptr, nullptr, 0);
#    endif
}
#endif

volatile int stackOverflowSink = 0;

Q_NEVER_INLINE void triggerStackOverflow(int depth)
{
    volatile char buffer[32768];
    for (size_t index = 0; index < sizeof(buffer); index += 512) {
        buffer[index] = static_cast<char>(depth);
    }
    triggerStackOverflow(depth + 1);
    stackOverflowSink += buffer[depth % sizeof(buffer)];
}

class CrashActions : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void crash(const QString &type)
    {
        if (type == QLatin1String("assert-failure")) {
            triggerAssertFailure();
        } else if (type == QLatin1String("abort")) {
            triggerAbort();
        } else if (type == QLatin1String("unhandled-cpp-exception")) {
            triggerUnhandledCppException();
        } else if (type == QLatin1String("stack-overflow")) {
            triggerStackOverflow(0);
#if defined(Q_OS_WIN)
        } else if (type == QLatin1String("fastfail")) {
            triggerFastFail();
#endif
        } else {
            triggerSegfault();
        }
    }
};

int runCrashTarget(int argc, char *argv[])
{
    std::fputs("tst_crash: starting target\n", stderr);

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Sentry"));
    QCoreApplication::setApplicationName(QStringLiteral("Sentry QML Crash Test Target"));

    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    QQmlEngine engine;
    engine.addImportPath(environment.value(QStringLiteral("SENTRY_QML_TEST_QML_IMPORT_PATH"),
                                           QStringLiteral(SENTRY_QML_IMPORT_PATH)));

    const QString crashType = argc > 2
        ? QString::fromLocal8Bit(argv[2])
        : QStringLiteral("segfault");

    CrashActions crashActions;
    engine.rootContext()->setContextProperty(QStringLiteral("crashActions"), &crashActions);
    engine.rootContext()->setContextProperty(QStringLiteral("testCrashType"), crashType);
    engine.rootContext()->setContextProperty(QStringLiteral("testDsn"),
                                             environment.value(QStringLiteral("SENTRY_QML_TEST_DSN")));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"),
        environment.value(QStringLiteral("SENTRY_QML_TEST_DATABASE_PATH")));

    const QString testPath = environment.value(QStringLiteral("SENTRY_QML_TEST_QML_DIR"),
                                               QStringLiteral(SENTRY_QML_TEST_QML_DIR));
    QQmlComponent component(&engine, QUrl::fromLocalFile(QDir(testPath).filePath(QStringLiteral("CrashTest.qml"))));
    if (component.isError()) {
        qWarning().noquote() << component.errorString();
        return 2;
    }

    const std::unique_ptr<QObject> object(component.create());
    if (!object) {
        qWarning().noquote() << component.errorString();
        return 3;
    }

    return 4;
}

void SentryQmlCrashTest::capturesNativeCrashWithQmlScope_data()
{
    QTest::addColumn<QString>("crashType");

    QTest::newRow("segfault") << QStringLiteral("segfault");
    QTest::newRow("stack-overflow") << QStringLiteral("stack-overflow");
    QTest::newRow("assert-failure") << QStringLiteral("assert-failure");
    QTest::newRow("abort") << QStringLiteral("abort");
    QTest::newRow("unhandled-cpp-exception") << QStringLiteral("unhandled-cpp-exception");
#if defined(Q_OS_WIN)
    QTest::newRow("fastfail") << QStringLiteral("fastfail");
#endif
}

void SentryQmlCrashTest::capturesNativeCrashWithQmlScope()
{
    QFETCH(QString, crashType);

    CrashEnvelopeServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString targetPath = QCoreApplication::applicationFilePath();
    QVERIFY2(QFileInfo::exists(targetPath), qPrintable(targetPath));

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SENTRY_QML_TEST_DSN"),
                       QStringLiteral("http://public@127.0.0.1:%1/42").arg(server.serverPort()));
    environment.insert(QStringLiteral("SENTRY_QML_TEST_DATABASE_PATH"),
                       QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));
    environment.insert(QStringLiteral("SENTRY_QML_TEST_QML_IMPORT_PATH"),
                       QStringLiteral(SENTRY_QML_IMPORT_PATH));
    environment.insert(QStringLiteral("SENTRY_QML_TEST_QML_DIR"),
                       QStringLiteral(SENTRY_QML_TEST_QML_DIR));
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(QFileInfo(targetPath).absolutePath());
    QSignalSpy finishedSpy(&process, &QProcess::finished);
    process.start(targetPath, {QStringLiteral("--crash"), crashType});

    QVERIFY2(process.waitForStarted(5000), qPrintable(process.errorString()));

    QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty(), 60000);

    const QByteArray output = process.readAllStandardOutput() + process.readAllStandardError();
    QVERIFY2(process.exitStatus() == QProcess::CrashExit || process.exitCode() != 0, output.constData());

    QTRY_VERIFY2_WITH_TIMEOUT(server.contains("sentry-qml@crash"),
                              missingCrashEnvelopeDiagnostics(output, server.combinedBody()).constData(),
                              15000);

    const QByteArray body = server.combinedBody();
    QVERIFY(body.contains("\"type\":\"event\""));
    QVERIFY(body.contains("\"release\":\"sentry-qml@crash\""));
    QVERIFY(body.contains("\"environment\":\"crash-test\""));
    QVERIFY(body.contains("\"dist\":\"7\""));
    QVERIFY(body.contains("\"id\":\"crash-user\""));
    QVERIFY(body.contains("\"scenario\":\"crash\""));
    QVERIFY(body.contains(QByteArrayLiteral("\"crash_type\":\"") + crashType.toUtf8() + QByteArrayLiteral("\"")));
    QVERIFY(body.contains("\"fingerprint\":[\"{{ default }}\",\"qml-crash\"]"));
    QVERIFY(body.contains("crash breadcrumb"));
}

int main(int argc, char *argv[])
{
    std::fputs("tst_crash: starting\n", stderr);

    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--crash")) {
        return runCrashTarget(argc, argv);
    }

    QCoreApplication app(argc, argv);
    SentryQmlCrashTest test;
    const int result = QTest::qExec(&test, argc, argv);
    std::fprintf(stderr, "tst_crash: finished with %d\n", result);
    return result;
}

#include "tst_crash.moc"

#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <functional>

// One-shot HTTP/1.1 responder for tests. Each request is answered by the
// handler; a response with hang=true is never answered.
class FakeHttpServer : public QObject {
    Q_OBJECT
public:
    struct Response {
        int status = 200;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        bool hang = false;
    };
    struct Request {
        QByteArray target;  // path and query
        QByteArray rawHead; // request line and headers
    };

    explicit FakeHttpServer(std::function<Response(const Request &)> handler) : m_handler(std::move(handler))
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }
    QList<Request> requests;
    int abortedConnections = 0;

private:
    void onReadyRead(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        if (!buffer.contains("\r\n\r\n"))
            return;
        Request req;
        req.rawHead = buffer.left(buffer.indexOf("\r\n\r\n"));
        req.target = req.rawHead.split(' ').value(1);
        requests.append(req);
        const Response res = m_handler(req);
        if (res.hang) {
            connect(socket, &QTcpSocket::disconnected, this, [this] { ++abortedConnections; });
            return;
        }
        QByteArray out = "HTTP/1.1 " + QByteArray::number(res.status) + " X\r\nContent-Type: application/json\r\n";
        for (const auto &[name, value] : res.headers)
            out += name + ": " + value + "\r\n";
        out += "Content-Length: " + QByteArray::number(res.body.size()) + "\r\nConnection: close\r\n\r\n" + res.body;
        socket->write(out);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    std::function<Response(const Request &)> m_handler;
};

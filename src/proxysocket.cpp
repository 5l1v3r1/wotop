#include "standard.h"
#include "utils.h"
#include "proxysocket.h"
#include "logger.h"

using namespace std;

ProxySocket::ProxySocket(int _fd, Protocol _inProto) {
    fd = _fd;
    protocol = _inProto;
    logger(DEBUG) << "Accepted connection";
    logger(DEBUG) << "This connection is in " << (protocol==PLAIN?"PLAIN":"HTTP");
    snprintf(ss, MAXHOSTBUFFERSIZE, "HTTP/1.1 200 OK\r\nContent-Length");

    setNonBlocking(fd);
}

ProxySocket::ProxySocket(char *host, int port, Protocol _outProto) {
    protocol = _outProto;
    logger(DEBUG) << "Making outgoing connection to " << host << ":" << port;
    logger(DEBUG) << "This connection is in " << (protocol==PLAIN?"PLAIN":"HTTP");
    if (snprintf(ss, MAXHOSTBUFFERSIZE,
                "GET %s / HTTP/1.0\r\nHost: %s:%d\r\nContent-Length",
                 host, host, port) >= MAXHOSTBUFFERSIZE) {
        logger(ERROR) << "Host name too long";
        exit(0);
    };
    // Open a socket file descriptor
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0){
        logger(ERROR) << "Could not open outward socket";
        exit(0);
    }

    // Standard syntax
    server = gethostbyname(host);
    if (!server) {
        logger(ERROR) << "Cannot reach host";
        exit(0);
    }

    bzero((char *)&servAddr, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&servAddr.sin_addr.s_addr,
          server->h_length);
    servAddr.sin_port = htons(port);

    // Connect to the server's socket
    if (connect(fd, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        logger(ERROR) << "Was connecting to " << host << ":" << port << "\n"
            << "Cannot connect to remote server";
        exit(0);
    }

    setNonBlocking(fd);
}

int ProxySocket::recvFromSocket(vector<char> &buffer, int from,
                                int &respFrom) {

    receivedBytes = 0;
    numberOfFailures = 0;
    connectionBroken = false;
    gotHttpHeaders = -1;
    if (protocol == PLAIN) {
        logger(DEBUG) << "Recv PLAIN";
        do {
            retval = recv(fd, &buffer[receivedBytes+from],
                          BUFSIZE-from-2-receivedBytes, 0);
            if (retval < 0) {
                numberOfFailures++;
            } else {
                if (retval == 0) {
                    connectionBroken = true;
                    logger(VERB1) << "PLAIN connection broken";
                    numberOfFailures += 10000;
                }
                numberOfFailures = 0;
                receivedBytes += retval;
            }
            // Now loop till either we don't receive anything for
            // 50000 bytes, or we've got 500 bytes of data
            // Both these are to ensure decent latency
        } while (numberOfFailures < 50000 && receivedBytes < 500);

        respFrom = 0;

        logger(DEBUG) << "Received " << receivedBytes << " bytes as plain.";

    } else if (protocol == HTTP) {
        logger(DEBUG) << "Recv HTTP";
        numberOfFailures = 0;
        do {
            retval = recv(fd, &buffer[receivedBytes+from],
                          BUFSIZE-from-2-receivedBytes, 0);
            if (retval == -1) {
                numberOfFailures++;
            } else if (retval == 0) {
                connectionBroken = true;
                logger(VERB1) << "HTTP Connection broken";
                numberOfFailures += 10000;
            } else {
                numberOfFailures = 0;
                receivedBytes += retval;
                for (k=from; k<receivedBytes+from-3; k++) {
                    if (buffer[k] == '\r' && buffer[k+1] == '\n' &&
                        buffer[k+2] == '\r' && buffer[k+3] == '\n') {
                        k += 4;
                        gotHttpHeaders = k-from;
                        break;
                    }
                }
            }
        } while (gotHttpHeaders == -1 && numberOfFailures < 50000);

        if (connectionBroken == true) {
            logger(VERB1) << "Exiting because of broken connection";
            return -1;
        } else if (receivedBytes == 0) {
            return 0;
        }
        // If it was a normal HTTP response,
        // we should parse it
        logger(DEBUG) << "Received " << receivedBytes << " bytes as HTTP headers";
        logger(DEBUG) << &buffer[from];

        // Find content length header
        // TODO Make this more optimum
        for (k=from; k<receivedBytes+from; k++) {
            if (strncmp(&buffer[k], "Content-Length: ", 16) == 0) {
                break;
            }
        }

        // If we couldn't find the header
        if (k == receivedBytes+from) {
            logger(DEBUG) << "Didn't find content-length in headers";
            return 0;
        }

        // Point @k to the start of the content length int
        k += 17;
        int tp=0;
        while (buffer[k] >= '0' && buffer[k] <= '9') {
            tp *= 10;
            tp += buffer[k]-'0';
            k++;
        }

        from = receivedBytes+from;
        receivedBytes = receivedBytes-gotHttpHeaders;
        respFrom = gotHttpHeaders;
        logger(DEBUG) << "headers end at " << gotHttpHeaders;

        // Read the response
        do {
            retval = recv(fd, &buffer[receivedBytes+from],
                          BUFSIZE-from-2-receivedBytes, 0);
            if (retval == 0) {
                logger(VERB1) << "HTTP Connection broken when receiving bytes";
                connectionBroken = true;
                break;
            }
            if (retval > 0) {
                receivedBytes += retval;
            }
        } while (receivedBytes < tp);
    }

    // Signal error, or return length of message
    return connectionBroken ? -1 : receivedBytes;
}

int ProxySocket::sendFromSocket(vector<char> &buffer, int from, int len) {

    sentBytes = 0;
    numberOfFailures = 0;
    if (protocol == PLAIN) {
        logger(DEBUG) << "Write PLAIN";
        sentBytes = send(fd, &buffer[from], len, 0);
    } else if (protocol == HTTP) {
        logger(DEBUG) << "Write HTTP";
        writtenBytes = snprintf(headers, MAXHOSTBUFFERSIZE+4,
                                "%s: %d\r\n\r\n", ss, len);
        if (writtenBytes >= MAXHOSTBUFFERSIZE+4) {
            logger(ERROR) << "Host name or content too long";
            exit(0);
        }
        logger(DEBUG) << "Wrote " << headers;
        sentBytes = send(fd, headers, writtenBytes, 0);
        if (sentBytes < 1) {
            return -1;
        }
        sentBytes = send(fd, &buffer[from], len, 0);
        buffer[from+len] = 0;
        logger(DEBUG) << "Wrote now " << &buffer[from];
    }
    return sentBytes;
}

void ProxySocket::sendHelloMessage() {
    logger(VERB1) << "Sending hello handshake";
    sentBytes = 0;
    writtenBytes = 0;
    do {
        writtenBytes = send(fd, "GET / HTTP/1.1\r\n\r\n", 18, 0);
        if (writtenBytes == 0) {
            logger(ERROR) << "Connection closed at handshake";
            exit(0);
        } else if (writtenBytes > 0) {
            sentBytes += writtenBytes;
        }
    } while (sentBytes < 18);
    logger(VERB1) << "Sent handshake";
}

void ProxySocket::receiveHelloMessage() {
    logger(VERB1) << "Receiving hello handshake";
    receivedBytes = 0;

    char tmp[20];
    do {
        readBytes = recv(fd, tmp, 18, 0);
        if (readBytes == 0) {
            logger(ERROR) << "Connection closed at handshake";
            exit(0);
        } else if (readBytes > 0) {
            receivedBytes += readBytes;
        }
    } while (receivedBytes < 18);
    logger(VERB1) << "Received handshake";
}

void ProxySocket::closeSocket() {
    close(fd);
}

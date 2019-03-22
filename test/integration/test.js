const { expect } = require('chai');
const PromiseSocket = require('promise-socket');
const net = require('net');
const childProcess = require('child_process');
const requestPromise = require('request-promise');
const WebSocket = require('ws');

const fixedPort = process.env.TESTPORT;

class Server {
    constructor(port) {
        this.port = port;
        this.output = '';
        this.process = null;
        this.isRunning = false;
    }

    async start() {
        this.process = childProcess.spawn('../../bin/http-test', ['-s', `${this.port}`]);

        return new Promise((resolve, reject) => {
            this.process.stdout.on('data', (data) => {
                if (data.toString().startsWith(`Listening on port ${this.port}`)) {
                    this.isRunning = true;
                    resolve();
                } else {
                    reject(new Error(`server couldn't be started on port ${this.port}`));
                }
            });
            this.process.stdout.on('data', (data) => {
                this.output += data.toString();
            });
            this.process.stdout.on('close', () => reject(new Error(`server couldn't be started on port ${this.port}`)));
        });
    }

    async stop() {
        return new Promise((resolve) => {
            // this.process.on('exit', () => { console.log(this.output); resolve(); });
            this.process.on('exit', () => resolve());
            this.process.kill();
        });
    }
}

class FakeServer {
    constructor(port) {
        this.port = port;
        this.output = '';
        this.isRunning = false;
    }

    async start() {
        this.isRunning = true;
    }

    async stop() {
        this.isRunning = false;
    }
}

async function readLine(socket) {
    let line = '';

    for (; ;) {
        const r = await socket.read(1);
        if (r === undefined) {
            return line;
        }
        const c = r.toString();
        if (c === '\n') {
            return line;
        }
        if (c !== '\r') {
            line += c;
        }
    }
}

async function readHeaders(socket) {
    const headers = [];

    for (; ;) {
        const line = await readLine(socket);
        if (line.length === 0) {
            return headers;
        }
        headers.push(line);
    }
}

describe('HTTP server', () => {
    let server;
    let port = 4000 + Math.floor(Math.random() * 4000);
    const host = 'localhost';

    beforeEach(async () => {
        if (fixedPort) {
            port = fixedPort;
            server = new FakeServer(port);
        } else {
            server = new Server(port);
        }
        await server.start();
    });

    afterEach(async () => {
        if (server.isRunning) {
            await server.stop();
        } else {
            console.error('Server is not running:');
            process.stdout.write(server.output);
        }

        port += 1;
    });

    describe('requests through raw sockets', () => {
        let socket;

        function createSocket() {
            const s = new net.Socket();
            s.setNoDelay(true);
            const ps = new PromiseSocket(s);
            ps.setTimeout(25);
            return ps;
        }

        beforeEach(async () => {
            socket = createSocket();
            await socket.connect({ host, port });
        });

        afterEach(async () => {
            try {
                await socket.end();
            } catch (err) {
                // eslint-disable-line no-empty
            }
        });

        it('can GET a simple url', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');
            const response = await socket.readAll();
            expect(server.isRunning).to.be.true;
            expect(response).to.exist;
        });

        it('returns status 200 on success', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');
            const statusLine = await readLine(socket);
            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 200 OK');
        });

        it('returns an error if HTTP version is 1.0', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.0\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');
            const statusLine = await readLine(socket);
            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 505 HTTP Version Not Supported');
        });

        it('returns an error if HTTP request contains extra space', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1 \r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');
            const statusLine = await readLine(socket);
            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 400 Bad Request');
        });

        it('returns an error if Content-Length can\'t be parsed', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1 \r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + 'Content-Length: 12a\r\n'
                + '\r\n');
            const statusLine = await readLine(socket);
            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 400 Bad Request');
        });

        it('can handle an early socket close', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`);
            await socket.end();

            socket = createSocket();
            await socket.connect({ host, port });
            await socket.write(''
                + 'GET /simple HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');

            const statusLine = await readLine(socket);
            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 200 OK');
        });

        it('can handle two simultaneous connections', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1\r\n'
                + `Host: ${host}:`);

            const anotherSocket = createSocket();
            await anotherSocket.connect({ host, port });
            await anotherSocket.write(''
                + 'GET /simple HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');

            await socket.write(''
                + `${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');

            const statusLine = await readLine(socket);
            const anotherStatusLine = await readLine(anotherSocket);

            await anotherSocket.end();

            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 200 OK');
            expect(anotherStatusLine).to.equal('HTTP/1.1 200 OK');
        });

        it('can handle a POST', async () => {
            const message = 'testtesttesttesttesttesttest';

            await socket.write(''
                + 'POST /post HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + `Content-Length: ${message.length}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');

            await socket.write(message);

            const statusLine = await readLine(socket);
            const headers = await readHeaders(socket);

            const bodyRaw = await socket.readAll();
            const body = bodyRaw.toString();

            expect(statusLine).to.equal('HTTP/1.1 200 OK');
            expect(headers).to.include('Transfer-Encoding: chunked');
            expect(body).to.contain('This is a response from \'cgi_post\'');
            expect(body).to.contain(message);
        });

        it('can handle an early close in a POST request', async () => {
            const message = 'testtesttesttesttesttesttest';

            await socket.write(''
                + 'POST /post HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + `Content-Length: ${message.length}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');

            await socket.write(message.slice(0, 2));

            const statusLine = await readLine(socket);
            const headers = await readHeaders(socket);

            const bodyRaw = await socket.readAll();
            const body = bodyRaw.toString();

            expect(statusLine).to.equal('HTTP/1.1 200 OK');
            expect(headers).to.include('Transfer-Encoding: chunked');
            expect(body).to.contain('This is a response from \'cgi_post\'');
            expect(body).to.contain(`You posted: "${message.slice(0, 2)}`);
        });

        it('can handle a chunked POST', async () => {
            const message = 'testtesttesttesttesttesttest';

            await socket.write(''
                + 'POST /post HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Transfer-Encoding: chunked\r\n'
                + 'Connection: close\r\n'
                + '\r\n');

            await socket.write(`${message.length.toString(16)}\r\n`);
            await socket.write(`${message}\r\n`);
            await socket.write('0\r\n\r\n');

            const statusLine = await readLine(socket);
            const headers = await readHeaders(socket);

            const bodyRaw = await socket.readAll();
            const body = bodyRaw.toString();

            expect(statusLine).to.equal('HTTP/1.1 200 OK');
            expect(headers).to.include('Transfer-Encoding: chunked');
            expect(body).to.contain('This is a response from \'cgi_post\'');
            expect(body).to.contain(message);
        });

        it('can handle a POST with misformed chunk header and footer', async () => {
            const message = 'testtesttesttesttest';

            await socket.write(''
                + 'POST /post HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Transfer-Encoding: chunked\r\n'
                + 'Connection: close\r\n'
                + '\r\n');

            await socket.write(`${message.length.toString(16)}X\rY\n`);
            await socket.write(`${message}Z\rW\n`);
            await socket.write('X\rY\nZ\rW\nS');

            const statusLine = await readLine(socket);
            const headers = await readHeaders(socket);

            const bodyRaw = await socket.readAll();
            const body = bodyRaw.toString();

            expect(statusLine).to.equal('HTTP/1.1 200 OK');
            expect(headers).to.include('Transfer-Encoding: chunked');
            expect(body).to.contain('This is a response from \'cgi_post\'');
            expect(body).to.contain(message);
        });

        it('performs websocket handshake without key', async () => {
            await socket.write(''
                + 'GET /ws-echo HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: Upgrade\r\n'
                + 'Upgrade: websocket\r\n'
                + '\r\n');

            const statusLine = await readLine(socket);
            const headers = await readHeaders(socket);

            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 101 Switching Protocols');
            expect(headers).to.include('Upgrade: websocket');
            expect(headers).to.include('Connection: Upgrade');
            expect(headers.join()).to.not.contain('Sec-WebSocket-Accept:');
        });

        it('performs websocket handshake with key', async () => {
            await socket.write(''
                + 'GET /ws-echo HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: Upgrade\r\n'
                + 'Upgrade: websocket\r\n'
                + 'Sec-WebSocket-Key: I3XUDuX0KVcV9WZaunih6g==\r\n'
                + '\r\n');

            const statusLine = await readLine(socket);
            const headers = await readHeaders(socket);

            expect(server.isRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 101 Switching Protocols');
            expect(headers).to.include('Upgrade: websocket');
            expect(headers).to.include('Connection: Upgrade');
            expect(headers).to.include('Sec-WebSocket-Accept: AHbBmP6erNg7nxW8CJ+V7AHhT3Y=');
        });

        async function connectWebsocket(sock, path) {
            await sock.write(''
                + `GET ${path} HTTP/1.1\r\n`
                + `Host: ${host}:${port}\r\n`
                + 'Connection: Upgrade\r\n'
                + 'Upgrade: websocket\r\n'
                + '\r\n');

            const statusLine = await readLine(sock);
            const headers = await readHeaders(sock);

            expect(statusLine).to.equal('HTTP/1.1 101 Switching Protocols');
            expect(headers).to.include('Upgrade: websocket');
            expect(headers).to.include('Connection: Upgrade');
            expect(headers.join()).to.not.contain('Sec-WebSocket-Accept:');
        }

        it('can echo a text message through websocket', async () => {
            await connectWebsocket(socket, '/ws-echo');

            const message = [0x81, 0x05, 0x41, 0x42, 0x43, 0x44, 0x45];
            await socket.write(Buffer.from(message));

            const response = await socket.read(message.length);
            const array = Array.from(response);

            expect(server.isRunning).to.be.true;
            expect(array).to.deep.equal(message);
        });

        it('can handle two interleaved websockets', async () => {
            const anotherSocket = createSocket();
            await anotherSocket.connect({ host, port });

            await connectWebsocket(socket, '/ws-echo');
            await connectWebsocket(anotherSocket, '/ws-echo');

            const message1 = [0x81, 0x05, 0x41, 0x42, 0x43, 0x44, 0x45];
            const message2 = [0x82, 0x04, 0x01, 0x02, 0x03, 0x04];

            for (let i = 0; i < Math.max(message1.length, message2.length); i++) {
                if (i < message1.length) {
                    await socket.write(Buffer.from(message1.slice(i, i + 1)));
                }
                if (i < message2.length) {
                    await anotherSocket.write(Buffer.from(message2.slice(i, i + 1)));
                }
            }

            const response1 = await socket.read(message1.length);
            const array1 = Array.from(response1);

            const response2 = await anotherSocket.read(message2.length);
            const array2 = Array.from(response2);

            expect(server.isRunning).to.be.true;
            expect(array1).to.deep.equal(message1);
            expect(array2).to.deep.equal(message2);
        });

        it('can handle an early close of the websocket socket', async () => {
            await connectWebsocket(socket, '/ws-echo');

            const message = [0x81, 0x05, 0x41, 0x42, 0x43];
            await socket.write(Buffer.from(message));
            await socket.end();

            expect(server.isRunning).to.be.true;
        });

        it('can write to one websocket and read from another', async () => {
            const anotherSocket = createSocket();
            await anotherSocket.connect({ host, port });

            await connectWebsocket(socket, '/ws-in');
            await connectWebsocket(anotherSocket, '/ws-out');

            const message = [0x81, 0x02, 0x68, 0x69];

            await socket.write(Buffer.from(message));

            const response = await anotherSocket.read(message.length);
            const array = Array.from(response);

            expect(server.isRunning).to.be.true;
            expect(array).to.deep.equal(message);
        });
    });


    describe('simple requests', () => {
        function request(method, path, body) {
            const options = {
                method,
                uri: `http://${host}:${port}${path}`,
                body,
                resolveWithFullResponse: true,
                timeout: 250,
            };

            return requestPromise(options);
        }

        it('can GET a simple url', async () => {
            await request('GET', '/simple')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.equal('This is a response from \'cgi_simple\'');
                });
        });

        it('can GET an url with chunked transfer encoding', async () => {
            await request('GET', '/stream')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.equal('This is a response from \'cgi_stream\'');
                });
        });

        it('can GET a wildcard url', async () => {
            await request('GET', '/wildcard/xyz')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.equal('This is a response from \'cgi_simple\'');
                });
        });

        it('can GET an url with no query', async () => {
            await request('GET', '/query')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).not.to.contain('=');
                });
        });

        it('can GET an url with a query', async () => {
            await request('GET', '/query?a=142&b=271')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).to.contain('a = 142');
                    expect(response.body).to.contain('b = 271');
                });
        });

        it('can GET an url with misformed query containing double ??', async () => {
            await request('GET', '/query??a=1')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).not.to.contain('=');
                });
        });

        it('can GET an url with an urlencoded query', async () => {
            await request('GET', '/query?a="1+2%203%2B4%20%35b%3DX"')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).to.contain('a = "1 2 3+4 5b=X"');
                    expect(response.body).not.to.contain('b =');
                });
        });

        it('returns 404 for an unknown url', async () => {
            await request('GET', '/unkown')
                .catch((error) => {
                    expect(server.isRunning).to.be.true;
                    expect(error.response.statusCode).to.equal(404);
                });
        });

        it('returns 414 for an url that is too long', async () => {
            let path = '/0123456789';

            for (let i = 0; i < 7; i++) {
                path += path;
            }

            expect(path.length).to.be.greaterThan(1000);

            await request('GET', path)
                .catch((error) => {
                    expect(server.isRunning).to.be.true;
                    expect(error.response.statusCode).to.equal(414);
                });
        });

        it('returns 414 for a query that is too long', async () => {
            const queryList = [];

            for (let i = 0; i < 150; i++) {
                queryList.push(`a${i}=${i}`);
            }

            const query = queryList.join('&');

            expect(query.length).to.be.greaterThan(1000);

            await request('GET', `/query?${query}`)
                .catch((error) => {
                    expect(server.isRunning).to.be.true;
                    expect(error.response.statusCode).to.equal(414);
                });
        });

        it('can handle an unkown method', async () => {
            await request('ABC', '/simple')
                .catch((error) => {
                    expect(server.isRunning).to.be.true;
                    expect(error.response).to.exist;
                    expect(error.response.statusCode).to.equal(405);
                });
        });

        it('can handle a POST request', async () => {
            await request('POST', '/post', 'test')
                .then((response) => {
                    expect(server.isRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_post\'');
                    expect(response.body).to.contain('You posted: "test"');
                });
        });
    });


    describe('websocket requests', () => {
        it('can echo a text message', (done) => {
            const ws = new WebSocket(`ws://${host}:${port}/ws-echo`);
            const text = '0123456789abcdef';
            let dataReceived;

            ws.on('open', () => {
                ws.send(text);
            });

            ws.on('message', (data) => {
                dataReceived = data;
                ws.close(1000);
            });

            ws.on('close', (closeReason) => {
                expect(server.isRunning).to.be.true;
                expect(closeReason).to.equal(1000);
                expect(dataReceived).to.equal(text);
                done();
            });

            ws.on('error', (error) => {
                expect(server.isRunning).to.be.true;
                done(new Error(error));
            });
        });

        it('can echo a binary message', (done) => {
            const ws = new WebSocket(`ws://${host}:${port}/ws-echo`);
            const array = new Uint8Array(64);

            for (let i = 0; i < array.length; i++) {
                array[i] = Math.floor(Math.random() * 256);
            }

            let dataReceived;

            ws.on('open', () => {
                ws.send(array);
            });

            ws.on('message', (data) => {
                dataReceived = data;
                ws.close(1000);
            });

            ws.on('close', (closeReason) => {
                expect(server.isRunning).to.be.true;
                expect(closeReason).to.equal(1000);
                expect(dataReceived).to.have.lengthOf(array.length);
                expect(dataReceived).to.deep.equal(array);
                done();
            });

            ws.on('error', (error) => {
                expect(server.isRunning).to.be.true;
                ws.close();
                done(new Error(error));
            });
        });

        it('can echo a longer binary message', (done) => {
            const ws = new WebSocket(`ws://${host}:${port}/ws-echo`);
            const array = new Uint8Array(2048);

            for (let i = 0; i < array.length; i++) {
                array[i] = Math.floor(Math.random() * 256);
            }

            let dataReceived;

            ws.on('open', () => {
                ws.send(array);
            });

            ws.on('message', (data) => {
                dataReceived = data;
                ws.close();
            });

            ws.on('close', () => {
                expect(server.isRunning).to.be.true;
                expect(dataReceived).to.have.lengthOf(array.length);
                expect(dataReceived).to.deep.equal(array);
                done();
            });

            ws.on('error', (error) => {
                expect(server.isRunning).to.be.true;
                ws.close();
                done(new Error(error));
            });
        });

        it('can echo a very long binary message', (done) => {
            const ws = new WebSocket(`ws://${host}:${port}/ws-echo`);
            const array = new Uint8Array(131052);

            for (let i = 0; i < array.length; i++) {
                array[i] = Math.floor(Math.random() * 256);
            }

            let dataReceived;

            ws.on('open', () => {
                ws.send(array);
            });

            ws.on('message', (data) => {
                dataReceived = data;
                ws.close();
            });

            ws.on('close', () => {
                expect(server.isRunning).to.be.true;
                expect(dataReceived).to.have.lengthOf(array.length);
                expect(dataReceived).to.deep.equal(array);
                done();
            });

            ws.on('error', (error) => {
                expect(server.isRunning).to.be.true;
                ws.close();
                done(new Error(error));
            });
        });

        it('responds to ping with a pong', (done) => {
            const ws = new WebSocket(`ws://${host}:${port}/ws-echo`);
            let pingReceived = false;
            let dataReceived;

            ws.on('open', () => {
                ws.ping();
            });
            ws.on('pong', (data) => {
                dataReceived = data;
                pingReceived = true;
                ws.close();
            });
            ws.on('close', () => {
                expect(server.isRunning).to.be.true;
                expect(pingReceived).to.be.true;
                expect(dataReceived).to.be.empty;
                done();
            });
            ws.on('error', (error) => {
                expect(server.isRunning).to.be.true;
                done(new Error(error));
            });
        });

        it('responds to ping with data with a pong with the same data', (done) => {
            const ws = new WebSocket(`ws://${host}:${port}/ws-echo`);
            const message = 'abc';
            let messageReceived;

            ws.on('open', () => {
                ws.ping(message);
            });
            ws.on('pong', (data) => {
                messageReceived = data.toString();
                ws.close();
            });
            ws.on('close', () => {
                expect(server.isRunning).to.be.true;
                expect(messageReceived).to.equal(message);
                done();
            });
            ws.on('error', (error) => {
                expect(server.isRunning).to.be.true;
                done(new Error(error));
            });
        });
    });
});

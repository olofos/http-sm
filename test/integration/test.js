const { expect } = require('chai');
const PromiseSocket = require('promise-socket');
const net = require('net');
const childProcess = require('child_process');
const requestPromise = require('request-promise');
const WebSocket = require('ws');

function startServer(port) {
    const server = childProcess.spawn('../../bin/http-test', ['-s', `${port}`]);

    return new Promise((resolve, reject) => {
        server.stdout.on('data', (data) => {
            if (data.toString().startsWith(`Listening on port ${port}`)) {
                resolve(server);
            }
        });
        server.stdout.on('close', () => reject(new Error(`server couldn't be started on port ${port}`)));
    });
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
    let serverRunning = false;
    let server;
    let port = 4000 + Math.floor(Math.random() * 4000);
    const host = 'localhost';

    beforeEach(async () => {
        server = await startServer(port);
        serverRunning = true;
        server.on('exit', () => { serverRunning = true; });
    });

    afterEach(() => {
        if (server) {
            server.kill();
        }
        port += 1;
    });

    describe('requests through raw sockets', () => {
        let socket;

        beforeEach(async () => {
            socket = new PromiseSocket(new net.Socket());
            await socket.connect({ host, port });
            socket.setTimeout(25);
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
            expect(serverRunning).to.be.true;
            expect(response).to.exist;
        });

        it('returns status 200 on success', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');
            const statusLine = await readLine(socket);
            expect(serverRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 200 OK');
        });

        it('returns an error if HTTP version is 1.0', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.0\r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');
            const statusLine = await readLine(socket);
            expect(serverRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 505 HTTP Version Not Supported');
        });

        it('returns an error if HTTP request contains extra space', async () => {
            await socket.write(''
                + 'GET /simple HTTP/1.1 \r\n'
                + `Host: ${host}:${port}\r\n`
                + 'Connection: close\r\n'
                + '\r\n');
            const statusLine = await readLine(socket);
            expect(serverRunning).to.be.true;
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
            expect(serverRunning).to.be.true;
            expect(statusLine).to.equal('HTTP/1.1 400 Bad Request');
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

            expect(statusLine).to.equal('HTTP/1.1 101 Switching Protocols');
            expect(headers).to.include('Upgrade: websocket');
            expect(headers).to.include('Connection: Upgrade');
            expect(headers).to.include('Sec-WebSocket-Accept: AHbBmP6erNg7nxW8CJ+V7AHhT3Y=');
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
                    expect(serverRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.equal('This is a response from \'cgi_simple\'');
                });
        });

        it('can GET an url with chunked transfer encoding', async () => {
            await request('GET', '/stream')
                .then((response) => {
                    expect(serverRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.equal('This is a response from \'cgi_stream\'');
                });
        });

        it('can GET a wildcard url', async () => {
            await request('GET', '/wildcard/xyz')
                .then((response) => {
                    expect(serverRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.equal('This is a response from \'cgi_simple\'');
                });
        });

        it('can GET an url with no query', async () => {
            await request('GET', '/query')
                .then((response) => {
                    expect(serverRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).not.to.contain('=');
                });
        });

        it('can GET an url with a query', async () => {
            await request('GET', '/query?a=142&b=271')
                .then((response) => {
                    expect(serverRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).to.contain('a = 142');
                    expect(response.body).to.contain('b = 271');
                });
        });

        it('can GET an url with misformed query containing double ??', async () => {
            await request('GET', '/query??a=1')
                .then((response) => {
                    expect(serverRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).not.to.contain('=');
                });
        });

        it('can GET an url with an urlencoded query', async () => {
            await request('GET', '/query?a="1+2%203%2B4%20%35b%3DX"')
                .then((response) => {
                    expect(serverRunning).to.be.true;
                    expect(response.statusCode).to.equal(200);
                    expect(response.body).to.contain('\'cgi_query\'');
                    expect(response.body).to.contain('a = "1 2 3+4 5b=X"');
                    expect(response.body).not.to.contain('b =');
                });
        });

        it('returns 404 for an unknown url', async () => {
            await request('GET', '/unkown')
                .catch((error) => {
                    expect(serverRunning).to.be.true;
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
                    expect(serverRunning).to.be.true;
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
                    expect(serverRunning).to.be.true;
                    expect(error.response.statusCode).to.equal(414);
                });
        });

        it('can handle an unkown method', async () => {
            await request('ABC', '/simple')
                .catch((error) => {
                    expect(serverRunning).to.be.true;
                    expect(error.response).to.exist;
                    expect(error.response.statusCode).to.equal(405);
                });
        });

        it('can handle a POST request', async () => {
            await request('POST', '/post', 'test')
                .then((response) => {
                    expect(serverRunning).to.be.true;
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
                expect(serverRunning).to.be.true;
                expect(closeReason).to.equal(1000);
                expect(dataReceived).to.equal(text);
                done();
            });

            ws.on('error', (error) => {
                expect(serverRunning).to.be.true;
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
                expect(serverRunning).to.be.true;
                expect(closeReason).to.equal(1000);
                expect(dataReceived).to.have.lengthOf(array.length);
                expect(dataReceived).to.deep.equal(array);
                done();
            });

            ws.on('error', (error) => {
                expect(serverRunning).to.be.true;
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
                expect(serverRunning).to.be.true;
                expect(dataReceived).to.have.lengthOf(array.length);
                expect(dataReceived).to.deep.equal(array);
                done();
            });

            ws.on('error', (error) => {
                expect(serverRunning).to.be.true;
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
                expect(serverRunning).to.be.true;
                expect(dataReceived).to.have.lengthOf(array.length);
                expect(dataReceived).to.deep.equal(array);
                done();
            });

            ws.on('error', (error) => {
                expect(serverRunning).to.be.true;
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
                expect(serverRunning).to.be.true;
                expect(pingReceived).to.be.true;
                expect(dataReceived).to.be.empty;
                done();
            });
            ws.on('error', (error) => {
                expect(serverRunning).to.be.true;
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
                expect(serverRunning).to.be.true;
                expect(messageReceived).to.equal(message);
                done();
            });
            ws.on('error', (error) => {
                expect(serverRunning).to.be.true;
                done(new Error(error));
            });
        });
    });
});

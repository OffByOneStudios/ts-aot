import * as net from 'net';

const server = net.createServer((socket) => {
    console.log("Client connected");
    socket.on('data', (data) => {
        console.log("Data received: " + data.toString());
        socket.write("Echo: " + data.toString());
    });
    socket.on('end', () => {
        console.log("Client disconnected");
    });
});

server.listen(8081, () => {
    console.log("Server listening on 8081");
});

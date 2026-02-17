/**
 * TCP Chat Server
 *
 * Multi-client TCP chat server with nicknames, rooms, private messaging,
 * and connection management. Uses length-prefixed JSON protocol for
 * reliable message framing.
 *
 * Usage:
 *   chat-server                        Start on default port 4000
 *   chat-server --port 8080            Start on custom port
 *
 * Client protocol (length-prefixed JSON over TCP):
 *   AUTH:    {"type":"auth","nick":"alice","pass":"secret"}
 *   MSG:     {"type":"msg","text":"hello"}
 *   PRIVMSG: {"type":"privmsg","to":"bob","text":"hi"}
 *   JOIN:    {"type":"join","room":"dev"}
 *   LEAVE:   {"type":"leave"}
 *   LIST:    {"type":"list"}
 *   QUIT:    {"type":"quit"}
 *
 * Node modules: net, readline, events, buffer, crypto, os, util, dns
 */

import * as net from 'net';
import * as os from 'os';
import * as dns from 'dns';
import * as util from 'util';
import { RoomManager } from './room';
import { ClientManager } from './client';

function getServerInfo(): string {
    const hostname = os.hostname();
    const cpus = os.cpus();
    const cpuModel = cpus.length > 0 ? cpus[0].model : 'unknown';
    const totalMem = Math.round(os.totalmem() / (1024 * 1024));
    const freeMem = Math.round(os.freemem() / (1024 * 1024));

    const lines: string[] = [];
    lines.push('Server Information:');
    lines.push(util.format('  Hostname:  %s', hostname));
    lines.push(util.format('  Platform:  %s %s', os.platform(), os.arch()));
    lines.push(util.format('  CPU:       %s (%s cores)', cpuModel, cpus.length));
    lines.push(util.format('  Memory:    %s MB free / %s MB total', freeMem, totalMem));
    return lines.join('\n');
}

function parsePort(argv: string[]): number {
    const args = argv.slice(2);
    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--port' && i + 1 < args.length) {
            const port = parseInt(args[i + 1], 10);
            if (port > 0 && port < 65536) return port;
        }
    }
    const envPort = process.env.CHAT_PORT;
    if (envPort) {
        const port = parseInt(envPort, 10);
        if (port > 0 && port < 65536) return port;
    }
    return 4000;
}

function user_main(): number {
    const PORT = parsePort(process.argv);
    const HOST = process.env.CHAT_HOST || '0.0.0.0';

    // Initialize room manager with default room
    const rooms = new RoomManager();
    const clients = new ClientManager(rooms);

    // Create TCP server
    const server = net.createServer((socket: net.Socket) => {
        clients.handleConnection(socket);
    });

    // Server events
    server.on('error', (err: Error) => {
        console.error(util.format('Server error: %s', err.message));
    });

    server.on('listening', () => {
        console.log('');
        console.log('=== ts-aot TCP Chat Server ===');
        console.log('');
        console.log(getServerInfo());
        console.log('');
        console.log('Rooms: #general (default)');
        console.log('Awaiting connections...');
        console.log('');
    });

    // Graceful shutdown
    process.on('SIGINT', () => {
        console.log('\nShutting down...');
        server.close(() => {
            console.log('Server closed');
            process.exit(0);
        });
    });

    process.on('SIGTERM', () => {
        server.close(() => {
            process.exit(0);
        });
    });

    // Start listening
    server.listen(PORT, HOST);

    return 0;
}

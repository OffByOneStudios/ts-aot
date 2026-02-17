/**
 * TCP Chat Server — Client Connection Handler
 *
 * Manages individual client sessions: authentication, message routing,
 * command processing, and disconnect handling.
 */

import * as net from 'net';
import * as crypto from 'crypto';
import * as util from 'util';
import {
    Message, MessageParser, frameMessage,
    serverMessage, errorMessage, privateMessage, parseCommand
} from './protocol';
import { RoomManager, RoomMember } from './room';

export interface ClientSession {
    sessionId: string;
    nick: string;
    socket: net.Socket;
    parser: MessageParser;
    authenticated: boolean;
    connectedAt: number;
    remoteAddr: string;
}

/**
 * Hash a password with SHA-256 for storage comparison.
 */
function hashPassword(pass: string): string {
    const hash = crypto.createHash('sha256');
    hash.update(pass);
    return hash.digest('hex');
}

/**
 * Manages all connected client sessions.
 */
export class ClientManager {
    private sessions: Map<string, ClientSession>;
    private nickToSession: Map<string, string>;
    private rooms: RoomManager;
    private passwords: Map<string, string>; // nick -> hashed password

    constructor(rooms: RoomManager) {
        this.sessions = new Map();
        this.nickToSession = new Map();
        this.rooms = rooms;
        this.passwords = new Map();
    }

    get clientCount(): number {
        return this.sessions.size;
    }

    /**
     * Handle a new TCP connection.
     */
    handleConnection(socket: net.Socket): void {
        const sessionId = crypto.randomUUID();
        let addr = socket.remoteAddress;
        if (!addr) addr = 'unknown';
        let port = socket.remotePort;
        if (!port) port = 0;
        const remoteAddr = addr + ':' + port;

        const session: ClientSession = {
            sessionId,
            nick: '',
            socket,
            parser: new MessageParser(),
            authenticated: false,
            connectedAt: Date.now(),
            remoteAddr
        };

        this.sessions.set(sessionId, session);

        // Configure socket
        socket.setKeepAlive(true, 30000);
        socket.setTimeout(300000); // 5 minute timeout

        console.log(util.format('[connect] %s (session: %s)', remoteAddr, sessionId.substring(0, 8)));

        // Send welcome
        this.sendTo(session, serverMessage('Welcome to ts-aot Chat Server'));
        this.sendTo(session, serverMessage('Send AUTH <nick> <pass> to authenticate'));

        // Handle incoming data
        socket.on('data', (data: Buffer) => {
            this.handleData(sessionId, data);
        });

        // Handle disconnect
        socket.on('end', () => {
            this.handleDisconnect(sessionId, 'connection ended');
        });

        socket.on('error', (err: Error) => {
            this.handleDisconnect(sessionId, err.message);
        });

        socket.on('timeout', () => {
            this.sendTo(session, serverMessage('Connection timed out'));
            socket.end();
        });
    }

    /**
     * Process incoming data from a client.
     */
    private handleData(sessionId: string, data: Buffer): void {
        const session = this.sessions.get(sessionId);
        if (!session) return;

        const messages = session.parser.parse(data);
        for (let i = 0; i < messages.length; i++) {
            this.handleMessage(session, messages[i]);
        }
    }

    /**
     * Route a parsed message to the appropriate handler.
     */
    private handleMessage(session: ClientSession, msg: Message): void {
        if (session.authenticated === false) {
            if (msg.type === 'auth') {
                this.handleAuth(session, msg);
            } else {
                this.sendTo(session, errorMessage('Not authenticated. Send AUTH first.'));
            }
            return;
        }

        switch (msg.type) {
            case 'msg':
                this.handleChat(session, msg.text || '');
                break;
            case 'privmsg':
                this.handlePrivateMsg(session, msg.to || '', msg.text || '');
                break;
            case 'join':
                this.handleJoin(session, msg.room || 'general');
                break;
            case 'leave':
                this.handleLeave(session);
                break;
            case 'list':
                this.handleList(session);
                break;
            case 'quit':
                this.sendTo(session, serverMessage('Goodbye!'));
                session.socket.end();
                break;
            default:
                this.sendTo(session, errorMessage('Unknown command: ' + msg.type));
        }
    }

    /**
     * Authenticate a client session.
     */
    private handleAuth(session: ClientSession, msg: Message): void {
        const nick = msg.nick || '';
        const pass = msg.pass || '';

        if (!nick || nick.length < 2 || nick.length > 20) {
            this.sendTo(session, errorMessage('Nickname must be 2-20 characters'));
            return;
        }

        // Check if nick is taken by someone else
        const existingSessionId = this.nickToSession.get(nick);
        if (existingSessionId && existingSessionId !== session.sessionId) {
            const existing = this.sessions.get(existingSessionId);
            if (existing) {
                // Check password for re-login
                const storedHash = this.passwords.get(nick);
                if (storedHash && storedHash !== hashPassword(pass)) {
                    this.sendTo(session, errorMessage('Nickname already in use'));
                    return;
                }
            }
        }

        // Store password hash on first registration
        if (!this.passwords.has(nick)) {
            this.passwords.set(nick, hashPassword(pass));
        } else {
            // Verify password
            if (this.passwords.get(nick) !== hashPassword(pass)) {
                this.sendTo(session, errorMessage('Wrong password for nickname'));
                return;
            }
        }

        session.nick = nick;
        session.authenticated = true;
        this.nickToSession.set(nick, session.sessionId);

        console.log(util.format('[auth] %s authenticated as "%s"', session.remoteAddr, nick));

        this.sendTo(session, serverMessage('Authenticated as ' + nick));

        // Auto-join general room
        const generalRoom = this.rooms.getOrCreate('general');
        const member: RoomMember = {
            sessionId: session.sessionId,
            nick: session.nick,
            send: (m: Message) => this.sendTo(session, m)
        };
        generalRoom.join(member);
    }

    /**
     * Handle a chat message — broadcast to current room.
     */
    private handleChat(session: ClientSession, text: string): void {
        if (!text) return;

        // Check for command prefix
        const cmd = parseCommand(text);
        if (cmd) {
            this.handleMessage(session, cmd);
            return;
        }

        const room = this.rooms.findRoom(session.sessionId);
        if (!room) {
            this.sendTo(session, errorMessage('Not in a room. Use /join <room>'));
            return;
        }

        room.sendChat(session.sessionId, text);
    }

    /**
     * Handle a private message.
     */
    private handlePrivateMsg(session: ClientSession, toNick: string, text: string): void {
        if (!toNick || !text) {
            this.sendTo(session, errorMessage('Usage: /msg <nick> <message>'));
            return;
        }

        const targetSessionId = this.nickToSession.get(toNick);
        if (!targetSessionId) {
            this.sendTo(session, errorMessage('User not found: ' + toNick));
            return;
        }

        const targetSession = this.sessions.get(targetSessionId);
        if (!targetSession) {
            this.sendTo(session, errorMessage('User not connected: ' + toNick));
            return;
        }

        const msg = privateMessage(session.nick, toNick, text);
        this.sendTo(targetSession, msg);
        // Echo to sender
        this.sendTo(session, msg);
    }

    /**
     * Handle room join.
     */
    private handleJoin(session: ClientSession, roomName: string): void {
        // Leave current room first
        this.rooms.removeFromAll(session.sessionId);

        const room = this.rooms.getOrCreate(roomName);
        const member: RoomMember = {
            sessionId: session.sessionId,
            nick: session.nick,
            send: (m: Message) => this.sendTo(session, m)
        };
        room.join(member);

        console.log(util.format('[room] %s joined #%s', session.nick, roomName));
    }

    /**
     * Handle leaving the current room.
     */
    private handleLeave(session: ClientSession): void {
        const room = this.rooms.findRoom(session.sessionId);
        if (!room) {
            this.sendTo(session, errorMessage('Not in a room'));
            return;
        }

        room.leave(session.sessionId);
        this.sendTo(session, serverMessage('Left #' + room.name));
    }

    /**
     * List rooms and users.
     */
    private handleList(session: ClientSession): void {
        const roomList = this.rooms.listRooms();

        this.sendTo(session, serverMessage('--- Rooms ---'));
        for (let i = 0; i < roomList.length; i++) {
            const r = roomList[i];
            const room = this.rooms.get(r.name);
            const nicks = room ? room.getMemberNicks().join(', ') : '';
            this.sendTo(session, serverMessage(
                '  #' + r.name + ' (' + r.members + '): ' + nicks
            ));
        }
        this.sendTo(session, serverMessage('--- ' + this.clientCount + ' client(s) connected ---'));
    }

    /**
     * Send a framed message to a session.
     */
    private sendTo(session: ClientSession, msg: Message): void {
        if (session.socket.destroyed === true) return;
        const frame = frameMessage(msg);
        session.socket.write(frame);
    }

    /**
     * Handle client disconnect.
     */
    private handleDisconnect(sessionId: string, reason: string): void {
        const session = this.sessions.get(sessionId);
        if (!session) return;

        let displayNick = session.nick;
        if (!displayNick) displayNick = 'unauthenticated';
        console.log(util.format('[disconnect] %s (%s): %s',
            displayNick,
            session.remoteAddr,
            reason
        ));

        // Remove from rooms
        this.rooms.removeFromAll(sessionId);

        // Clean up session
        if (session.nick !== '') {
            this.nickToSession.delete(session.nick);
        }
        this.sessions.delete(sessionId);

        if (session.socket.destroyed !== true) {
            session.socket.destroy();
        }
    }
}

/**
 * TCP Chat Server — Message Protocol
 *
 * Length-prefixed message framing for reliable TCP communication.
 * Format: [4-byte big-endian length][UTF-8 JSON payload]
 *
 * Message types:
 *   AUTH    — { type: "auth", nick: string, pass: string }
 *   MSG     — { type: "msg", text: string }
 *   PRIVMSG — { type: "privmsg", to: string, text: string }
 *   JOIN    — { type: "join", room: string }
 *   LEAVE   — { type: "leave" }
 *   LIST    — { type: "list" }
 *   QUIT    — { type: "quit" }
 *   SERVER  — { type: "server", text: string }
 *   ERROR   — { type: "error", text: string }
 */

export interface Message {
    type: string;
    nick?: string;
    pass?: string;
    text?: string;
    to?: string;
    room?: string;
    users?: string[];
    count?: number;
}

/**
 * Frame a message for sending over TCP.
 * Returns a Buffer with 4-byte length prefix followed by JSON payload.
 */
export function frameMessage(msg: Message): Buffer {
    const json = JSON.stringify(msg);
    const payload = Buffer.from(json, 'utf8');
    const frame = Buffer.alloc(4 + payload.length);
    frame.writeUInt32BE(payload.length, 0);
    payload.copy(frame, 4);
    return frame;
}

/**
 * Message parser that handles TCP stream fragmentation.
 * Accumulates data and yields complete messages.
 */
export class MessageParser {
    private buffer: Buffer;

    constructor() {
        this.buffer = Buffer.alloc(0);
    }

    /**
     * Feed incoming data and extract complete messages.
     */
    parse(data: Buffer): Message[] {
        this.buffer = Buffer.concat([this.buffer, data]);
        const messages: Message[] = [];

        while (this.buffer.length >= 4) {
            const msgLen = this.buffer.readUInt32BE(0);

            // Sanity check: reject absurdly large messages
            if (msgLen > 1024 * 1024) {
                // Reset buffer on corrupt frame
                this.buffer = Buffer.alloc(0);
                break;
            }

            if (this.buffer.length < 4 + msgLen) {
                // Incomplete message, wait for more data
                break;
            }

            const payload = this.buffer.subarray(4, 4 + msgLen).toString('utf8');
            this.buffer = this.buffer.subarray(4 + msgLen);

            try {
                const msg = JSON.parse(payload) as Message;
                messages.push(msg);
            } catch (err) {
                // Skip malformed JSON
            }
        }

        return messages;
    }
}

/**
 * Build a server notification message.
 */
export function serverMessage(text: string): Message {
    return { type: 'server', text };
}

/**
 * Build an error message.
 */
export function errorMessage(text: string): Message {
    return { type: 'error', text };
}

/**
 * Build a chat message with sender info.
 */
export function chatMessage(nick: string, text: string): Message {
    return { type: 'msg', nick, text };
}

/**
 * Build a private message.
 */
export function privateMessage(nick: string, to: string, text: string): Message {
    return { type: 'privmsg', nick, to, text };
}

/**
 * Parse a text command from user input.
 * Commands start with / and include: /msg, /join, /leave, /list, /quit, /help
 */
export function parseCommand(input: string): Message | null {
    const trimmed = input.trim();
    if (!trimmed.startsWith('/')) return null;

    const spaceIdx = trimmed.indexOf(' ');
    const cmd = spaceIdx === -1 ? trimmed : trimmed.substring(0, spaceIdx);
    const rest = spaceIdx === -1 ? '' : trimmed.substring(spaceIdx + 1).trim();

    switch (cmd) {
        case '/msg': {
            const targetSpace = rest.indexOf(' ');
            if (targetSpace === -1) return null;
            const to = rest.substring(0, targetSpace);
            const text = rest.substring(targetSpace + 1).trim();
            return { type: 'privmsg', to, text };
        }
        case '/join':
            return { type: 'join', room: rest || 'general' };
        case '/leave':
            return { type: 'leave' };
        case '/list':
            return { type: 'list' };
        case '/quit':
            return { type: 'quit' };
        default:
            return null;
    }
}

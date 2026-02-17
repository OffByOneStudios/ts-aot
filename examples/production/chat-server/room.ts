/**
 * TCP Chat Server — Room Management
 *
 * Chat rooms with member tracking, message broadcast,
 * and event emission for join/leave/message events.
 */

import * as events from 'events';
import { Message, frameMessage, serverMessage, chatMessage } from './protocol';

export interface RoomMember {
    sessionId: string;
    nick: string;
    send: (msg: Message) => void;
}

export class ChatRoom {
    name: string;
    private members: Map<string, RoomMember>;
    private emitter: events.EventEmitter;
    private createdAt: number;

    constructor(name: string) {
        this.name = name;
        this.members = new Map();
        this.emitter = new events.EventEmitter();
        this.createdAt = Date.now();
    }

    get memberCount(): number {
        return this.members.size;
    }

    /**
     * Add a member to the room.
     */
    join(member: RoomMember): void {
        this.members.set(member.sessionId, member);
        this.emitter.emit('join', member.nick);

        // Notify others
        this.broadcastExcept(
            serverMessage(member.nick + ' has joined #' + this.name),
            member.sessionId
        );

        // Welcome the joiner
        member.send(serverMessage('Welcome to #' + this.name + ' (' + this.members.size + ' users)'));
    }

    /**
     * Remove a member from the room.
     */
    leave(sessionId: string): RoomMember | null {
        const member = this.members.get(sessionId);
        if (!member) return null;

        this.members.delete(sessionId);
        this.emitter.emit('leave', member.nick);

        this.broadcastExcept(
            serverMessage(member.nick + ' has left #' + this.name),
            sessionId
        );

        return member;
    }

    /**
     * Get a member by session ID.
     */
    getMember(sessionId: string): RoomMember | undefined {
        return this.members.get(sessionId);
    }

    /**
     * Get the list of member nicknames.
     */
    getMemberNicks(): string[] {
        const nicks: string[] = [];
        const keys = Array.from(this.members.keys());
        for (let i = 0; i < keys.length; i++) {
            const member = this.members.get(keys[i]);
            if (member) {
                nicks.push(member.nick);
            }
        }
        return nicks;
    }

    /**
     * Broadcast a message to all members.
     */
    broadcastAll(msg: Message): void {
        const keys = Array.from(this.members.keys());
        for (let i = 0; i < keys.length; i++) {
            const member = this.members.get(keys[i]);
            if (member) {
                member.send(msg);
            }
        }
    }

    /**
     * Broadcast a message to all members except one.
     */
    broadcastExcept(msg: Message, excludeSessionId: string): void {
        const keys = Array.from(this.members.keys());
        for (let i = 0; i < keys.length; i++) {
            if (keys[i] === excludeSessionId) continue;
            const member = this.members.get(keys[i]);
            if (member) {
                member.send(msg);
            }
        }
    }

    /**
     * Send a chat message from a user to the room.
     */
    sendChat(sessionId: string, text: string): void {
        const sender = this.members.get(sessionId);
        if (!sender) return;

        const msg = chatMessage(sender.nick, text);
        this.emitter.emit('message', sender.nick, text);

        // Send to everyone including sender (echo)
        this.broadcastAll(msg);
    }

    /**
     * Register event handler.
     */
    on(event: string, listener: (...args: any[]) => void): void {
        this.emitter.on(event, listener);
    }
}

/**
 * Manages all chat rooms.
 */
export class RoomManager {
    private rooms: Map<string, ChatRoom>;

    constructor() {
        this.rooms = new Map();
        // Create default room
        this.rooms.set('general', new ChatRoom('general'));
    }

    /**
     * Get or create a room.
     */
    getOrCreate(name: string): ChatRoom {
        let room = this.rooms.get(name);
        if (!room) {
            room = new ChatRoom(name);
            this.rooms.set(name, room);
        }
        return room;
    }

    /**
     * Get a room by name.
     */
    get(name: string): ChatRoom | undefined {
        return this.rooms.get(name);
    }

    /**
     * Remove a member from all rooms they're in.
     */
    removeFromAll(sessionId: string): void {
        const keys = Array.from(this.rooms.keys());
        for (let i = 0; i < keys.length; i++) {
            const room = this.rooms.get(keys[i]);
            if (room) {
                room.leave(sessionId);
            }
        }
    }

    /**
     * Find which room a session is in.
     */
    findRoom(sessionId: string): ChatRoom | null {
        const keys = Array.from(this.rooms.keys());
        for (let i = 0; i < keys.length; i++) {
            const room = this.rooms.get(keys[i]);
            if (room) {
                const member = room.getMember(sessionId);
                if (member) {
                    return room;
                }
            }
        }
        return null;
    }

    /**
     * Get summary of all rooms.
     */
    listRooms(): { name: string; members: number }[] {
        const result: { name: string; members: number }[] = [];
        const keys = Array.from(this.rooms.keys());
        for (let i = 0; i < keys.length; i++) {
            const room = this.rooms.get(keys[i]);
            if (room) {
                result.push({ name: room.name, members: room.memberCount });
            }
        }
        return result;
    }
}

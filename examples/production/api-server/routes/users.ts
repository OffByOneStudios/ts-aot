/**
 * User Routes
 *
 * CRUD operations for users (in-memory storage for demo)
 */

import * as http from 'http';
import { Router, RouteHandler } from './index';

/**
 * User type
 */
interface User {
    id: number;
    name: string;
    email: string;
    createdAt: string;
}

// In-memory user storage
const users: Map<number, User> = new Map();
let nextId = 1;

// Seed some initial data
users.set(nextId++, {
    id: 1,
    name: 'Alice',
    email: 'alice@example.com',
    createdAt: new Date().toISOString()
});
users.set(nextId++, {
    id: 2,
    name: 'Bob',
    email: 'bob@example.com',
    createdAt: new Date().toISOString()
});

/**
 * List all users
 */
const listUsers: RouteHandler = (req, res, params) => {
    const userList: User[] = [];
    users.forEach((user) => {
        userList.push(user);
    });

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
        count: userList.length,
        users: userList
    }));
};

/**
 * Get user by ID
 */
const getUser: RouteHandler = (req, res, params) => {
    const id = parseInt(params.id, 10);

    if (isNaN(id)) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid user ID' }));
        return;
    }

    const user = users.get(id);

    if (!user) {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'User not found' }));
        return;
    }

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(user));
};

/**
 * Create new user
 */
const createUser: RouteHandler = (req, res, params, body) => {
    // Validate required fields
    if (!body || !body.name || !body.email) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Missing required fields: name, email' }));
        return;
    }

    // Create user
    const user: User = {
        id: nextId++,
        name: body.name,
        email: body.email,
        createdAt: new Date().toISOString()
    };

    users.set(user.id, user);

    res.writeHead(201, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(user));
};

/**
 * Update user
 */
const updateUser: RouteHandler = (req, res, params, body) => {
    const id = parseInt(params.id, 10);

    if (isNaN(id)) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid user ID' }));
        return;
    }

    const existing = users.get(id);
    if (!existing) {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'User not found' }));
        return;
    }

    // Update fields
    if (body.name) existing.name = body.name;
    if (body.email) existing.email = body.email;

    users.set(id, existing);

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(existing));
};

/**
 * Delete user
 */
const deleteUser: RouteHandler = (req, res, params) => {
    const id = parseInt(params.id, 10);

    if (isNaN(id)) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid user ID' }));
        return;
    }

    if (!users.has(id)) {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'User not found' }));
        return;
    }

    users.delete(id);

    res.writeHead(204);
    res.end();
};

/**
 * Register user routes
 */
export function userRoutes(router: Router): void {
    router.get('/api/users', listUsers);
    router.get('/api/users/:id', getUser);
    router.post('/api/users', createUser);
    router.put('/api/users/:id', updateUser);
    router.delete('/api/users/:id', deleteUser);
}

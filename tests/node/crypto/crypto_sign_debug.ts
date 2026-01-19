// Debug test for crypto.createSign
import * as crypto from 'crypto';

function user_main(): number {
    console.log("=== Debug Sign Test ===");

    const privateKeyPem = `-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA0Z3VS5JJcds3xfn/ygWyF8PbnGy0AHB7MmVz0UFQG5ALyhBO
qj+95QPaE4k3rLcNsI8tZhS7Bb0EfkNQvG4bYsJTaZ0HvdXCy6+bQb1Cp9FvYvvQ
GJKZjCi6VBHAH7i9YfPBJGn3fJXQwHUNxQhM+E0B5QasLPxKMxGzYPzfXxh5f75V
MJbvqYYHYhC5BYMuvI9JKRZHqu6kvqTYL4fcxNIxiMCwUj4uNqzbk5Uo6N7TZU/7
nOlJl6e8e5Lrb5v6yPCV4V8sGnwfJDuJNqK0XPFphET+D6K7LoY0FVL9D0aGYrJS
WoLrWctwMHwUrf4kMEfJfPDA4mXv+N9dnfKhxQIDAQABAoIBAC0l+GfL6veG0Wgf
ZfNjxpHxqYo6MZ7o2lT0/h9Z7F1JfZq2Z3Xdmm0EFKBuIRjx+TMxHdWTOiHlYuI6
4tptVvhi3lWq3n5NdVo1HU7c+oBDPsq1S0MkQfjgC6MiNy7bLD8pzK3xEhCaJ9BV
nE5+L+fN3zF3tqlLbPzoiZgIIhYGfsOaKClz3Xqt0HgfZs0Y6uUjZNFlnj9iIDXH
IJb/oeX7JLdMhOlhYsR/QK3Y+VZVsqjRNrGBXDFqPicSvrMoxPHQjNP0RnHl94tx
njcK7QrWf7E+RiXA2lB6IQ/F6kuICmUa8R8awq6ZPfGWPx9KoAYFgiIDifb0u9Fg
r3q+pQECgYEA7T2mP2THvZnYF/6D0FKq7rg8vH4IXA9s/i/0xpAN4Zu5d2EJX2Nm
5gKsv7Xl+m34qBNwHOKP8Ee2f4M/MsKt2YXPFKO0AIxv+OQQPX9ESjMH0m5oY8u7
Lck/rK6bfl6O2/A+l6VSqAKEbHQ8PWj2pCZ0qIxpsu0c8TPmvXKQhUECgYEA4qXW
5cBhvcxRRU/xt9TN/M0FbN8fDdF7y1ulHLbDu0VAjsTmReM3JnFJs1oHXg5n2B8j
LdV2bS2Y6sYD0e5qfLgBue1Y7lKnV/gCb3xKQ+sP2mPKxQJdmcoQ7vIUF33qBNpp
jAphJdoHH4GHqfMfbX9lq4lKpLdMNTqIrk7dq9UCgYEAwq4yK7iNhS7dHR0GGQJK
qgpU1+8qTA0apSWoBZe0APmUVylN2X9gMVBsq8WA/i2sspWUd3d2npnCRp9r8P91
bSqPVlPn4FBgSG4GjI5pT8k/pQk2UYN1VmTL4dnQ0S9U0nH1X4gxj0kYrLXqWL6R
/z8qCdX7PWH0w0VZHNpUOIECgYEAxDZ8/h4T0dPFRN/lB4mXUAN1Ckm7X6mLpcfY
y8P7l4y4E0z29jDZ1/aW6W2wGZ5CflE3cPZ1bSD7yBjhS3/uAITGQIw6wKHNpqrz
e0qTkGNzYPJx7DBp19e+J9JD1WN0g7yjYc8JQAuJ4d5S0F3hxqfY0I0w11rq7jxb
LQHlOSUCgYBXq6hpPqy3ssTcL+UT+rCwapf8rD5wUtxE2g7fOEQbNj6kZsK9HeBF
rYB0RlMnKYvJxGG2RJvhCPK7B0d9a0hh3pIXUPxh5dGHPpJX5P2wBppOmekd0cM/
BRznPCFk7hLqfNQmP6TwvGMqJPEwU/fgQ8qGfvHy/nDsS7BrjCq7gA==
-----END RSA PRIVATE KEY-----`;

    console.log("Creating Sign object...");
    const sign = crypto.createSign('SHA256');
    console.log("Sign object created");

    console.log("Calling update...");
    sign.update("test message");
    console.log("Update done");

    console.log("Calling sign.sign()...");
    const signature = sign.sign(privateKeyPem);
    console.log("sign.sign() returned");

    if (signature) {
        console.log("Signature is not null");
        console.log("Signature type: " + typeof signature);
        if (typeof signature === 'object') {
            console.log("Signature is an object");
        }
    } else {
        console.log("Signature is null/undefined");
    }

    // Also test one-shot
    console.log("\nOne-shot test:");
    const data = Buffer.from("test message");
    const sig2 = crypto.sign('SHA256', data, privateKeyPem);
    if (sig2) {
        console.log("One-shot signature length: " + sig2.length);
    } else {
        console.log("One-shot signature is null");
    }

    return 0;
}

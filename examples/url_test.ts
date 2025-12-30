// URL and URLSearchParams API test
console.log("=== URL Tests ===");

// Test URL parsing
const url = new URL("https://user:pass@example.com:8080/path/to/resource?query=1&name=test#hash");
console.log("href: " + url.href);
console.log("origin: " + url.origin);
console.log("protocol: " + url.protocol);
console.log("host: " + url.host);
console.log("hostname: " + url.hostname);
console.log("port: " + url.port);
console.log("pathname: " + url.pathname);
console.log("search: " + url.search);
console.log("hash: " + url.hash);
console.log("username: " + url.username);
console.log("password: " + url.password);

// Test URL methods
console.log("toString(): " + url.toString());
console.log("toJSON(): " + url.toJSON());

console.log("\n=== URLSearchParams Tests ===");

// Test URLSearchParams from query string
const params = url.searchParams;
console.log("searchParams from URL:");
console.log("get('query'): " + params.get("query"));
console.log("get('name'): " + params.get("name"));
console.log("has('query'): " + params.has("query"));
console.log("has('missing'): " + params.has("missing"));
console.log("size: " + params.size);

// Test URLSearchParams constructor
const params2 = new URLSearchParams("foo=bar&baz=qux");
console.log("\nnew URLSearchParams('foo=bar&baz=qux'):");
console.log("get('foo'): " + params2.get("foo"));
console.log("get('baz'): " + params2.get("baz"));
const str = params2.toString();
console.log("toString(): " + str);

// Test append
params2.append("extra", "value");
console.log("\nafter append('extra', 'value'):");
console.log("get('extra'): " + params2.get("extra"));
console.log("toString(): " + params2.toString());

// Test set
params2.set("foo", "newvalue");
console.log("\nafter set('foo', 'newvalue'):");
console.log("get('foo'): " + params2.get("foo"));
console.log("toString(): " + params2.toString());

// Test delete
params2.delete("baz");
console.log("\nafter delete('baz'):");
console.log("has('baz'): " + params2.has("baz"));
console.log("toString(): " + params2.toString());

// Test sort
const params3 = new URLSearchParams("z=1&a=2&m=3");
console.log("\nnew URLSearchParams('z=1&a=2&m=3'):");
console.log("before sort: " + params3.toString());
params3.sort();
console.log("after sort: " + params3.toString());

console.log("\n=== All URL tests passed! ===");

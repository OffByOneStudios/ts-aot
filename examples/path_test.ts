import * as path from 'path';

function test() {
    console.log("Testing path module...");

    const p1 = "foo";
    const p2 = "bar";
    const joined = path.join(p1, p2);
    console.log("join(foo, bar):", joined);

    const joinedVariadic = path.join("a", "b", "c", "d");
    console.log("join(a, b, c, d):", joinedVariadic);

    const normalized = path.normalize("foo/bar//baz/..");
    console.log("normalize(foo/bar//baz/..):", normalized);

    const isAbs = path.isAbsolute("/foo/bar");
    console.log("isAbsolute(/foo/bar):", isAbs);

    const base = path.basename("/foo/bar/baz.txt");
    console.log("basename(/foo/bar/baz.txt):", base);

    const baseWithExt = path.basename("/foo/bar/baz.txt", ".txt");
    console.log("basename(/foo/bar/baz.txt, .txt):", baseWithExt);

    const dir = path.dirname("/foo/bar/baz.txt");
    console.log("dirname(/foo/bar/baz.txt):", dir);

    const ext = path.extname("/foo/bar/baz.txt");
    console.log("extname(/foo/bar/baz.txt):", ext);

    const resolved = path.resolve("foo", "bar", "baz");
    console.log("resolve(foo, bar, baz):", resolved);

    const relative = path.relative("/data/orandea/test/aaa", "/data/orandea/impl/bbb");
    console.log("relative(/data/orandea/test/aaa, /data/orandea/impl/bbb): " + relative);

    console.log("sep: " + path.sep);
    console.log("delimiter: " + path.delimiter);

    console.log("Testing path.parse()...");
    const p3 = "C:\\path\\to\\file.txt";
    const parsed = path.parse(p3);
    console.log("Root: " + parsed.root);
    console.log("Dir: " + parsed.dir);
    console.log("Base: " + parsed.base);
    console.log("Ext: " + parsed.ext);
    console.log("Name: " + parsed.name);

    const formatted = path.format(parsed);
    console.log("Formatted: " + formatted);

    console.log("Testing path.toNamespacedPath()...");
    console.log("Namespaced: " + path.toNamespacedPath(p3));

    console.log("Testing path.win32 and path.posix...");
    console.log("win32.join: " + path.win32.join("a", "b"));
    console.log("posix.join: " + path.posix.join("a", "b"));

    console.log("win32.sep: " + path.win32.sep);
    console.log("posix.sep: " + path.posix.sep);

    console.log("win32.isAbsolute(C:\\\\foo): " + path.win32.isAbsolute("C:\\\\foo"));
    console.log("posix.isAbsolute(C:\\\\foo): " + path.posix.isAbsolute("C:\\\\foo"));

    console.log("win32.basename(a\\\\b\\\\c.txt): " + path.win32.basename("a\\\\b\\\\c.txt"));
    console.log("posix.basename(a\\\\b\\\\c.txt): " + path.posix.basename("a\\\\b\\\\c.txt"));

    const parsedWin32 = path.win32.parse("C:\\\\path\\\\to\\\\file.txt");
    console.log("win32.parse root: " + parsedWin32.root);
    console.log("win32.parse dir: " + parsedWin32.dir);

    const parsedPosix = path.posix.parse("/path/to/file.txt");
    console.log("posix.parse root: " + parsedPosix.root);
    console.log("posix.parse dir: " + parsedPosix.dir);
}

test();

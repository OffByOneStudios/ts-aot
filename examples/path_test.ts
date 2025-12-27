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
    console.log("relative(/data/orandea/test/aaa, /data/orandea/impl/bbb):", relative);

    console.log("sep:", path.sep);
    console.log("delimiter:", path.delimiter);
}

test();

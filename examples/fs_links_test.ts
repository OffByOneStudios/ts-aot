import * as fs from 'fs';

async function test() {
    const testFile = 'test_link.txt';
    const hardLink = 'test_hardlink.txt';
    const symLink = 'test_symlink.txt';
    const hardLinkPromise = 'test_hardlink_promise.txt';
    const symLinkPromise = 'test_symlink_promise.txt';

    // Cleanup
    try { fs.unlinkSync(testFile); } catch {}
    try { fs.unlinkSync(hardLink); } catch {}
    try { fs.unlinkSync(symLink); } catch {}
    try { fs.unlinkSync(hardLinkPromise); } catch {}
    try { fs.unlinkSync(symLinkPromise); } catch {}

    console.log('Creating test file...');
    fs.writeFileSync(testFile, 'Hello Links!');

    console.log('Creating hard link...');
    fs.linkSync(testFile, hardLink);
    const hardStat = fs.statSync(hardLink);
    console.log('Hard link size:', hardStat.size);

    console.log('Creating symlink...');
    // Note: On Windows, symlink requires special privileges or developer mode.
    // We'll try it and see.
    try {
        fs.symlinkSync(testFile, symLink);
        console.log('Symlink created.');

        const lstat = fs.lstatSync(symLink);
        console.log('Is symbolic link (lstat):', lstat.isSymbolicLink());

        const stat = fs.statSync(symLink);
        console.log('Is symbolic link (stat):', stat.isSymbolicLink());

        const target = fs.readlinkSync(symLink);
        console.log('Readlink target:', target);

        const real = fs.realpathSync(symLink);
        console.log('Realpath:', real);
    } catch (e) {
        console.log('Symlink failed (likely permissions):', e);
    }

    console.log('Testing fs.promises...');
    await fs.promises.link(testFile, 'test_hardlink_promise.txt');
    const pStat = await fs.promises.stat('test_hardlink_promise.txt');
    console.log('Promise hard link size:', pStat.size);

    await fs.promises.symlink(testFile, 'test_symlink_promise.txt');
    const pLstat = await fs.promises.lstat('test_symlink_promise.txt');
    console.log('Promise lstat isSymbolicLink:', pLstat.isSymbolicLink());
    const pReadlink = await fs.promises.readlink('test_symlink_promise.txt');
    console.log('Promise readlink:', pReadlink);
    const pRealpath = await fs.promises.realpath('test_symlink_promise.txt');
    console.log('Promise realpath:', pRealpath);

    console.log('Done.');

// CHECK: Hard link size:
// CHECK: 18
// CHECK: Is symbolic link (lstat):
// CHECK: true
// CHECK: Is symbolic link (stat):
// CHECK: false
// CHECK: Readlink target:
// CHECK: test_link.txt
// CHECK: Promise hard link size:
// CHECK: 18
// CHECK: Promise lstat isSymbolicLink:
// CHECK: true
// CHECK: Promise readlink:
// CHECK: test_link.txt
// CHECK: Done.
}

test();

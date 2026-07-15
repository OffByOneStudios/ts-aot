// Buffer bridge: file bytes -> native memory in ONE copy (arr.copyFrom),
// process natively, and back out (arr.toBuffer). The unlock for
// parser/codec workloads — previously bytes crossed one boxed element at
// a time.
"use fast";
import * as fs from 'fs';

function user_main(): number {
  let failed: i64 = 0;
  fs.writeFileSync("tmp/test_na_buffer_in.bin", "abcxyz");
  const buf = fs.readFileSync("tmp/test_na_buffer_in.bin");

  const arr = new NativeArray<u8>(6, Allocator.Temp);
  const copied = arr.copyFrom(buf);
  if (copied !== 6) { console.log("FAIL copied=" + copied); failed = failed + 1; }

  let sum: i64 = 0;
  for (let i: i64 = 0; i < arr.length; i++) { sum = sum + arr.get(i); }
  if (sum !== 657) { console.log("FAIL sum=" + sum); failed = failed + 1; }

  // uppercase in native memory: 'a'..'z' -> subtract 32
  for (let i: i64 = 0; i < arr.length; i++) {
    const c = arr.get(i);
    if (c >= 97 && c <= 122) { arr.set(i, c - 32); }
  }

  const out = arr.toBuffer();
  fs.writeFileSync("tmp/test_na_buffer_out.bin", out);
  // Round-trip verification through a SECOND copyFrom: read the written
  // file back as a Buffer and compare bytes in native memory. (A utf8
  // string compare would hit a pre-existing === bug on read-back strings.)
  const round = fs.readFileSync("tmp/test_na_buffer_out.bin");
  const chk = new NativeArray<u8>(6, Allocator.Temp);
  chk.copyFrom(round);
  let ok = true;
  if (chk.get(0) !== 65) ok = false;  // A
  if (chk.get(1) !== 66) ok = false;  // B
  if (chk.get(2) !== 67) ok = false;  // C
  if (chk.get(3) !== 88) ok = false;  // X
  if (chk.get(4) !== 89) ok = false;  // Y
  if (chk.get(5) !== 90) ok = false;  // Z
  if (!ok) {
    console.log("FAIL roundtrip bytes " + chk.get(0) + "," + chk.get(1) + "," +
                chk.get(2) + "," + chk.get(3) + "," + chk.get(4) + "," + chk.get(5));
    failed = failed + 1;
  }

  console.log("buffer bridge done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}

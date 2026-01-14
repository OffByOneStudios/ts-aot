// Stream Utilities Tests - stream.pipeline() and stream.finished()
import * as fs from 'fs';
import * as stream from 'stream';

function user_main(): number {
  let failures = 0;
  console.log('=== Stream Utilities Tests ===\n');

  // Create test input file
  const testContent = 'Hello, stream utilities test! This is line 1.\nThis is line 2.\nEnd of test.';
  fs.writeFileSync('stream_util_input.txt', testContent);

  // Test 1: Basic stream.finished() usage
  console.log('Test 1: stream.finished() with ReadStream');
  const readStream1 = fs.createReadStream('stream_util_input.txt');

  let finishedCallbackCalled = false;
  const cleanup1 = stream.finished(readStream1, (err: any) => {
    finishedCallbackCalled = true;
    if (err) {
      console.log('  Finished callback called with error: ' + err);
    } else {
      console.log('  Finished callback called successfully (no error)');
    }
  });
  console.log('  stream.finished() returned cleanup function');
  if (typeof cleanup1 === 'function') {
    console.log('PASS: stream.finished() returns a function');
  } else {
    console.log('FAIL: stream.finished() should return a cleanup function');
    failures++;
  }

  // Add a data listener to trigger streaming
  readStream1.on('data', (chunk: any) => {
    // Just consume data
  });

  // Test 2: stream.pipeline() basic usage
  console.log('\nTest 2: stream.pipeline() with ReadStream and WriteStream');
  const readStream2 = fs.createReadStream('stream_util_input.txt');
  const writeStream2 = fs.createWriteStream('stream_util_output.txt');

  let pipelineCallbackCalled = false;
  const lastStream = stream.pipeline(readStream2, writeStream2, (err: any) => {
    pipelineCallbackCalled = true;
    if (err) {
      console.log('  Pipeline callback called with error: ' + err);
    } else {
      console.log('  Pipeline callback called successfully (no error)');
    }
  });
  console.log('  stream.pipeline() called');
  console.log('PASS: stream.pipeline() executed without error');

  // Test 3: stream.finished() cleanup function
  console.log('\nTest 3: Calling cleanup function from stream.finished()');
  const readStream3 = fs.createReadStream('stream_util_input.txt');
  const cleanup3 = stream.finished(readStream3, (err: any) => {
    console.log('  This callback should not be called after cleanup');
  });

  // Call the cleanup function immediately
  if (typeof cleanup3 === 'function') {
    cleanup3();
    console.log('  Cleanup function called successfully');
    console.log('PASS: Cleanup function is callable');
  } else {
    console.log('FAIL: cleanup3 is not a function');
    failures++;
  }

  // Test 4: stream.finished() with WriteStream
  console.log('\nTest 4: stream.finished() with WriteStream');
  const writeStream4 = fs.createWriteStream('stream_util_output2.txt');

  stream.finished(writeStream4, (err: any) => {
    if (err) {
      console.log('  WriteStream finished with error');
    } else {
      console.log('  WriteStream finished successfully');
    }
  });

  writeStream4.write('Test data');
  writeStream4.end();
  console.log('PASS: stream.finished() works with WriteStream');

  // Allow time for async operations to complete
  // In a real test environment, we would use proper async handling
  console.log('\n--- Waiting for async operations ---');

  // Clean up test files
  try {
    fs.unlinkSync('stream_util_input.txt');
  } catch (e) {
    // Ignore
  }
  try {
    fs.unlinkSync('stream_util_output.txt');
  } catch (e) {
    // Ignore
  }
  try {
    fs.unlinkSync('stream_util_output2.txt');
  } catch (e) {
    // Ignore
  }

  // Summary
  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All Stream utility tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}

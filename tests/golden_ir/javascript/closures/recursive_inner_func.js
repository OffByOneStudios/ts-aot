// Test: inner function declaration calling itself recursively
// Bug: self-reference cell update failed because displayName wasn't set,
// so the name comparison "next" != "next_0" failed in HIRToLLVM.

function dispatch(items) {
  var idx = 0;

  next();

  function next() {
    if (idx >= items.length) {
      console.log("done, processed " + idx);
      return;
    }
    var item = items[idx++];
    console.log("item: " + item);
    next();
  }
}

dispatch(["a", "b", "c"]);

// OUTPUT: item: a
// OUTPUT: item: b
// OUTPUT: item: c
// OUTPUT: done, processed 3

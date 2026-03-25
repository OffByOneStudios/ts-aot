// Test: inner function capture works when parent has parameters
// Bug fixed: hoistAllVars was gated on allParamsUntyped

function processData(data, label) {
  var total = 0;

  accumulate();
  console.log(label + ": total=" + total);

  function accumulate() {
    for (var i = 0; i < data.length; i++) {
      total += data[i];
    }
  }
}

processData([10, 20, 30], "sum");

// OUTPUT: sum: total=60

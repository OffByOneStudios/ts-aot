function user_main(): number {
  let count = 0;
  const interval = setInterval(() => {
    count++;
    console.log("tick " + count);
    if (count === 3) {
      clearInterval(interval);  // interval is captured in closure
      console.log("cleared");
    }
  }, 30);
  
  return 0;
}

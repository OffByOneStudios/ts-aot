function user_main(): number {
  let count = 0;
  let interval: number = 0;  // Declare first
  interval = setInterval(() => {
    count++;
    console.log("tick " + count);
    if (count === 3) {
      clearInterval(interval);
      console.log("cleared");
    }
  }, 30);
  
  return 0;
}

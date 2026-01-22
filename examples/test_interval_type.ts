function user_main(): number {
  const x = setInterval(() => {
    console.log("tick");
  }, 100);
  
  console.log("Type test - x should be int");
  clearInterval(x);
  console.log("clearInterval called");
  
  return 0;
}

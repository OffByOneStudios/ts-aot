class Animal {
  constructor(name) { this.name = name; }
  speak() { return this.name + " makes a sound"; }
}

class Dog extends Animal {
  constructor(name) { super(name); }
  speak() { return this.name + " barks"; }
  parentSpeak() { return super.speak(); }
}

var d = new Dog("Rex");
console.log(d.speak());
console.log(d.parentSpeak());
console.log("name:", d.name);
console.log("d instanceof Dog:", d instanceof Dog);
console.log("d instanceof Animal:", d instanceof Animal);

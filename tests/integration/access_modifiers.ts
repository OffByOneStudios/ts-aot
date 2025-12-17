class Base {
    public pub: number = 1;
    protected prot: number = 2;
    private priv: number = 3;

    testBase() {
        ts_console_log(this.pub);
        ts_console_log(this.prot);
        ts_console_log(this.priv);
    }
}

class Derived extends Base {
    testDerived() {
        ts_console_log(this.pub);
        ts_console_log(this.prot);
        // ts_console_log(this.priv); // Error: private in Base
    }
}

function main() {
    const b = new Base();
    ts_console_log(b.pub);
    // ts_console_log(b.priv); // Error: private
    // ts_console_log(b.prot); // Error: protected
    b.testBase();

    const d = new Derived();
    d.testDerived();
}

// Comprehensive JSX test

function user_main(): number {
    // Test self-closing element with multiple props
    const input = <input type="text" value="hello" placeholder="Enter text" />;

    console.log("=== Self-closing element ===");
    console.log("Tag type:", input.type);
    console.log("Props object:", input.props);
    console.log("  type:", input.props.type);
    console.log("  value:", input.props.value);
    console.log("  placeholder:", input.props.placeholder);

    // Test element with children
    const div = <div className="container">
        Hello World
    </div>;

    console.log("\n=== Element with children ===");
    console.log("Tag type:", div.type);
    console.log("Props.className:", div.props.className);
    console.log("Children array:", div.children);

    // Test nested elements
    const parent = <div id="parent">
        <span className="child">Nested content</span>
    </div>;

    console.log("\n=== Nested elements ===");
    console.log("Parent type:", parent.type);
    console.log("Parent props.id:", parent.props.id);
    console.log("Has children:", parent.children.length > 0 ? "yes" : "no");

    // Test boolean attribute
    const checkbox = <input type="checkbox" checked />;
    console.log("\n=== Boolean attribute ===");
    console.log("Checkbox type:", checkbox.props.type);
    console.log("Checked:", checkbox.props.checked);

    return 0;
}

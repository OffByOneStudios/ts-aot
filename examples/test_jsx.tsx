// JSX Test - transforms JSX to createElement calls

// Simple createElement function for testing
function createElement(tag: any, props: any, ...children: any[]): any {
    return {
        type: tag,
        props: props || {},
        children: children
    };
}

function user_main(): number {
    // Test 1: Simple element with no props
    const div = <div></div>;
    console.log("Test 1 - Simple element:");
    console.log(div.type);

    // Test 2: Self-closing element
    const br = <br />;
    console.log("Test 2 - Self-closing:");
    console.log(br.type);

    // Test 3: Element with props
    const input = <input type="text" value="hello" />;
    console.log("Test 3 - With props:");
    console.log(input.type);
    console.log(input.props.type);
    console.log(input.props.value);

    // Test 4: Element with children
    const container = <div><span></span></div>;
    console.log("Test 4 - With children:");
    console.log(container.type);
    console.log(container.children.length);

    // Test 5: Element with text child
    const text = <p>Hello World</p>;
    console.log("Test 5 - With text:");
    console.log(text.type);

    // Test 6: Element with expression
    const name = "Claude";
    const greeting = <span>Hello {name}</span>;
    console.log("Test 6 - With expression:");
    console.log(greeting.type);

    console.log("All JSX tests passed!");
    return 0;
}

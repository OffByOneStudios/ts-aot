// JSX Props Simple Test - direct property access

function user_main(): number {
    // Create a simple JSX element with props
    const elem = <input type="text" value="hello" />;

    // Log element type
    console.log("Element type:");
    console.log(elem.type);

    // Log element props
    console.log("Element props:");
    console.log(elem.props);

    // Try direct property access
    console.log("Props.type:");
    console.log(elem.props.type);

    console.log("Props.value:");
    console.log(elem.props.value);

    return 0;
}

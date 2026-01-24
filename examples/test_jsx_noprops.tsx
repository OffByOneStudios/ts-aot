// JSX Test - no props

function user_main(): number {
    const elem = <br />;

    console.log("Type:");
    console.log(elem.type);

    console.log("Props:");
    console.log(elem.props);

    console.log("Children:");
    console.log(elem.children);

    console.log("Children length:");
    console.log(elem.children.length);

    return 0;
}

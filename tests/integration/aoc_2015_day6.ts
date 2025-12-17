function main() {
    const input = fs.readFileSync("tests/integration/aoc_2015_day6.txt");
    const lines = input.split("\n");
    const grid = new Array(1000000);
    
    // Initialize grid (optional since CreateSized zeroes it)
    // But let's be safe and assume it's zeroed.

    for (let i = 0; i < lines.length; i = i + 1) {
        const line = lines[i];
        if (line.length == 0) continue;

        let action = 0; // 0: off, 1: on, 2: toggle
        let startStr = "";
        let endStr = "";

        if (line.startsWith("turn on ")) {
            action = 1;
            const parts = line.substring(8).split(" through ");
            startStr = parts[0];
            endStr = parts[1];
        } else if (line.startsWith("turn off ")) {
            action = 0;
            const parts = line.substring(9).split(" through ");
            startStr = parts[0];
            endStr = parts[1];
        } else if (line.startsWith("toggle ")) {
            action = 2;
            const parts = line.substring(7).split(" through ");
            startStr = parts[0];
            endStr = parts[1];
        }

        const startParts = startStr.split(",");
        const startX = parseInt(startParts[0]);
        const startY = parseInt(startParts[1]);

        const endParts = endStr.split(",");
        const endX = parseInt(endParts[0]);
        const endY = parseInt(endParts[1]);

        for (let y = startY; y <= endY; y = y + 1) {
            for (let x = startX; x <= endX; x = x + 1) {
                const idx = y * 1000 + x;
                if (action == 1) {
                    grid[idx] = 1;
                } else if (action == 0) {
                    grid[idx] = 0;
                } else {
                    if (grid[idx] == 1) {
                        grid[idx] = 0;
                    } else {
                        grid[idx] = 1;
                    }
                }
            }
        }
    }

    let count = 0;
    for (let i = 0; i < 1000000; i = i + 1) {
        if (grid[i] == 1) {
            count = count + 1;
        }
    }
    console.log(count);
}

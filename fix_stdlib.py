import os

file_path = r'e:\src\github.com\cgrinker\ts-aoc\src\compiler\analysis\Analyzer_StdLib.cpp'
with open(file_path, 'r') as f:
    content = f.read()

# Find the first occurrence of Math definition
index = content.find('symbols.define("Math", mathType);')
if index != -1:
    # Find the end of that line
    end_of_line = content.find('\n', index)
    if end_of_line != -1:
        new_content = content[:end_of_line+1] + "\n}\n} // namespace ts\n"
        with open(file_path, 'w') as f:
            f.write(new_content)
        print("File fixed successfully.")
    else:
        print("Could not find end of line.")
else:
    print("Could not find Math definition.")

import sys
import re

def remove_comments(text):
    def replacer(match):
        s = match.group(0)
        if s.startswith('/'):
            # Return spaces to preserve character offsets if possible, 
            # or just a single space for block comments to prevent token merging.
            if s.startswith('//'):
                return ""
            else:
                # For block comments, preserve newlines to maintain line numbers
                return re.sub(r'[^\r\n]', ' ', s)
        else:
            return s
    
    # Pattern explanation:
    # //.*?$           : single line comments
    # /\*.*?\*/        : multi-line comments
    # \'(?:\\.|[^\\\'])\' : character literals
    # "(?:\\.|[^\\"])*"  : string literals
    pattern = re.compile(
        r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\\\'])\'|"(?:\\.|[^\\"])*"',
        re.DOTALL | re.MULTILINE
    )
    return re.sub(pattern, replacer, text)

if __name__ == "__main__":
    for arg in sys.argv[1:]:
        try:
            with open(arg, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            new_content = remove_comments(content)
            with open(arg, 'w', encoding='utf-8') as f:
                f.write(new_content)
            print(f"Cleaned {arg}")
        except Exception as e:
            print(f"Error processing {arg}: {e}")

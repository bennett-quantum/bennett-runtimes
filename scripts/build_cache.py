"""Content-addressed intermediates; the publication never contains cache metadata."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

class BuildCache:
    def __init__(self, directory, headers):
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=True)
        self.headers = headers
        self.hits = 0
        self.misses = 0

    def execute(self, args, root):
        arguments = list(map(str, args))
        if '-o' not in arguments or arguments[arguments.index('-o')+1] == '-':
            return subprocess.check_output(arguments, text=True, cwd=root)
        output_index = arguments.index('-o')+1
        output = Path(arguments[output_index])
        tool = Path(arguments[0]).resolve()
        stat = tool.stat()
        tokens = ["stable-source-paths", str(tool), stat.st_size, stat.st_mtime_ns, self.headers]
        for index, argument in enumerate(arguments[1:], 1):
            if index in (output_index-1, output_index):
                continue
            prefix, separator, value = argument.partition('=')
            path = Path(value if separator else argument)
            path = path if path.is_absolute() else root/path
            if path.is_file():
                tokens.append([prefix if separator else 'input',
                               str(Path(argument)) if path.suffix == '.c' else '',
                               hashlib.sha256(path.read_bytes()).hexdigest()])
            elif path.is_dir():
                # Header content is included once, independently of build paths.
                parent = '<build>' if path.parent.name.startswith('build-') else path.parent.name
                tokens.append(['include-directory', parent, path.name])
            else:
                tokens.append(argument)
        key = hashlib.sha256(json.dumps(tokens).encode()).hexdigest()
        cached = self.directory/key
        if cached.exists():
            output.write_bytes(cached.read_bytes())
            self.hits += 1
            return ''
        result = subprocess.check_output(arguments, text=True, cwd=root)
        temporary = cached.with_suffix(f'.{os.getpid()}.new')
        temporary.write_bytes(output.read_bytes())
        temporary.replace(cached)
        self.misses += 1
        return result

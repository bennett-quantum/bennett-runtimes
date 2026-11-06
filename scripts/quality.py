#!/usr/bin/env python3
"""Repository ratchet: lexical C and AST Python metrics, documented in docs/quality.md."""
import argparse
import ast
from collections import Counter
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '<!-- quality-baseline -->'
END = '<!-- /quality-baseline -->'
PROCESS = re.compile(r'\b(?:brt-[a-z0-9]+(?:\.\d+)?|(?:sol|quality)-runtimes|shift [0-9]+|review round[- ]?[0-9]+)\b', re.I)
LITTER = re.compile(r'(?:__pycache__|\.pyc$|\.log$|LAST-RUN|PROGRESS|(?:^|/)(?:LOCK|[^/]*\.lock)$|\.darc)')

def python_functions(text):
    functions = []
    tree = ast.parse(text)
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        mass = 0
        def branches(parent, nesting=0):
            nonlocal mass
            for child in ast.iter_child_nodes(parent):
                if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
                    continue
                branch = isinstance(child, (ast.If, ast.For, ast.While, ast.ExceptHandler, ast.IfExp, ast.Match))
                if branch:
                    mass += 1+nesting
                if isinstance(child, ast.BoolOp):
                    mass += len(child.values)-1
                branches(child, nesting+int(branch))
        branches(node)
        wrapper = len(node.body)==1 and isinstance(node.body[0], (ast.Return, ast.Expr)) and isinstance(node.body[0].value, ast.Call)
        functions.append((node.end_lineno-node.lineno+1, mass, int(wrapper)))
    return functions

def c_functions(text):
    # Preserve newlines while removing comments and literals before tokenising.
    clean = re.sub(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""",
                   lambda match: '\n'*match[0].count('\n')+' ', text, flags=re.S)
    functions = []
    depth = 0
    start = None
    line = 1
    previous = ''
    body = []
    for match in re.finditer(r'\n|[A-Za-z_]\w*|&&|\|\||[{}()?;]', clean):
        token = match[0]
        if token == '\n':
            line += 1
            continue
        if token == '{':
            if depth == 0 and previous == ')':
                start = line
                body = []
            depth += 1
        elif token == '}':
            depth -= 1
            if depth == 0 and start is not None:
                # Nesting-weighted decision tokens: a stable lexical proxy,
                # rather than a claim of Sonar-compatible cognitive complexity.
                nesting = mass = 0
                for item in body:
                    if item == '{': nesting += 1
                    elif item == '}': nesting -= 1
                    elif item in ('if', 'for', 'while', 'switch', 'case', '?'):
                        mass += 1+nesting
                    elif item in ('&&', '||'): mass += 1
                wrapper = bool(re.fullmatch(r'(?:return)?[A-Za-z_]\w*\([^;{}]*\);', ''.join(body)))
                functions.append((line-start+1, mass, int(wrapper)))
                start = None
        if start is not None and not (token == '{' and depth == 1):
            body.append(token)
        previous = token
    return functions

def measure(root=ROOT):
    files = {}
    identifiers = []
    tracked = subprocess.check_output(['git', '-C', root, 'ls-files'], text=True).splitlines()
    paths = sorted({*tracked, *(str(p.relative_to(root)) for directory in ('src', 'scripts', 'julia-shim')
                               for p in (root/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)})
    for name in paths:
        path = root/name
        if not path.is_file():
            continue
        if path.parts[len(root.parts)] not in ('src', 'scripts', 'julia-shim'):
            continue
        if path.suffix not in ('.c', '.h', '.cpp', '.py', '.ll', '.S', ''):
            continue
        text = path.read_text()
        if name != 'scripts/quality.py':
            identifiers.extend(f'{name}:{index}' for index, line in enumerate(text.splitlines(), 1) if PROCESS.search(line))
        functions = python_functions(text) if path.suffix == '.py' or text.startswith('#!/usr/bin/env python3') else c_functions(text)
        lengths = [length for length, _, _ in functions]
        buckets = Counter('1-20' if n<=20 else '21-50' if n<=50 else '51-100' if n<=100 else '101-300' if n<=300 else '301+' for n in lengths)
        files[name] = {'functions': len(functions), 'lengths': dict(sorted(buckets.items())),
                       'max_lines': max(lengths, default=0),
                       **{f'over_{bound}': sum(n>bound for n in lengths) for bound in (20, 50, 100, 300)},
                       'complexity_mass': sum(mass for _, mass, _ in functions),
                       'trivial_wrappers': sum(wrapper for _, _, wrapper in functions)}
    return {'files': files, 'litter': [name for name in tracked if LITTER.search(name)], 'process_identifiers': identifiers}

def regressions(baseline, current):
    errors = []
    for metric in ('litter', 'process_identifiers'):
        if len(current[metric]) > len(baseline[metric]):
            errors.append(f'{metric}: {current[metric]}')
    for name, values in current['files'].items():
        old = baseline['files'].get(name, {})
        for metric in ('max_lines', 'over_20', 'over_50', 'over_100', 'over_300', 'complexity_mass', 'trivial_wrappers'):
            if values[metric] > old.get(metric, 0):
                errors.append(f'{name} {metric}: {old.get(metric, 0)} -> {values[metric]}')
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    document = ROOT/'docs/quality.md'
    current = measure()
    text = document.read_text()
    if args.record:
        block = BEGIN+'\n```json\n'+json.dumps(current, indent=2, sort_keys=True)+'\n```\n'+END
        text = re.sub(re.escape(BEGIN)+'.*?'+re.escape(END), lambda _: block, text, flags=re.S) if BEGIN in text else text+'\n'+block+'\n'
        document.write_text(text)
        return
    recorded = text.split(BEGIN, 1)[1].split(END, 1)[0].split('```json', 1)[1].split('```', 1)[0]
    errors = regressions(json.loads(recorded), current)
    if errors:
        raise SystemExit('Quality baseline regression:\n'+'\n'.join(errors))
    print('quality: function lengths, complexity mass, wrappers, litter and process identifiers PASS')

if __name__ == '__main__':
    main()

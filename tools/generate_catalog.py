"""Rebuild resources/parameters.json from the upstream server README snapshot."""
import html
import json
import re
from pathlib import Path
from russian_catalog import localize

ROOT = Path(__file__).resolve().parents[1]
TYPES = {"string": 0, "integer": 1, "number": 2, "boolean": 3, "path": 4, "choice": 5, "arguments": 6}
popular = ['model', 'ctx-size', 'gpu-layers', 'host', 'port', 'threads', 'parallel', 'flash-attn',
           'batch-size', 'ubatch-size', 'cache-type-k', 'cache-type-v', 'temp', 'top-p', 'min-p',
           'fit', 'fit-target', 'mmproj', 'spec-draft-model', 'lora', 'tensor-split', 'split-mode']
choices = {
    'flash-attn': ['on', 'off', 'auto'], 'fit': ['on', 'off'], 'reasoning': ['on', 'off', 'auto'],
    'log-colors': ['on', 'off', 'auto'], 'split-mode': ['none', 'layer', 'row', 'tensor'],
    'load-mode': ['auto', 'none', 'mmap', 'mlock', 'mmap+mlock', 'dio'],
    'lazy-mode': ['on', 'auto', 'off'], 'rope-scaling': ['none', 'linear', 'yarn'],
    'pooling': ['none', 'mean', 'cls', 'last', 'rank'], 'numa': ['distribute', 'isolate', 'numactl'],
    'reasoning-format': ['auto', 'none', 'deepseek', 'deepseek-legacy'],
    'spec-draft-sampling': ['greedy', 'probabilistic'],
}
for key in ['cache-type-k', 'cache-type-v', 'spec-draft-type-k', 'spec-draft-type-v']:
    choices[key] = ['f32', 'f16', 'bf16', 'q8_0', 'q4_0', 'q4_1', 'iq4_nl', 'q5_0', 'q5_1']
labels = {'model': 'Основная модель', 'ctx-size': 'Размер контекста', 'gpu-layers': 'Слои на GPU',
          'host': 'Адрес сервера', 'port': 'Порт', 'threads': 'Потоки CPU', 'parallel': 'Параллельные запросы',
          'mmproj': 'Мультимодальный проектор', 'spec-draft-model': 'Draft-модель', 'lora': 'LoRA-адаптеры',
          'flash-attn': 'Flash Attention', 'temp': 'Температура', 'fit': 'Подбор по памяти'}
params = []
group = ''
for line in (ROOT / 'resources/server-README.md').read_text(encoding='utf-8').split('<!-- HELP_END -->')[0].splitlines():
    if line.startswith('### '):
        group = line[4:]
    match = re.match(r'\| `(.+?)` \| (.*) \|$', line)
    if not match:
        continue
    signature, desc = match.groups()
    signature = signature.replace('\\|', '|')
    # Stop alias parsing before the first argument placeholder.
    alias_text = signature
    aliases = []
    while True:
        m = re.match(r'^(--?[\w.-]+)(?:,\s*|\s+|$)', alias_text)
        if not m:
            break
        aliases.append(m[1])
        consumed = m.end()
        had_comma = ',' in m[0]
        alias_text = alias_text[consumed:]
        if not had_comma:
            break
    flag = next((a for a in aliases if a.startswith('--') and not a.startswith('--no-')), aliases[0])
    key = flag[2:]
    text = html.unescape(re.sub(r'<[^>]+>', ' ', desc.replace('<br/>', '\n'))).replace('\\|', '|')
    text = re.sub(r'\[([^]]+)\]\([^)]+\)', r'\1', text)
    default_match = re.search(r"default:\s*([^\n)]+)", text)
    raw_default = default_match[1].strip() if default_match else ''
    default = None
    typ = 'string'
    if not alias_text:
        typ = 'boolean'
        if raw_default in ['true', 'enabled', 'prefill enabled']:
            default = True
        elif raw_default in ['false', 'disabled']:
            default = False
    elif key in choices:
        typ = 'choice'
        default = raw_default.strip("'\"").split(',')[0]
        if default not in choices[key]:
            default = None
    elif key == 'control-vector-layer-range':
        typ = 'arguments'
    elif re.search(r'\b(FNAME|FILE|PATH|DIR|JINJA_TEMPLATE_FILE)\b', alias_text):
        typ = 'path'
    elif re.fullmatch(r'(N|SEED|PORT|INDEX|SECONDS|SIMILARITY|P|L|<0.*?>)', alias_text):
        typ = 'integer'
        if re.search(r'(temp|penalty|factor|scale|freq|top-p|min-p|sigma|probability|threshold|typical|decay|mirostat-|similarity|draft-p|video-fps|spec-synth-len|adaptive-target)', key):
            typ = 'number'
        if key in ['dry-penalty-last-n']:
            typ = 'integer'
        n = re.match(r'-?\d+(?:\.\d+)?', raw_default)
        if n:
            default = float(n[0]) if typ == 'number' else int(float(n[0]))
    else:
        if raw_default and raw_default not in ['unused', 'none', 'disabled'] and not raw_default.startswith(('same ', 'value ', 'loaded ', 'read ', 'follows ', 'template ', 'search ')):
            default = raw_default.strip("'\"")
    if key in ['gpu-layers', 'spec-draft-ngl']:
        typ, default = 'string', 'auto'
    if key in ['host', 'cors-methods', 'api-prefix', 'path']:
        typ = 'path' if key == 'path' else 'string'
        default = raw_default
    negative = next((a for a in aliases if a.startswith('--no-')), '')
    params.append(dict(name=flag, label=labels.get(key, key), type=TYPES[typ], default=default,
                       priority=(popular.index(key) + 1 if key in popular else 100 + len(params)),
                       group=group, aliases=aliases, negative=negative, description=text,
                       choices=choices.get(key, []), enabled=False,
                       unavailable=('has been removed' in text or 'DEPRECATED' in text),
                       action=key in ['help', 'version', 'cache-list', 'completion-bash', 'list-devices']))
localize(params, ROOT)
result = dict(schemaVersion=1, source='https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md',
              typeEnum=TYPES, groups=['Common params', 'Sampling params', 'Server-specific params'], parameters=params)
(ROOT / 'resources/parameters.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f'Generated {len(params)} parameters')

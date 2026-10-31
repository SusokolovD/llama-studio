"""Russian UI text; CLI flags and enum values stay machine-readable."""
import re
def localize(params, root):
    translations = {}
    for line in (root / 'resources/parameters.ru.tsv').read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('#'):
            key, label, desc = line.split('|', 2)
            translations[key] = (label, desc)
    for key in ['threads', 'threads-batch', 'cpu-mask', 'cpu-range', 'cpu-strict', 'prio', 'poll', 'cpu-mask-batch', 'cpu-strict-batch', 'prio-batch', 'poll-batch', 'override-tensor', 'cpu-moe', 'n-cpu-moe']:
        label, desc = translations[key]
        translations['spec-draft-' + key] = ('Черновая модель: ' + label.lower(), 'Для черновой модели. ' + desc)
    for kind in ['mod', 'simple', 'map-k', 'map-k4v']:
        for suffix, label, desc in [('n-min', 'Минимум токенов', 'Минимум токенов n-граммной генерации.'), ('n-max', 'Максимум токенов', 'Максимум токенов n-граммной генерации.'), ('n-match', 'Длина поиска', 'Длина совпадающей последовательности.'), ('size-n', 'Длина n-граммы поиска', 'Число токенов в искомой n-грамме.'), ('size-m', 'Длина черновой n-граммы', 'Число токенов в предлагаемом продолжении.'), ('min-hits', 'Минимум совпадений', 'Порог совпадений для применения продолжения.')]:
            translations[f'spec-ngram-{kind}-{suffix}'] = (f'{label} ({kind})', desc + f' Алгоритм {kind}.')
    for p in params:
        key = p['name'][2:]
        if key not in translations and key.endswith(('-default', '-spec')):
            name = key.removesuffix('-default').removesuffix('-spec')
            translations[key] = ('Готовая модель: ' + name, 'Использовать преднастроенную модель ' + name + '. Может загружать веса из интернета.')
        if key not in translations:
            raise ValueError('Missing Russian translation: ' + key)
        p['originalDescription'] = p['description']
        p['label'], p['description'] = translations[key]
        options = {'on': 'Включено', 'off': 'Выключено', 'auto': 'Автоматически', 'none': 'Без / отключено', 'layer': 'По слоям', 'row': 'По строкам', 'tensor': 'По тензорам (экспериментально)', 'linear': 'Линейное', 'greedy': 'Жадный выбор', 'probabilistic': 'Вероятностный выбор', 'distribute': 'Распределить по узлам', 'isolate': 'Один узел'}
        p['choiceLabels'] = {v: options.get(v, v) for v in p['choices']}
        tips = {
            'ctx-size': ('Начните с 8192 токенов на диалог, если модель поддерживает; увеличивайте по длине истории.', 'Для репозитория часто нужен длинный контекст. Начните с 32768 при поддержке модели и проверьте расход памяти.'),
            'parallel': ('Для личного чата обычно достаточно одного слота.', 'Начните с одного слота; параллельные агенты умножают потребность в контексте и памяти.'),
            'temp': ('Следуйте карточке модели; пример 0,7 подходит не всем семействам.', 'Не считайте низкую температуру универсальным правилом: reasoning-модели могут требовать других значений.'),
            'jinja': ('Используйте родной шаблон Instruct-модели.', 'Нужен совместимый шаблон вызовов инструментов; одного включения Jinja недостаточно для обучения модели tools.'),
            'split-mode': ('Для нескольких GPU начните с layer и подбора fit.', 'Сначала сравните задержку на одной GPU и нескольких: обмен между картами может замедлять короткие шаги агента.'),
            'fit-target': ('Оставьте запас под рабочий стол и дополнительные модели.', 'Для проектора, draft-модели и параллельных запросов увеличьте резерв каждой GPU.'),
            'cache-prompt': ('Повторное использование префикса ускоряет длинные диалоги.', 'Полезно при повторяющихся системных инструкциях, файлах и циклах инструментов.'),
            'context-shift': ('При отключении клиент должен сокращать историю по достижении лимита.', 'Автоматическое удаление старых токенов может потерять инструкции и результаты инструментов.'),
            'tools': ('Для обычного чата встроенные серверные инструменты не требуются.', 'Внешняя IDE передаёт tools через API. Серверные инструменты включайте отдельно только при необходимости.'),
        }
        if key in tips:
            p['guidance'] = dict(zip(['chat', 'agent'], tips[key]))
        assert re.search('[А-Яа-я]', p['label']) and re.search('[А-Яа-я]', p['description'])

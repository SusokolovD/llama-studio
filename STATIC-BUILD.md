# Статическая сборка

## MSVC 2026, Windows x64

Требуются Visual Studio 2026 с компонентами C++, Windows SDK, CMake 4.2+ и **статическая Qt для MSVC x64**, собранная с `/MT` (`-static -static-runtime`), с модулями Widgets, Network, Test и платформенными Windows/offscreen-плагинами. Qt MinGW и динамическая Qt не подходят. Генератор `Visual Studio 18 2026` выбирает штатный toolset v145: [справка CMake](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html).

В обычном Windows PowerShell или Developer PowerShell:

Статическая MSVC-сборка настраивается непосредственно в `CMakeLists.txt` опцией `LLAMA_STUDIO_STATIC_MSVC` (по умолчанию ON для MSVC). Она включает `LLAMA_STUDIO_STATIC_QT`, задаёт `/MT` для Release и `/MTd` для Debug. Внутренние библиотеки приложения также собираются статическими. Путь к подходящей Qt передаётся через `CMAKE_PREFIX_PATH`:

```powershell
cmake -S . -B build-msvc2026-static -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/msvc2026-static -DLLAMA_STUDIO_STATIC_MSVC=ON
cmake --build build-msvc2026-static --config Release --parallel 4
ctest --test-dir build-msvc2026-static -C Release --output-on-failure
```

Для динамической MSVC-сборки задайте `-DLLAMA_STUDIO_STATIC_MSVC=OFF -DLLAMA_STUDIO_STATIC_QT=OFF -DLLAMA_STUDIO_MSVC_RUNTIME=MultiThreadedDLL`. Runtime выбирается корректно для обеих конфигураций: `/MD` и `/MDd`. Qt должна использовать такой же runtime.

Скрипт сборки и упаковки использует тот же режим CMake:

```powershell
.\tools\build-static-msvc2026.ps1 -QtRoot 'C:\Qt\msvc2026-static'
```

Скрипт создаёт отдельный от MinGW каталог `build-msvc2026-static`, собирает Release x64 с `MultiThreaded` (/MT), выполняет CTest и проверяет PE imports через dumpbin. Затем копирует результат и конфиги в `dist/LlamaStudio-msvc2026-x64-static`. Параметры `-BuildDirectory`, `-OutputDirectory`, `-Jobs` меняют каталоги и параллелизм. Подмена установленной Qt или установка инструментов не выполняется. Qt с `/MD` будет несовместима с выбранным `/MT`; пересоберите Qt с нужным runtime.

Альтернатива — пресет:

```powershell
$env:QT_MSVC_STATIC_ROOT = 'C:\Qt\msvc2026-static'
cmake --preset msvc2026-static-x64
cmake --build --preset msvc2026-static-x64
ctest --preset msvc2026-static-x64
```

Для Qt 6 удобно использовать встроенный Windows TLS backend Schannel: настройте Qt с `-schannel -no-openssl`. Существующий статический Schannel-плагин включается в приложение. Если используете OpenSSL, он также должен быть статическим и собранным под тот же MSVC/x64/runtime; `LLAMA_STUDIO_STATIC_OPENSSL_DIR` относится только к старому MinGW-комплекту. Подробности: [Qt configure](https://doc.qt.io/qt-6/configure-options.html). Проверка тестов включает доступность TLS.

Скрипт отклоняет импорт Qt, OpenSSL и MSVC/UCRT DLL; системные Windows DLL допустимы. Для другого Qt-комплекта приложите уведомления его версии и встроенных зависимостей. Здесь проверены компиляция исходников MSVC 2026 с доступной динамической Qt и отклонение динамической Qt статическим сценарием. Готовый MSVC static x64 exe не собран: подходящая статическая Qt находится на другом ПК. Публикуемый ZIP ниже по-прежнему содержит проверенную MinGW static x86-сборку.

## Имеющаяся MinGW-сборка

Сборка: Windows x86, Release, Qt 5.15.1 (`E:/Qt_installer/QtStaticSSL`), MinGW GCC 7.3.0. Qt, platform plugins, OpenSSL 1.1.1w, zlib и runtime MinGW статически включены в exe. Системные библиотеки Windows остаются внешними.

Готовый файл: `dist/LlamaStudio-static/LlamaStudio.exe`. Внешний `parameters.json` нужен только для редактирования каталога: при его отсутствии exe использует встроенный каталог. llama.cpp запускается из отдельно выбранной папки.

## Повторная сборка

Из корня проекта, PowerShell:

```powershell
.\tools\build-static.ps1
```

Скрипт использует установленный QtStaticSSL, MSYS2 Perl/Make и MinGW 7.3. При отсутствии статических архивов OpenSSL загружает официальный архив OpenSSL_1_1_1w, проверяет SHA256 и собирает библиотеки в `build-static-deps`. Версия OpenSSL соответствует ABI установленного Qt. Для перехода на новый ABI OpenSSL потребуется совместимая сборка Qt.

Каталоги можно переопределить параметрами `-QtRoot`, `-CompilerBin`, `-MsysRoot`. Разрядность компилятора и комплект Qt должны совпадать. Системная установка Qt не изменяется.

Если предыдущий exe открыт, используйте отдельные каталоги, например: `tools/build-static.ps1 -BuildDirectory build-static-v040 -OutputDirectory dist/LlamaStudio-0.4.0-static`. Это позволяет собрать обновление без закрытия прежней версии. В архив также включается внешний `build-commands.json`; резервная копия рецепта и справочник помощника встроены в exe.

Ручная конфигурация после сборки архивов OpenSSL:

```powershell
$env:PATH = 'E:\Qt\Qt5.14.2\Tools\mingw730_32\bin;' + $env:PATH
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLAMA_STUDIO_STATIC_QT=ON -DCMAKE_PREFIX_PATH=E:/Qt_installer/QtStaticSSL -DCMAKE_CXX_COMPILER=E:/Qt/Qt5.14.2/Tools/mingw730_32/bin/g++.exe -DLLAMA_STUDIO_STATIC_OPENSSL_DIR=C:/llama_studio/build-static-deps/openssl-OpenSSL_1_1_1w
cmake --build build-static -j 4
ctest --test-dir build-static --output-on-failure
```

`LLAMA_STUDIO_STATIC_QT=ON` проверяет тип Qt Core и импортирует встроенные Windows/offscreen plugins. `LLAMA_STUDIO_STATIC_OPENSSL_DIR` заменяет оставшиеся в Qt ссылки на import libraries статическими архивами в памяти CMake. Для другого Qt-комплекта с уже полностью статическими зависимостями этот параметр можно не задавать. Для MSVC runtime настраивается отдельно через `LLAMA_STUDIO_MSVC_RUNTIME` в соответствии со сборкой Qt.

Минимальная версия CMake: 3.21. `windeployqt` и Qt DLL для этой сборки не нужны. Результат проверки PE imports записывается в `dist/LlamaStudio-static/imports.txt`. Скрипт не принимает сборку с импортом Qt, OpenSSL или runtime MinGW.

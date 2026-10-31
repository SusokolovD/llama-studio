# Статическая сборка

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

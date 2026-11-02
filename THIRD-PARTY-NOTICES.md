# Компоненты статической сборки

Ниже описан публикуемый MinGW x86-архив. Сценарий MSVC 2026 использует Qt из переданного `QtRoot`; версия Qt и её встроенные зависимости должны сопровождаться соответствующими уведомлениями. При использовании Qt 6 с Schannel OpenSSL в сборку не включается. `nvidia-smi` и Libre Hardware Monitor — внешние необязательные источники датчиков, в архив не входят.

Приложение статически связывается с Qt 5.15.1 (Core, Gui, Widgets, Network и плагины) из установленного комплекта QtStaticSSL. Qt распространяется по условиям GNU LGPL v3 / GPL; тексты находятся в папке `licenses`. Соответствующие исходники Qt: [Qt 5.15.1](https://download.qt.io/archive/qt/5.15/5.15.1/single/), [QtBase v5.15.1](https://github.com/qt/qtbase/tree/v5.15.1), [QtImageFormats v5.15.1](https://github.com/qt/qtimageformats/tree/v5.15.1). Исходные деревья содержат уведомления о встроенных сторонних библиотеках.

Исходники приложения и CMake-проект поставляются в репозитории вместе с инструкцией STATIC-BUILD.md для повторной сборки с заменёнными библиотеками Qt. Повторная линковка для использования изменённого Qt и отладка таких изменений разрешаются. Для установленного Windows x86-комплекта используется MinGW GCC 7.3.

OpenSSL 1.1.1w включён статически. This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit (https://www.openssl.org/). This product includes cryptographic software written by Eric Young (eay@cryptsoft.com). Полный текст лицензии: OpenSSL-LICENSE.txt в архиве сборки; исходники: [OpenSSL_1_1_1w](https://github.com/openssl/openssl/tree/OpenSSL_1_1_1w).

Runtime MinGW/GCC используется с GCC Runtime Library Exception. Документация llama.cpp и производный каталог: MIT, файл llama.cpp-LICENSE. Бинарные файлы llama.cpp и веса моделей в архив не включены.

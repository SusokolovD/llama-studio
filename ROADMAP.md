# Статическая сборка Qt — выполнено

- Добавлен режим LLAMA_STUDIO_STATIC_QT с проверкой статического Qt Core.
- Для Windows встроены platform plugins, для MinGW — runtime компилятора.
- OpenSSL собран совместимым MinGW и включён статически; установленный Qt не изменён.
- Добавлены скрипт повторной сборки/упаковки и проверка PE imports.
- Инструкции: [STATIC-BUILD.md](STATIC-BUILD.md).

llama-server и llama-fit-params остаются внешними процессами из выбранной пользователем папки.

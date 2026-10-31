#include "config.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

bool Config::load(const QByteArray &data, QString *error) {
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(data, &pe);
    auto fail = [&](const QString &s) { if (error) *error = s; return false; };
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return fail("Некорректный JSON: " + pe.errorString());
    auto root = doc.object();
    QMap<QString,QJsonObject> translations;
    QFile builtIn(":/parameters.json");
    if(builtIn.open(QIODevice::ReadOnly))for(auto p:QJsonDocument::fromJson(builtIn.readAll()).object().value("parameters").toArray()){auto o=p.toObject();translations[o.value("name").toString()]=o;}
    if (root.value("schemaVersion").toInt() != 1 || !root.value("parameters").isArray()) return fail("Ожидается schemaVersion: 1 и массив parameters");
    QVector<Parameter> next;
    QSet<QString> names;
    for (auto v : root.value("parameters").toArray()) {
        if (!v.isObject()) return fail("Параметр должен быть объектом");
        auto o = v.toObject();
        auto translated=translations.value(o.value("name").toString());
        if(!translated.isEmpty() && !QRegularExpression("[А-Яа-яЁё]").match(o.value("description").toString()).hasMatch())
            for(auto key:{"label","description","originalDescription","choiceLabels","guidance"})if(translated.contains(key))o[key]=translated.value(key);
        const QString name = o.value("name").toString();
        const int type = o.value("type").toInt(-1);
        if (!QRegularExpression("^--[a-zA-Z0-9][a-zA-Z0-9.-]*$").match(name).hasMatch() || names.contains(name)) return fail("Неверное или повторяющееся имя: " + name);
        if (type < 0 || type > 6 || !o.contains("default") || !o.value("priority").isDouble() || o.value("group").toString().isEmpty()) return fail("Неполное описание: " + name);
        Parameter p{o, name, o.value("group").toString(), DataType(type), o.contains("value") ? o.value("value") : o.value("default"), o.value("enabled").toBool(), o.value("priority").toInt()};
        if (!p.value.isNull()) {
            bool valid = true;
            switch (p.type) {
            case DataType::Integer: valid = p.value.isDouble() && std::floor(p.value.toDouble()) == p.value.toDouble() && std::abs(p.value.toDouble()) <= 2147483647.; break;
            case DataType::Number: valid = p.value.isDouble(); break;
            case DataType::Boolean: valid = p.value.isBool(); break;
            default: valid = p.value.isString(); break;
            }
            if (!valid) return fail("Значение не соответствует типу: " + name);
        }
        if (p.type == DataType::Choice && !p.value.isNull() && !o.value("choices").toArray().contains(p.value)) return fail("Значение отсутствует в choices: " + name);
        names.insert(name);
        next.append(p);
    }
    if (next.isEmpty()) return fail("Каталог пуст");
    int context = root.value("fitContext").toInt(8192);
    if (context < 1) return fail("Размер контекста должен быть больше нуля");
    parameters = next; metadata = root; directory = root.value("llamaDirectory").toString(); fitContext = context;
    std::stable_sort(parameters.begin(), parameters.end(), [](const Parameter &a, const Parameter &b) { return a.priority < b.priority; });
    return true;
}
QByteArray Config::save() const {
    auto root = metadata;
    QJsonArray array;
    for (const auto &p : parameters) { auto o = p.definition; o["value"] = p.value; o["enabled"] = p.enabled; o["priority"] = p.priority; array.append(o); }
    root["parameters"] = array; root["llamaDirectory"] = directory; root["fitContext"] = fitContext;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
Parameter *Config::find(const QString &name) { for (auto &p : parameters) { if (p.name == name || p.definition.value("aliases").toArray().contains(name)) return &p; } return nullptr; }
const Parameter *Config::find(const QString &name) const { return const_cast<Config *>(this)->find(name); }
bool Config::arguments(QStringList *result, QString *error) const {
    result->clear();
    for (const auto &p : parameters) {
        if (!p.enabled) continue;
        if (p.definition.value("unavailable").toBool() || p.definition.value("action").toBool()) { if (error) *error = "Параметр недоступен для запуска сервера: " + p.name; return false; }
        if (p.value.isNull()) { if (error) *error = "Укажите значение: " + p.name; return false; }
        if (p.type == DataType::Boolean) {
            if (p.value.toBool()) *result << p.name;
            else {
                auto negative = p.definition.value("negative").toString();
                if (!negative.isEmpty()) *result << negative;
                else { if (error) *error = "Для false отключите параметр: " + p.name; return false; }
            }
        } else if (p.type == DataType::Arguments) {
            QStringList args;
            if (!tokenize(p.value.toString(), &args, error) || args.size() != 2) { if (error) *error = p.name + ": нужны два аргумента START END"; return false; }
            *result << p.name << args;
        } else {
            QString val = p.value.isDouble() ? QString::number(p.value.toDouble(), 'g', 15) : p.value.toString();
            if (val.trimmed().isEmpty()) { if (error) *error = "Пустое значение: " + p.name; return false; }
            *result << p.name << val;
        }
    }
    return true;
}
bool Config::tokenize(const QString &s, QStringList *tokens, QString *error) {
    tokens->clear(); QString token; bool quoted = false, started = false;
    // Backslashes are literal: tensor regexes and Windows paths must survive parsing.
    for (auto c : s) {
        if (c == '"') { quoted = !quoted; started = true; }
        else if (c.isSpace() && !quoted) { if (started) { tokens->append(token); token.clear(); started = false; } }
        else { token += c; started = true; }
    }
    if (quoted) { if (error) *error = "Незакрытая двойная кавычка"; return false; }
    if (started) tokens->append(token);
    return true;
}
bool Config::applyFit(const QString &output, QString *error) {
    QString line;
    for (auto candidate : output.split('\n')) if (candidate.trimmed().startsWith("-c ") || candidate.trimmed().startsWith("--ctx-size ")) line = candidate.trimmed();
    if (line.isEmpty()) { if (error) *error = "fit не вернул строку аргументов. См. журнал."; return false; }
    QStringList args;
    if (!tokenize(line, &args, error)) return false;
    Config next = *this;
    QSet<QString> allowed{"--ctx-size", "--gpu-layers", "--tensor-split", "--override-tensor"};
    QSet<QString> seen;
    for (int i = 0; i < args.size(); i += 2) {
        auto p = next.find(args[i]);
        if (!p || !allowed.contains(p->name) || i + 1 >= args.size()) { if (error) *error = "Неподдерживаемый вывод fit: " + line; return false; }
        QString value = args[i + 1];
        if (p->type == DataType::Integer) { bool ok; int n = value.toInt(&ok); if (!ok || n < 1) { if (error) *error = "Неверный контекст fit"; return false; } p->value = n; }
        else p->value = value;
        p->enabled = true; seen.insert(p->name);
    }
    if (!seen.contains("--ctx-size") || !seen.contains("--gpu-layers")) { if (error) *error = "Неполный результат fit"; return false; }
    for (const auto &name : {"--tensor-split", "--override-tensor"}) if (!seen.contains(name)) if (auto p = next.find(name)) p->enabled = false;
    *this = next; return true;
}
QString Config::displayCommand(const QString &program, const QStringList &args, bool redact) {
    auto quote = [](QString s) { s.replace('"', "\\\""); return '"' + s + '"'; };
    QStringList text{quote(QDir::toNativeSeparators(program))}; bool secret = false;
    for (const auto &a : args) { text << quote(secret && redact ? "••••••" : a); secret = a == "--api-key" || a == "--hf-token"; }
    return text.join(' ');
}
Executable findExecutable(const QString &directory, bool fit) {
#ifdef Q_OS_WIN
    QString ext = ".exe";
#else
    QString ext;
#endif
    for (auto sub : {"", "bin", "build/bin", "build/bin/Release", "build/Release", "Release"}) {
        QDir dir(QDir(directory).filePath(sub));
        for (auto name : (fit ? QStringList{"llama-fit-params", "fit-params"} : QStringList{"llama-server", "server"})) {
            QFileInfo info(dir.filePath(name + ext));
            if (info.isFile() && info.isExecutable()) return {info.absoluteFilePath(), {}};
        }
    }
    for (auto sub : {"", "bin", "build/bin", "build/bin/Release"}) {
        QFileInfo info(QDir(directory).filePath(QString(sub) + "/llama" + ext));
        if (info.isFile() && info.isExecutable()) return {info.absoluteFilePath(), {fit ? "fit-params" : "server"}};
    }
    return {};
}

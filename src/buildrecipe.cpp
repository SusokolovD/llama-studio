#include "buildrecipe.h"
#include "config.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
bool BuildRecipe::parse(const QByteArray &json,QVector<BuildCommand> *commands,QString *error) {
    auto fail=[&](const QString &why){if(error)*error=why;return false;};
    if(json.size()>1024*1024)return fail("JSON команд превышает 1 МиБ.");
    QJsonParseError pe;auto doc=QJsonDocument::fromJson(json,&pe);
    if(pe.error!=QJsonParseError::NoError||!doc.isObject())return fail(QString("Некорректный JSON команд (позиция %1): %2").arg(pe.offset).arg(pe.errorString()));
    auto root=doc.object();
    if(root.value("schemaVersion")!=QJsonValue(1)||!root.value("commands").isArray())return fail("Нужны schemaVersion: 1 и массив commands.");
    auto array=root.value("commands").toArray();if(array.isEmpty()||array.size()>128)return fail("В commands должно быть от 1 до 128 команд.");
    QVector<BuildCommand> parsed;
    for(int i=0;i<array.size();++i){
        auto o=array[i].toObject();auto program=o.value("program");
        if(!program.isString()||program.toString().trimmed().isEmpty()||program.toString().contains(QChar(0))||program.toString().contains('\n')||program.toString().contains('\r')||!o.value("arguments").isArray())return fail(QString("Команда %1: укажите строку program и массив строк arguments.").arg(i+1));
        auto args=o.value("arguments").toArray();if(args.size()>512)return fail("Слишком много аргументов команды.");
        BuildCommand command{program.toString().trimmed(),{}};
        for(const auto &value:args){if(!value.isString()||value.toString().contains(QChar(0)))return fail(QString("Команда %1: arguments содержит не строку или нулевой символ.").arg(i+1));command.arguments<<value.toString();}
        parsed.append(command);
    }
    if(commands)*commands=parsed;return true;
}
QString BuildRecipe::defaults() {
    QFile external(QDir(QCoreApplication::applicationDirPath()).filePath("build-commands.json"));
    if(external.exists())return external.open(QIODevice::ReadOnly)?QString::fromUtf8(external.read(1024*1024+1)):QString();
    QFile embedded(":/build-commands.json");if(embedded.open(QIODevice::ReadOnly))return QString::fromUtf8(embedded.readAll());
    return format({{"schemaVersion",1},{"commands",QJsonArray()}});
}
QString BuildRecipe::format(const QJsonObject &json){return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Indented));}
QString BuildRecipe::fromLegacy(const QString &lines){
    QJsonArray commands;
    for(auto line:lines.split('\n')){
        line=line.trimmed();if(line.isEmpty()||line.startsWith('#'))continue;
        QStringList parts;QString error;
        if(!Config::tokenize(line,&parts,&error)||parts.isEmpty())return format({{"schemaVersion",1},{"commands",QJsonArray()},{"legacyCommands",lines},{"migrationError",error}});
        QString program=parts.takeFirst();commands.append(QJsonObject{{"program",program},{"arguments",QJsonArray::fromStringList(parts)}});
    }
    return format({{"schemaVersion",1},{"name","Импорт прежних команд"},{"commands",commands}});
}

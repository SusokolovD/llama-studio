#pragma once
#include <QJsonObject>
#include <QVector>
#include <QStringList>
struct BuildCommand { QString program; QStringList arguments; };
class BuildRecipe {
public:
    static bool parse(const QByteArray &json,QVector<BuildCommand> *commands,QString *error);
    static QString defaults();
    static QString fromLegacy(const QString &lines);
    static QString format(const QJsonObject &json);
};

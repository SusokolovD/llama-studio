#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>

enum class DataType { String = 0, Integer = 1, Number = 2, Boolean = 3, Path = 4, Choice = 5, Arguments = 6 };
struct Parameter {
    QJsonObject definition;
    QString name, group;
    DataType type;
    QJsonValue value;
    bool enabled = false;
    int priority = 0;
};
class Config {
public:
    QJsonObject metadata;
    QVector<Parameter> parameters;
    QString directory;
    int fitContext = 8192;
    bool load(const QByteArray &data, QString *error);
    QByteArray save() const;
    Parameter *find(const QString &name);
    const Parameter *find(const QString &name) const;
    bool arguments(QStringList *result, QString *error) const;
    bool applyFit(const QString &output, QString *error);
    static bool tokenize(const QString &text, QStringList *tokens, QString *error);
    static QString displayCommand(const QString &program, const QStringList &args, bool redact = true);
};
struct Executable { QString path; QStringList prefix; };
Executable findExecutable(const QString &directory, bool fit);

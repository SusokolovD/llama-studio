#pragma once
#include <QPushButton>
#include <QVariant>
// Keep stylesheet padding decorative: the whole widget remains clickable.
class StudioButton : public QPushButton {
public:
    explicit StudioButton(const QString &text,QWidget *parent=nullptr):QPushButton(text,parent){setProperty("fullHit",true);}
protected:
    bool hitButton(const QPoint &position) const override { return rect().contains(position); }
};

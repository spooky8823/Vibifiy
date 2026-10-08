#pragma once
#include <QTabBar>
class TabBar : public QTabBar { Q_OBJECT public: explicit TabBar(QWidget *p=nullptr):QTabBar(p){} };

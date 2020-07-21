#ifndef PACKETVIEW_H
#define PACKETVIEW_H

#include <QObject>
#include <QWidget>
#include <QPropertyAnimation>
#include <QDebug>
#include <QPainter>
#include "datapacket.h"

#define PACKET_SIZE 6
#define PACKET_ANIMATION_INTERVAL 300

class DataPacketView : public QWidget
{
    Q_OBJECT
public:
    DataPacketView(DataPacket* packet, QWidget* parent = nullptr) : QWidget(parent), packet(packet), animation_duration(PACKET_ANIMATION_INTERVAL)
    {
        Q_ASSERT(packet != nullptr);
        connect(packet, SIGNAL(signalPosChanged(QPoint, QPoint)), this, SLOT(updatePosition(QPoint, QPoint)));
        connect(packet, SIGNAL(signalContentChanged()), this, SLOT(updateToolTip()));
        connect(packet, SIGNAL(signalDeleted()), this, SLOT(deleteLater()));

        setFixedSize(PACKET_SIZE, PACKET_SIZE);
    }

    DataPacket* getPacket()
    {
        return packet;
    }

    void setAnimationDuration(int dur)
    {
        this->animation_duration = dur;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QColor c = Qt::red;
        if (packet != nullptr) // 固定
        {
            c = QColor(30, 144, 255);
        }
        painter.fillRect(0,0,width(),height(),c);
    }

private slots:
    void updatePosition(QPoint old_pos, QPoint new_pos)
    {
        if (old_pos == new_pos) // 位置没有变
            return;
        if (this->pos().x() <= 0) // 初始化，不显示动画
        {
            this->move(new_pos - QPoint(PACKET_SIZE / 2, PACKET_SIZE / 2));
            return;
        }
        // 错位显示，以便观察
        new_pos += QPoint(rand() % 8 - 4, rand() % 8 - 4);
        QPropertyAnimation *ani = new QPropertyAnimation(this, "pos");
        ani->setStartValue(this->pos());
        ani->setEndValue(new_pos - QPoint(PACKET_SIZE / 2, PACKET_SIZE / 2));
        ani->setDuration(animation_duration);
    //    ani->setEasingCurve(QEasingCurve::InOutCubic);
        ani->start();
    }

    void updateToolTip()
    {
        if (!packet)
        {
            setToolTip(tr("没有 packet"));
            return ;
        }
        QString s = QString::number(packet->Tag);
        setToolTip(s);
    }

private:
    DataPacket* packet;
    int animation_duration;
};

#endif // PACKETVIEW_H


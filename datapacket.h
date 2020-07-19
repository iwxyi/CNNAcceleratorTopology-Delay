#ifndef DATAPACKET_H
#define DATAPACKET_H

#include <vector>
#include <QApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#else
    #define INT8 char
#endif

typedef int DataType;
typedef int TagType;
typedef INT8 IdType;
typedef INT8 PointVal;
struct PointBean;
typedef std::vector<PointBean> PointVec;

enum PacketType
{
    Unknow,
    Request,
    Data,
    ReqADat
};

/**
 * 每个点的数据
 */
struct PointBean
{
    PointBean(){}
    PointBean(int y, int x, int z, PointVal v)
        : y(y), x(x), z(z), val(v){}
    int y;
    int x;
    int z;
    PointVal val; // 点的值
};

/**
 * 数据包类
 * 分为：
 * - Unknow
 * - Request
 * - Data
 * - ReqADat
 */
struct DataPacket
{
    DataPacket(PacketType type) : _packet_type(type)
    {

    }

    DataPacket(TagType tag, IdType img, IdType cube, IdType sub)
        : _packet_type(Request), Tag(tag), ImgID(img), CubeID(cube), SubID(sub)
    {

    }

    DataPacket(TagType tag, DataType data)
        : _packet_type(Data), Tag(tag), data(data)
    {

    }

    DataPacket(DataType data)
        :_packet_type(Unknow), data(data)
    {

    }

    DataPacket(TagType tag, IdType img, IdType cube, IdType sub, DataType data)
        : _packet_type(ReqADat), Tag(tag), ImgID(img), CubeID(cube), SubID(sub), data(data)
    {

    }


    // 运行所需
    const PacketType _packet_type;
    int delay_step;
    int delay_max;

    // 各种标识符
    TagType Tag = 0;
    IdType ImgID = 0;  // 被滑动图像
    IdType CubeID = 0; // 一个小晶体
    IdType SubID = 0;  // 小晶体内部每一小块
    DataType data = 0; // 具体存储的data
    int kernel_index = -1; // 准备发送到的kernel索引
    PointVec points; // 包含的点的数据

    // 不用管的属性
    int CmdType = 0;
    int Cmd = 0;
    int VC = 0;
    int Pri = 0;
    int Par = 0;

    bool isReq()
    {
        return _packet_type == Request;
    }

    void resetDelay(int max)
    {
        delay_step = 0;
        delay_max = max;
    }

    void delayToNext()
    {
        delay_step++;
    }

    bool isDelayFinished()
    {
        return true;
        return delay_step >= delay_max;
    }
};

#endif // DATAPACKET_H

#ifndef DATAPACKET_H
#define DATAPACKET_H

typedef int DataType;
typedef int TagType;
typedef int IdType;

enum PacketType
{
    Request,
    Data
};

/**
 * 数据包类
 * 分为：
 * - Request
 * - Data
 */
struct DataPacket
{
    DataPacket(PacketType type) : _packet_type(type)
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

    bool isDelayFinished()
    {
        return delay_step >= delay_max;
    }
};

#endif // DATAPACKET_H

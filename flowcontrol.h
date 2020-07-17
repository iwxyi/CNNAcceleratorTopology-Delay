/**
  * 各种流程控制的类
  */

#ifndef FLOWCONTR_H
#define FLOWCONTR_H

#include <queue>
#include <windows.h>
#include "datapacket.h"
#include "layerthread.h"

typedef int ClockType;
typedef std::queue<DataPacket> FIFO;


// 从一开始到现在经过的clock
ClockType global_clock = 0;
// 某一个clock是否有数据流传输，若有则继续重新判断整个传输流程
// 使用此flag解决单线程机制无法模拟多线程的多数据同步传输问题
bool has_transfered = false;

// ==================== 各种队列 ===================
FIFO ReqFIFO;
FIFO DatLatch;


void beforeClock();
void dataTransfer();
void clockGoesBy();
void afterClock();


/**
 * 初始化一切所需要的内容
 */
void initFlowControl()
{

}

void runFlowControl()
{
    while (true)
    {
        Sleep(1); // 逐步显示流控

        beforeClock();

        dataTransfer();
    }
}

/**
 * 在一个clock执行之前初始化
 */
void beforeClock()
{

}

/**
 * 数据传输
 * 如果has_transfered，则重新此方法
 */
void dataTransfer()
{
    // 使用此flag避免了非多线程的先后顺序问题
    has_transfered = false;


    // 如果这个函数中有数据传输
    // 那么继续重新传输
    // 解决单线程机制无法模拟多线程的多数据同步传输问题
    if (has_transfered)
    {
        dataTransfer();
    }
}

/**
 * 流逝了一个clock
 */
void clockGoesBy()
{

}

/**
 * 完全结束后一个clock的操作
 */
void afterClock()
{

}

/**
 * 整个流控结束
 * 在这里输出各种结果
 */
void finishFlowControl()
{

}


#endif // FLOWCONTR_H

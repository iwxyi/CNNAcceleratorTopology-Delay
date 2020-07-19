/**
  * 各种流程控制的类
  */

/**
  * TODO:
  * - 小方块中很多点重复为3次？
  * - 小方面每个点都要分成8(kernel)份，创建时分还是pick时分？
  */

#ifndef FLOWCONTR_H
#define FLOWCONTR_H

#include <list>
#include "datapacket.h"
#include "layerthread.h"
#include "delaydefine.h"

#define PacketPointCount 2      // 每个req带有几个点的数量
#define ReqQueue_MaxSize 24     // ReqQueue数据量上限
#define DatLatch_MaxSize 24     // 和上面要一样大小
#define Picker_FullBandwidth 5  // 1个clock进行pick的数量，即bandwidth
#define ConvPoints_MaxSize 1600 // 要卷积的点的集合数量上限，要计算的话，至少要3*3*3*32
#define ConvQueue_MaxSize 2     // 卷积核存储的最大的大小

#define DEB if (0) printf
typedef int ClockType;
typedef std::vector<DataPacket*> FIFO;


// 从一开始到现在经过的clock
ClockType global_clock = 0;
// 某一个clock是否有数据流传输，若有则继续重新判断整个传输流程
// 使用此flag解决单线程机制无法模拟多线程的多数据同步传输问题
bool has_transfered = false;
int picker_bandwdith = Picker_FullBandwidth; // pick的最大bandwidth
int picker_tagret = 0; // picker下一次pick的目标，0~layer_kernel-1。如果不行，则跳过
int current_map_side = 0; // 当前图像的大小
long long total_points = 0; // 总共参与卷积的点
long long conved_points = 0; // 已经卷积并且结束的点。如果两者相等，则表示当前层已经结束了

// ==================== 各种队列 ===================
FIFO StartQueue; // 特征图的每一点生成后的总队列
FIFO ReqQueue;   // 特征图的每一点req的队列；tag和data相同
//FIFO DatLatch;   // 特征图的每一点data的队列；tag和req相同
FIFO PickQueue; // pick后进行delay的队列
//PointVec ConvPoints[KERNEL_MAX_COUNT]; // 卷积数据等待队列
FIFO ConvQueue[KERNEL_MAX_COUNT]; // 每个卷积结果队列
FIFO Conv2SndFIFO; // Conv => SndFIFO
FIFO SndQueue;   // 合并后的数据队列，发送到下一层
FIFO Snd2SwitchFIFO;
FIFO SwitchQueue;
FIFO Switch2NextLayer;

// ==================== 图像操作 ===================
FeatureMap* feature_map = NULL; // 当前特征图
std::vector<Kernel*> kernels;   // 卷积核数组


// ==================== 特征图操作 ===================
/**
 * 将特征图分割成非常非常小的粒子数据包
 * 其中每个小点数据包都重复kernel数量次
 * 按顺序（所以满了数量就能发送了）放入到队列里面，准备发送
 * @param map    要滑动的特征图
 * @param kernel 这里不是要相加，只是分割
 * @param queue  存储结果数据包的队列
 */
void splitMap2Queue(FeatureMap* map, Kernel* kernel, FIFO& queue)
{
    // 将特征图分割成小点队列
    INT8*** m = map->map;
    int side = map->side/* - kernel->side + 1*/; // 不是在这里卷积的，使用原始大小
    PointVec points;
    for (int y = 0; y < side; y++)
    {
        for (int x = 0; x < side; x++)
        {
            for (int z = 0; z < kernel->channel; z++)
            {
                points.push_back(PointBean(y, x, z, m[y][x][z]));
            }
        }
    }

    current_map_side = map->side;
    conved_points = 0;
    total_points = points.size() * layer_kernel; // 全部要处理的点的数量

    // 将每个点打包成能够发送的数据包
    // 存储在预备发送的队列中
    unsigned int size = points.size();
    for (unsigned int i = 0; i < size; )
    {
        // 这个数据包的首个点的坐标，用来编号
        int y = points[i].y;
        int x = points[i].x;
        int z = points[i].z;

        // 每个数据包包含几个点
        PointVec vec;
        for (int j = 0; j < PacketPointCount && i < size; j++)
        {
            vec.push_back(points[i++]);
        }

        // 创建当前层kernel数量个除了Kernel索引外都一模一样的数据包
        // 因为同一个点会被许多个不同的卷积核读取
        for (int knl = 0; knl < layer_kernel; knl++)
        {
            DataPacket* packet = new DataPacket(m[y][x][z]);
            packet->ImgID = (INT8)z;
            packet->CubeID = (INT8)y;
            packet->SubID = (INT8)x;
            // Tag暂时为三个ID拼接，确保每个packet的Tag都不相同
            packet->Tag = (packet->ImgID << 16)
                    + (packet->CubeID << 8)
                    + (packet->SubID);
            packet->kernel_index = knl; // 指向将要被发送的kernel索引，从0开始
            packet->points = vec;
            packet->resetDelay(Dly_inReqFIFO);
            queue.push_back(packet);
        }
    }
}

/**
 * 开启新的一层
 */
void startNewLayer()
{
    // 这里是一层的开始
    current_layer++;
    layer_channel = getKernelCount(current_layer-1);
    layer_kernel = getKernelCount(current_layer);
    picker_bandwdith = Picker_FullBandwidth;
    picker_tagret = 0;
    total_points = 0;
    conved_points = 0;
    printf("\n========== enter layer %d ==========\n", current_layer);

    // 数据分割，一下子就分好了，没有延迟
    Kernel* kernel = new Kernel(KERNEL_SIDE, getKernelCount(current_layer-1));
    splitMap2Queue(feature_map, kernel, StartQueue);
    delete kernel;
    delete feature_map;
    feature_map = NULL;
}

/**
 * 在一个queue里面查找是否有这个tag
 * ReqQueue里查找Data
 * DatLatch里查找Request
 * （现在已经没有DatLatch了，用不到）
 */
bool findTagInQueue(FIFO queue, TagType tag)
{
    for (unsigned int i = 0; i < queue.size(); i++)
    {
        if (queue.at(i)->Tag == tag)
            return true;
    }
    return false;
}

/**
 * 切换至下一个pick的目标
 */
void pickNextTarget()
{
    picker_tagret++;
    if (picker_tagret >= layer_kernel)
        picker_tagret = 0;
}

/**
 * 判断每个卷积核的数据量是否达到能够计算的程度
 * 如果可以计算，即 size() >= 偏移+KERNEL_SIDE * KERNEL_SIDE * layer_channel
 * 则将这些数量的数据进行累加，并将结果生成新的DataPacket传递至下一个队列
 */
void convCalc()
{
    // 不用计算，只需要延迟，不需要结果
}

/**
 * 根据传递过来的结果，生成下一层结果的特征图
 * 大小会缩小，new channel = this->kernel
 * 由于中间没有计算的数据，干脆就全部使用0了
 */
void generalNextLayerMap()
{
    int channel = layer_kernel;
    int side = current_map_side - KERNEL_SIDE + 1;
    feature_map = new FeatureMap(side, channel);
}

/**
 * 输出运行时的状况
 */
void printState()
{
#ifdef Q_OS_WIN
    system("cls");
#endif
    printf("current clock: %d\n", global_clock);
    printf("current layer: %d    %lld / %lld (%.4f%%)\n", current_layer, conved_points, total_points, conved_points*100.0/total_points);
    printf("    feature map: %d * %d * %d\n", current_map_side, current_map_side, layer_channel);
    printf("    conv kernel: %d * %d * %d, count = %d\n", KERNEL_SIDE, KERNEL_SIDE, layer_channel, layer_kernel);

    return ;
    int sum = conved_points;
    int start_points = 0;
    for (int i = 0; i < StartQueue.size(); i++)
        start_points += StartQueue.at(i)->points.size();
    printf("  Start: %d(%d)\n", StartQueue.size(), start_points);
    int req_points = 0;
    for (int i = 0; i < ReqQueue.size(); i++)
        req_points += ReqQueue.at(i)->points.size();
    printf("  ReqQueue: %d(%d)\n", ReqQueue.size(), req_points);
    int pick_points = 0;
    for (int i = 0; i < PickQueue.size(); i++)
        pick_points += PickQueue.at(i)->points.size();
    printf("  PickQueue: %d(%d)\n", PickQueue.size(), pick_points);
    printf("  ConvQueue: ");
    int conv_points = 0;
    for (int i = 0; i < layer_kernel; i++)
    {
        FIFO& queue = ConvQueue[i];
        int kernel_points = 0;
        for (int i = 0; i < queue.size(); i++)
        {
//            printf("*"); // 用来刷新缓冲区
            kernel_points += queue.at(i)->points.size();
        }
        printf("%d(%d)%c", queue.size(), kernel_points, i < layer_kernel - 1 ? ' ' : '\n');
        conv_points += kernel_points;
    }
    int conv2snd_points = 0;
    for (int i = 0; i < Conv2SndFIFO.size(); i++)
        conv2snd_points += Conv2SndFIFO.at(i)->points.size();
    printf("  Conv2SndFIFO: %d(%d)\n", Conv2SndFIFO.size(), conv2snd_points);
    int snd_points = 0;
    for (int i = 0; i < SndQueue.size(); i++)
        snd_points += SndQueue.at(i)->points.size();
    printf("  SndQueue: %d(%d)\n", SndQueue.size(), snd_points);
    printf("  Snd2SwitchFIFO: %d\n", Snd2SwitchFIFO.size());
    printf("  SwitchQueue: %d\n", SwitchQueue.size());
    printf("  Switch2NextLayer: %d\n", Switch2NextLayer.size());

    sum += start_points
            + req_points
            + pick_points
            + conv_points
            + conv2snd_points
            + snd_points;
    printf("sum: %d\n", sum);
}

// ==================== 流程控制 ===================
// 函数前置声明
void inClock();
void dataTransfer();
void clockGoesBy();

/**
 * 初始化一切所需要的内容
 */
void initFlowControl()
{
    initLayerResource();
    feature_map = new FeatureMap(0, MAP_SIDE_MAX, MAP_CHANNEL_DEFULT);
}

/**
 * 每个clock运行一次
 */
void runFlowControl()
{
    while (true)
    {
//        Sleep(1); // 逐步显示流控

        inClock();

        // 如果超过了最后一层，则退出
        if (current_layer > MAX_LAYER)
            break;
    }
}

/**
 * 一个clock中的操作
 */
void inClock()
{
//    getchar(); // 使用阻塞式输入来让一个clock一个clock的走过去

    global_clock++;
    picker_bandwdith = Picker_FullBandwidth; // bandwidth满载

    // 开启新的一层，分割特征图
    if (feature_map)
    {
        startNewLayer();
        printState();
//        printf("\n========== press enter to start new layer ======\n");
//        getchar();
    }

    dataTransfer();

    clockGoesBy();

    printState();
}

/**
 * 数据传输
 * 如果has_transfered，则重新此方法
 */
void dataTransfer()
{
    // 使用此flag避免了非多线程的先后顺序问题
    has_transfered = false;


    // 特征图的点到ReqFIFO和DatLatch
    // 由于ReqFIFO在pick后，有指针指向data，data立马跟着出来
    // 两个是连续的，应该可以不用分开，只使用一个队列
    while (ReqQueue.size() < ReqQueue_MaxSize && !StartQueue.empty())
    {
        DataPacket* packet = StartQueue.front();
        StartQueue.erase(StartQueue.begin()); // 删除首元素

        packet->resetDelay(Dly_inReqFIFO);
        DataPacket* req = packet;
        ReqQueue.push_back(req);

        /*DataPacket* data = new DataPacket(packet->data);
        data->Tag = req->Tag;
        DatLatch.push_back(data);*/
        DEB("Start %d => ReqFIFO + DatLatch\n", req->Tag);
    }

    // ReqFIFI => ConvQueues
    int start_picker_target = picker_tagret; // 记录当前的picker的目标，避免全部一遍后的死循环
    while (picker_bandwdith > 0 && ReqQueue.size())
    {
        // 如果卷积核数据数量已经达到了上限，则跳过这个kernel
        if (ConvQueue[picker_tagret].size() > ConvQueue_MaxSize)
        {
            pickNextTarget(); // pick到下一根
            continue;
        }

        // 判断有没有能pick到这个卷积核的数据包
        bool picked = false;
        for (int i = 0; i < ReqQueue.size(); i++)
        {
            DataPacket* packet = ReqQueue.at(i);
            // packet 延迟没有结束；或者根本就不是这个kernel的
            // （这样的picker的性能似乎不高）
            if (!packet->isDelayFinished() || packet->kernel_index != picker_tagret)
                continue;

            // packet能发送，kernel能接收
            // 开始发送
            ReqQueue.erase(ReqQueue.begin() + i--);
            PickQueue.push_back(packet); // 进入Pick延迟
            packet->resetDelay(Dly_onPick);
            DEB("ReqFIFO pick => Conv %d, bandwidth:%d\n", picker_tagret, picker_bandwdith-1);

            // 发送后各种数值的变化
            picker_bandwdith--;
            pickNextTarget();
            has_transfered = true;
            picked = true;
            break;
        }
        if (!picked) // 没有能pick到这个卷积核的数据包
            pickNextTarget();
        if (picker_tagret == start_picker_target) // 全部轮询了一遍都不行，取消遍历
            break;
    }

    // Pick延迟结束，进入Conv
    for (int i = 0; i < PickQueue.size(); i++)
    {
        DataPacket* packet = PickQueue.at(i);
        if (!packet->isDelayFinished())
            continue;

        // pick延迟结束，进入conv
        PickQueue.erase(PickQueue.begin() + i--);
        ConvQueue[packet->kernel_index].push_back(packet);
        packet->resetDelay(Dly_inConv);
        has_transfered = true;
        DEB("ReqFIFO pick=> Conv %d\n", picker_tagret);
    }


    /*// 需求变更：ConvPoints用不到了，因为 Conv 这些不再是存储点，只是点的delay
    int start_picker_target = picker_tagret; // 记录当前的picker的目标，避免全部一遍后的死循环
    while (picker_bandwdith > 0)
    {
        // 如果数据点的数量已经达到了上限，则跳过这个kernel
        if (ConvPoints[picker_tagret].size() >= ConvPoints_MaxSize)
        {
            pickNextTarget(); // pick到下一根
            if (picker_tagret == start_picker_target) // 全部轮询了一遍都不行，取消遍历
                break;
            continue;
        }

        for (int i = 0; i < ReqQueue.size(); i++)
        {
            DataPacket* packet = ReqQueue.at(i);
            // packet 延迟没有结束；或者根本就不是这个kernel的
            // （这样的picker的性能似乎不高）
            if (!packet->isDelayFinished() || packet->kernel_index != picker_tagret)
                continue;

            // packet能发送，kernel能接收
            // 开始发送
            PointVec vec = packet->points;
            for (unsigned int p = 0; p < vec.size(); p++)
            {
                ConvPoints[picker_tagret].push_back(vec.at(p));
            }
            DEB("ReqFIFO => Conv %d\n", picker_tagret);

            // 删除packet本身（已经不需要这个packet了）
            ReqQueue.erase(ReqQueue.begin() + i--);

            // 发送后各种数值的变化
            picker_bandwdith--;
            pickNextTarget();
        }
    }*/


    // DatLatch Pick给 ConvQueue。要先确定相同Tag的Req先发送才能发
    // Data跟着Req发送，不用再单独判断了
    /*for (unsigned int i = 0; i < DatLatch.size(); i++)
    { }*/


    // 每个 ConvQueue 将结果发送给 SndFIFO
    for (int i = 0; i < layer_kernel; i++)
    {
        FIFO& queue = ConvQueue[i];
        for (int j = 0; j < queue.size(); j++)
        {
            DataPacket* packet = queue.at(j);
            if (!packet->isDelayFinished())
                continue;

            // Conv => SndFIFO
            queue.erase(queue.begin() + j--);
            Conv2SndFIFO.push_back(packet);
            packet->resetDelay(Dly_Conv2SndFIFO);
            has_transfered = true;
            DEB("Conv %d => SndFIFO\n", i);
        }
    }

    // Conv => SndFIFO 的延迟
    for (int i = 0; i < Conv2SndFIFO.size(); i++)
    {
        DataPacket* packet = Conv2SndFIFO.at(i);
        if (!packet->isDelayFinished())
            continue;

        Conv2SndFIFO.erase(Conv2SndFIFO.begin() + i--);
        SndQueue.push_back(packet);
        packet->resetDelay(Dly_inSndFIFO);
        DEB("Conv => SndFIFO delay\n");
        has_transfered = true;
    }

    // SndFIFO 传送到 switch
    for (int i = 0; i < SndQueue.size(); i++)
    {
        DataPacket* packet = SndQueue.at(i);
        if (!packet->isDelayFinished())
            break;

        conved_points += packet->points.size(); // 完成的点的数量
        SndQueue.erase(SndQueue.begin() + i--);
        delete packet;

        /*SndQueue.erase(SndQueue.begin() + i--);
        Snd2SwitchFIFO.push_back(packet);
        packet->resetDelay(Dly_Snd2Switch);
        DEB("SndFIFO => Switch\n");*/
        has_transfered = true;
    }

    // 全部卷积完毕
    if (total_points > 0 && conved_points >= total_points)
    {
        conved_points = total_points = 0;
        DEB("convolution all completed\n");
        DataPacket* packet = new DataPacket(Request);
        Snd2SwitchFIFO.push_back(packet);
        packet->resetDelay(Dly_Snd2Switch);
    }

    // SndFIFO => Switch 中途的总延迟
    for (int i = 0; i < Snd2SwitchFIFO.size(); i++)
    {
        DataPacket* packet = Snd2SwitchFIFO.at(i);
        if (!packet->isDelayFinished())
            continue;

        Snd2SwitchFIFO.erase(Snd2SwitchFIFO.begin() + i--);
        SwitchQueue.push_back(packet);
        packet->resetDelay(Dly_inSwitch);
        DEB("SndFIFO => Switch delay\n");
        has_transfered = true;
    }

    // Switch发送至下一层
    for (int i = 0; i < SwitchQueue.size(); i++)
    {
        DataPacket* packet = SwitchQueue.at(i);
        if (!packet->isDelayFinished())
            continue;

        SwitchQueue.erase(SwitchQueue.begin() + i--);
        Switch2NextLayer.push_back(packet);
        packet->resetDelay(Dly_Switch2NextPE);
        DEB("Switch => NextLayer\n");
        has_transfered = true;
    }

    if (Switch2NextLayer.size())
    {
        DataPacket* packet = Switch2NextLayer.front();
        if (packet->isDelayFinished())
        {
            Switch2NextLayer.clear();
            delete packet;
            generalNextLayerMap();
        }
    }
    /*for (int i = 0; i < Switch2NextLayer.size(); i++)
    {
        DataPacket* packet = Switch2NextLayer.at(i);
        if (!packet->isDelayFinished())
            continue;

        Switch2NextLayer.erase(Switch2NextLayer.begin() + i--);
        conved_points += packet->points.size(); // 完成的点的数量
        delete packet; // 避免内存泄漏
        has_transfered = true;
    }*/


    // 如果这个函数中有数据传输
    // 那么继续重新传输
    // 解决单线程机制无法模拟多线程的多数据同步传输问题
    if (has_transfered)
    {
        // 这里全都是按照顺序来的，不需要再判断顺序了
        // dataTransfer(); // 递归调用自己，直至 !has_transfered
    }
}

/**
 * 流逝了一个clock
 * 即所有队列中的packet的delay都next
 */
void clockGoesBy()
{
    for (unsigned int i = 0; i < ReqQueue.size(); i++)
    {
        ReqQueue[i]->delayToNext();
    }

    for (unsigned int i = 0; i < PickQueue.size(); i++)
    {
        PickQueue[i]->delayToNext();
    }

    for (int i = 0; i < layer_kernel; i++)
    {
        for (unsigned int j = 0; j < ConvQueue[i].size(); j++)
        {
            ConvQueue[i].at(j)->delayToNext();
        }
    }

    for (unsigned int i = 0; i < Conv2SndFIFO.size(); i++)
    {
        Conv2SndFIFO[i]->delayToNext();
    }

    for (unsigned int i = 0; i < SndQueue.size(); i++)
    {
        SndQueue[i]->delayToNext();
    }

    for (unsigned int i = 0; i < Snd2SwitchFIFO.size(); i++)
    {
        Snd2SwitchFIFO[i]->delayToNext();
    }

    for (unsigned int i = 0; i < SwitchQueue.size(); i++)
    {
        SwitchQueue[i]->delayToNext();
    }

    for (unsigned int i = 0; i < Switch2NextLayer.size(); i++)
    {
        Switch2NextLayer[i]->delayToNext();
    }
}

/**
 * 整个流控结束
 * 在这里输出各种结果
 */
void finishFlowControl()
{

}


#endif // FLOWCONTR_H

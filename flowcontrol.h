/**
  * 各种流程控制的类
  */

/**
  * TODO:
  * - tag直接序号吧
  * - switch的bandwidth
  */

#ifndef FLOWCONTR_H
#define FLOWCONTR_H

#include <list>
#include <time.h>
#include "datapacket.h"
#include "layerthread.h"
#include "delaydefine.h"

#define DEB if (0) printf // 输出调试过程中的数据流动
#define PRINT_STATE true  // 输出当前layer、clock等信息
#define PRINT_MODULE true // 输出每一个模块中数据包的数量
#define DEB_MODE false    // 输出点的更多信息， 速度也会慢很多
#define STEP_MODE false   // 一步步停下，等待回车

typedef int ClockType;
typedef std::vector<DataPacket*> FIFO;


// 从一开始到现在经过的clock
ClockType global_clock = 0;
int layer_start_clock = 0;
clock_t start_time;
// 某一个clock是否有数据流传输，若有则继续重新判断整个传输流程
// 使用此flag解决单线程机制无法模拟多线程的多数据同步传输问题
bool has_transfered = false;
int picker_bandwdith = Picker_FullBandwidth; // pick的最大bandwidth
int picker_tagret = 0; // picker下一次pick的目标，0~layer_kernel-1。如果不行，则跳过
int switch_bandwidth = Switch_FullBandwidth; // switch发送的最大速度

int current_map_side = 0; // 当前图像的大小
long long total_points = 0; // 总共参与卷积的点
long long conved_points = 0; // 已经卷积并且结束的点。如果两者相等，则表示当前层已经结束了
long long next_layer_points = 0; // 下一层应该有的点的数量

// ==================== 各种队列 ===================
FIFO StartQueue; // 特征图的每一点生成后并传输到ReqQueue的队列
FIFO ReqQueue;   // 特征图的每一点req的队列；tag和data相同
//FIFO DatLatch;   // 特征图的每一点data的队列；tag和req相同
FIFO PickQueue; // pick后进行delay的队列
//PointVec ConvPoints[KERNEL_MAX_COUNT]; // 卷积数据等待队列
FIFO ConvQueue[KERNEL_MAX_COUNT]; // 每个卷积结果队列
int ConvWaiting[KERNEL_MAX_COUNT]; // 等待pick到卷积队列的数量
FIFO Conv2SndFIFO; // Conv => SndFIFO
FIFO SndQueue;   // 合并后的数据队列，发送到下一层
FIFO SndPipe;    // 下一层的发送管道
FIFO SwitchQueue;
FIFO Switch2NextLayer;
FIFO NextLayerFIFO;

// ==================== 图像操作 ===================
FeatureMap* feature_map = NULL; // 当前特征图
std::vector<Kernel*> kernels;   // 卷积核数组
#ifdef Q_OS_WIN
HANDLE HOutput = GetStdHandle(STD_OUTPUT_HANDLE);
#endif


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
    total_points = side * side * map->channel;
    // 预备要生成的点的数量
    next_layer_points = (map->side - kernel->side + 1) * (map->side - kernel->side + 1) * 1;

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

        // 将点打包成数据包
        // ImgID: 全部运行时唯一
        // CubeID: 小方块的ID
        // SubID: 小方块第几面
        // 其实感觉还少了更深一层的ID，但是这个ID也无所谓的，只要有了就行
        DataPacket* packet = new DataPacket(m[y][x][z]);
        packet->ImgID = (INT8)z;
        packet->CubeID = (INT8)y;
        packet->SubID = (INT8)x;
        // Tag暂时为三个ID拼接，确保每个packet的Tag都不相同
        packet->Tag = (packet->ImgID << 16)
                + (packet->CubeID << 8)
                + (packet->SubID);
        packet->points = vec;
        packet->resetDelay(Dly_Map2RegFIFO);
        queue.push_back(packet);
    }
}

/**
 * 开启新的一层
 */
void startNewLayer()
{
    printf("\nprevious layer clock used:%d    \n", global_clock - layer_start_clock);
    // 这里是一层的开始
    current_layer++;
    layer_start_clock = global_clock;
    layer_channel = getKernelCount(current_layer-1);
    layer_kernel = getKernelCount(current_layer);
    picker_bandwdith = Picker_FullBandwidth;
    picker_tagret = 0;
    total_points = 0;
    conved_points = 0;
    printf("========== enter layer %d ==========\n", current_layer);

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
 * Picker 切换至下一个pick的目标
 * 目前采用轮询算法，但是其实不管什么调度算法都可以
 * 只要卷积核有空，堆上去就是了，总量不变，不影响结果
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

    /**
      * 假装从队列里面获取数据
      * 根据里面的xyz值，一一放到新特征图的位数据里
      * 这里不需要真正的数据，直接创建新的特征图
      */

    // 这里直接生成新的特征图了
    while (!NextLayerFIFO.empty())
    {
        delete NextLayerFIFO.back();
        NextLayerFIFO.pop_back();
    }
}

/**
 * 输出运行时的状况
 * PRINT_STATE: 总开关
 * PRINT_POINT: 输出每个位置有多少数据包
 * DEB_MODE: 调试模式，输出每个位置有多少点
 */
void printState()
{
    if (!PRINT_STATE)
        return ;
#ifdef Q_OS_WIN
    // 实测跑第一层（非当前程序），不加清屏是90s，加了约8000s
    if (STEP_MODE)
        ;
    else
        // system("cls"); // 非常非常损耗性能，会降低近百倍速度
        SetConsoleCursorPosition(HOutput, COORD{0,0}); // 这句话改变光标输出位置，不是清屏。每行后面建议多点空格
#else
    // system("clear"); // 非常非常损耗性能，会降低近百倍速度
    printf("\033c"); // 这句清屏命令不吃性能
#endif
    printf("current clock: %d                                  \n", global_clock);
    printf("current layer: %d    %lld / %lld (%.4f%%)           \n", current_layer, conved_points, total_points,
           total_points == 0 ? 100 : conved_points*100.0/total_points);
    printf("    feature map: %d * %d * %d\n", current_map_side, current_map_side, layer_channel);
    printf("    conv kernel: %d * %d * %d, count = %d\n", KERNEL_SIDE, KERNEL_SIDE, layer_channel, layer_kernel);

    // 输出每一个模块的位置
    if (PRINT_MODULE)
    {
        printf("  Start       : %d       \n", StartQueue.size());
        printf("  ReqFIFI     : %d       \n", ReqQueue.size());
        printf("  PickFIFO    : %d       \n", PickQueue.size());
        /*printf("      picking : ");
        for (int i = 0; i < layer_kernel; i++)
        {
            printf("%d%c", ConvWaiting[i], i < layer_kernel - 1 ? ' ' : '\n');
        }*/
        printf("  ConvFIFO    : ");
        for (int i = 0; i < layer_kernel; i++)
        {
            FIFO& queue = ConvQueue[i];
            printf("%d%c", queue.size(), i < layer_kernel - 1 ? ' ' : '\n');
        }
        printf("  Conv2SndFIFO: %d      \n", Conv2SndFIFO.size());
        printf("  SndFIFO     : %d      \n", SndQueue.size());
        printf("  SndPipe     : %d      \n", SndPipe.size());
        printf("  SwitchFIFO  : %d      \n", SwitchQueue.size());
        printf("  toNextLayer : %d      \n", Switch2NextLayer.size());
        printf("  NextLayer   : %d      \n", NextLayerFIFO.size());
    }
    // 在调试的时候，输出更多的信息
    // 不过这样显示会大幅度拖慢速度
    else if (DEB_MODE)
    {
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
        printf("  SndQueue: %d(%d)\n", SndQueue.size(), snd_points);
        printf("  SwitchQueue: %d\n", SwitchQueue.size());
        printf("  Switch2NextLayer: %d\n", Switch2NextLayer.size());
        printf("  Switch2NextLayer: %d\n", NextLayerFIFO.size());

        sum += start_points
                + req_points
                + pick_points
                + conv_points
                + conv2snd_points
                + snd_points;
        printf("sum: %d\n", sum);
    }
}

// ==================== 流程控制 ===================
// 函数前置声明
void inClock();      // 上升沿
void dataTransfer(); // 数据传输
void clockGoesBy();  // 下降沿

/**
 * 初始化一切所需要的内容
 */
void initFlowControl()
{
    initLayerResource();
    feature_map = new FeatureMap(0, MAP_SIDE_MAX, MAP_CHANNEL_DEFULT);
    start_time = clock();
}

/**
 * 循环，使每个clock运行一次
 */
void runFlowControl()
{
    while (true)
    {
        inClock();
        if (STEP_MODE)
            getchar();

        // 如果超过了最后一层，则退出
        if (current_layer >= MAX_LAYER && feature_map)
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
    switch_bandwidth = Switch_FullBandwidth;

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
 * 如果has_transfered，则递归调用此方法
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
    bool round_picked = false; // 这一整圈有无pick。一圈无pick即使有bandwidth也退出
    while (picker_bandwdith > 0 && ReqQueue.size())
    {
        // 如果卷积核数据数量已经达到了上限，则跳过这个kernel
        if (/*ConvQueue[picker_tagret].size()+*/ConvWaiting[picker_tagret] >= ConvQueue_MaxSize)
        {
            pickNextTarget(); // pick到下一根
            if (picker_tagret == start_picker_target)
            {
                if (!round_picked) // 轮询了一整圈都没有pick，退出该循环
                    break;
                round_picked = false;
            }
            continue; // 寻找下一个能pick的卷积核
        }

        // 判断有没有能pick到这个卷积核的数据包
        bool picked = false;
        for (int i = 0; i < ReqQueue.size(); i++)
        {
            DataPacket* packet = ReqQueue.at(i);
            // packet 延迟没有结束
            if (!packet->isDelayFinished())
                break;

            // packet能发送，kernel能接收
            // 开始发送
            ReqQueue.erase(ReqQueue.begin() + i--);
            PickQueue.push_back(packet); // 进入Pick延迟
            packet->kernel_index = picker_tagret;
            packet->resetDelay(Dly_onPick);
            ConvWaiting[picker_tagret]++; // 等待进入Conv的数量
            DEB("ReqFIFO pick => Conv %d, bandwidth:%d\n", picker_tagret, picker_bandwdith-1);

            // 发送后各种数值的变化
            picker_bandwdith--;
            pickNextTarget();
            has_transfered = true;
            picked = true;
            round_picked = true;
            break;
        }
        if (!picked) // 没有能pick到这个卷积核的数据包，手动pick到下一个
            pickNextTarget();
        if (picker_tagret == start_picker_target) // 全部轮询了一遍都不行，取消遍历
        {
            if (!round_picked) // 轮询了一整圈都没有pick，退出该循环
                break;
            round_picked = false;
        }
    }


    // Pick延迟结束，准备进入Conv
    for (int i = 0; i < PickQueue.size(); i++)
    {
        DataPacket* packet = PickQueue.at(i);
        if (!packet->isDelayFinished())
            break;

        /**
          * 这里假装获取一下weight
          * 但是由于不进行计算
          * 故以delay进行设置，而非计算
          */

        // pick延迟结束，进入conv
        PickQueue.erase(PickQueue.begin() + i--);
        ConvQueue[packet->kernel_index].push_back(packet);
        packet->resetDelay(Dly_inConv);
        ConvWaiting[packet->kernel_index]--;
        has_transfered = true;
        DEB("ReqFIFO pick=> Conv %d\n", picker_tagret);
    }


    // DatLatch Pick给 ConvQueue。要先确定相同Tag的Req先发送才能发
    // Data跟着Req发送，不用再单独判断了
    /*for (unsigned int i = 0; i < DatLatch.size(); i++)
    { }*/


    // 每个 ConvQueue
    for (int i = 0; i < layer_kernel; i++)
    {
        FIFO& queue = ConvQueue[i];
        for (int j = 0; j < queue.size(); j++)
        {
            DataPacket* packet = queue.at(j);
            if (!packet->isDelayFinished())
                break;

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
            break;

        Conv2SndFIFO.erase(Conv2SndFIFO.begin() + i--);
        SndQueue.push_back(packet);
        packet->resetDelay(Dly_inSndFIFO);
        DEB("Conv => SndFIFO delay\n");
        has_transfered = true;
    }


    // SndFIFO
    for (int i = 0; i < SndQueue.size(); i++)
    {
        DataPacket* packet = SndQueue.at(i);
        if (!packet->isDelayFinished())
            break;

        PointVec points = packet->points;
        SndQueue.erase(SndQueue.begin() + i--);
        conved_points += points.size(); // 卷积完成的点的数量

        // 生成的点进入SndPipe
        for (int i = 0; i < points.size(); i++)
        {
            const PointBean point = points.at(i);
            // x>=3 且 y>=3 的点，可以用来生成新的坐标
            if (point.x < KERNEL_SIDE-1 || point.y < KERNEL_SIDE-1 || point.z != layer_channel-1)
                continue;

            /**
              * 假装这里packet执行 3*3*kernel 相乘操作
              * 例如224*224*3，生成222*222*3
              * 即 y>=3 且 x>=3 才能够计算，得出新的点
              */

            // 每一个点和前面3*3*kernel-1个点进行运算，得到一个全新的结果点
            // 结果点要重新编号，规则和之前的一样，而这也是下一层的编号
            TagType tag = (point.z << 16) + (point.y << 8) + point.x;
            DataPacket* resultPacket = new DataPacket(tag, (INT8)point.z, (INT8)point.y, (INT8)point.x);
            resultPacket->resetDelay(Dly_SndPipe);
            SndPipe.push_back(resultPacket);
        }

        delete packet;
        DEB("SndFIFO calculated\n");
        has_transfered = true;
    }

    // -------------------- 层分割线 -------------------


    // SndPipe存储的就是全部的结果了
    // SndPipe (SndFIFO => Switch)
    for (int i = 0; i < SndPipe.size(); i++)
    {
        DataPacket* packet = SndPipe.at(i);
        if (!packet->isDelayFinished())
            break;

        SndPipe.erase(SndPipe.begin() + i--);
        SwitchQueue.push_back(packet);
        packet->resetDelay(Dly_inSwitch);
        DEB("SndFIFO => Switch delay\n");
        has_transfered = true;
    }


    // -------------------- Switch分割线 --------------------

    // Switch发送至下一层
    for (int i = 0; i < SwitchQueue.size(); i++)
    {
        if (switch_bandwidth <= 0)
            break;
        DataPacket* packet = SwitchQueue.at(i);
        if (!packet->isDelayFinished())
            break;

        SwitchQueue.erase(SwitchQueue.begin() + i--);
        Switch2NextLayer.push_back(packet);
        packet->resetDelay(Dly_Switch2NextPE);
        DEB("Switch => NextLayer\n");
        has_transfered = true;
        switch_bandwidth--; // switch发送也要bandwidth
    }


    // Switch发往下一层的delay
    for (int i = 0; i < Switch2NextLayer.size(); i++)
    {
        DataPacket* packet = Switch2NextLayer.at(i);
        if (!packet->isDelayFinished())
            continue;

        Switch2NextLayer.erase(Switch2NextLayer.begin() + i--);
        NextLayerFIFO.push_back(packet);
        has_transfered = true;
    }


    // 传输到下一层的数据包队列，这一层已经不用管了
    if (NextLayerFIFO.size() == next_layer_points)
    {
        // 这一层完成，且全部传递完成
        // 创建新的feature map，下一个clock会读取，这样就进入了新的一层
        generalNextLayerMap();
    }


    // 如果这个函数中有数据传输
    // 那么继续重新传输
    // 解决单线程机制无法模拟多线程的多数据同步传输问题
    if (has_transfered)
    {
        // 这里全都是按照顺序来的，不需要再判断顺序了
        dataTransfer(); // 递归调用自己，直至 !has_transfered
    }
}

/**
 * 流逝了一个clock
 * 即所有队列中的packet的delay都next
 * 如果有新加的有延迟的队列，都要放里面
 */
void clockGoesBy()
{
    if (Dly_Map2RegFIFO && layer_start_clock + Dly_Map2RegFIFO >= global_clock)
    {
        for (unsigned int i = 0; i < StartQueue.size(); i++)
        {
            StartQueue[i]->delayToNext();
        }
    }

    if (Dly_inReqFIFO)
    {
        for (unsigned int i = 0; i < ReqQueue.size(); i++)
        {
            ReqQueue[i]->delayToNext();
        }
    }

    if (Dly_onPick)
    {
        for (unsigned int i = 0; i < PickQueue.size(); i++)
        {
            PickQueue[i]->delayToNext();
        }
    }

    if (Dly_inConv)
    {
        for (int i = 0; i < layer_kernel; i++)
        {
            for (unsigned int j = 0; j < ConvQueue[i].size(); j++)
            {
                ConvQueue[i].at(j)->delayToNext();
            }
        }
    }

    if (Dly_Conv2SndFIFO)
    {
        for (unsigned int i = 0; i < Conv2SndFIFO.size(); i++)
        {
            Conv2SndFIFO[i]->delayToNext();
        }
    }

    if (Dly_inSndFIFO)
    {
        for (unsigned int i = 0; i < SndQueue.size(); i++)
        {
            SndQueue[i]->delayToNext();
        }
    }

    if (Dly_SndPipe)
    {
        for (unsigned int i = 0; i < SndPipe.size(); i++)
        {
            SndPipe[i]->delayToNext();
        }
    }

    if (Dly_inSwitch)
    {
        for (unsigned int i = 0; i < SwitchQueue.size(); i++)
        {
            SwitchQueue[i]->delayToNext();
        }
    }

    if (Dly_Switch2NextPE)
    {
        for (unsigned int i = 0; i < Switch2NextLayer.size(); i++)
        {
            Switch2NextLayer[i]->delayToNext();
        }
    }
}

/**
 * 整个流控结束
 * 在这里输出各种结果
 */
void finishFlowControl()
{
    clock_t end = (clock() - start_time)/CLOCKS_PER_SEC;
    printf("total clock: %d, use time: %d s\n", global_clock, end);
    getchar();
}


#endif // FLOWCONTR_H

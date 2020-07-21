#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <list>
#include <time.h>
#include <QTimer>
#include <QDebug>
#include <QtConcurrent/QtConcurrent>
#include "datapacket.h"
#include "delaydefine.h"
#include "convolution.h"
#include "packetview.h"

#define DEB if (0) printf // 输出调试过程中的数据流动
#define PRINT_STATE true  // 输出当前layer、clock等信息
#define PRINT_MODULE true // 输出每一个模块中数据包的数量
#define DEB_MODE false    // 输出点的更多信息， 速度也会慢很多
#define STEP_MODE false   // 一步步停下，等待回车

typedef int ClockType;
typedef std::vector<DataPacket*> FIFO;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_actionRun_triggered();
    void on_actionRun_Extremly_triggered();

    void onTimerTimeOut();

protected:

    void initLayerResource();

    int getKernelCount(int layer);

    void splitMap2Queue(FeatureMap* map, Kernel* kernel, FIFO& queue);

    void startNewLayer();

    bool findTagInQueue(FIFO queue, TagType tag);

    void pickNextTarget();

    void convCalc();

    void generalNextLayerMap();

    void printState();

    void initFlowControl();

    void runFlowControl();

    void inClock();

    void dataTransfer();

    void clockGoesBy();

    void updatePacketPos();

    void finishFlowControl();

    DataPacketView* createPacketView(DataPacket* packet);

private:
    Ui::MainWindow *ui;
    QTimer *runtimer;

    int current_layer = 0;   // 当前正处在第几层
    int layer_channel = 3;   // 当前层图片深度，即channel数量
    int layer_kernel = 3;    // 当前层的kernel总数量
    int finished_kernel = 0; // 当前层结束的kernel数量
    std::vector<pthread_t*> conv_thread;   // 子线程对象

    // 从一开始到现在经过的clock
    ClockType global_clock = 0;
    int layer_start_clock = 0;
    clock_t start_time;
    bool concurrent_running = false; // 高并发运行
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
    FIFO StartFIFO; // 特征图的每一点生成后并传输到ReqQueue的队列
    FIFO ReqFIFO;   // 特征图的每一点req的队列；tag和data相同
    //FIFO DatLatch;   // 特征图的每一点data的队列；tag和req相同
    FIFO PickFIFO; // pick后进行delay的队列
    //PointVec ConvPoints[KERNEL_MAX_COUNT]; // 卷积数据等待队列
    FIFO ConvFIFOs[KERNEL_MAX_COUNT]; // 每个卷积结果队列
    int ConvWaitings[KERNEL_MAX_COUNT] = {}; // 等待pick到卷积队列的数量
    FIFO Conv2SndFIFO; // Conv => SndFIFO
    FIFO SndFIFO;   // 合并后的数据队列，发送到下一层
    FIFO SndPipe;    // 下一层的发送管道
    FIFO SwitchFIFO;
    FIFO Switch2NextLayer;
    FIFO NextLayerFIFO;

    // ==================== 图像操作 ===================
    FeatureMap* feature_map = NULL; // 当前特征图
    std::vector<Kernel*> kernels;   // 卷积核数组
    #ifdef Q_OS_WIN
    HANDLE HOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    #endif
};

#endif // MAINWINDOW_H
#include "delaydefine.h"
#include "layerthread.h"
#include "flowcontrol.h"

int main()
{
#if 1
    initFlowControl();

    runFlowControl();

    finishFlowControl();

#else

    // 多线程执行卷积可以使用此方法
    initLayerResource();

    while (true)
    {
        Sleep(1); // 避免直接卡死

        // 判断卷积子线程
        if (judgeConvolutionThreads())
        {
            // 一层卷积结束
            if (current_layer >= MAX_LAYER) // 超过层次上限
                break;
        }
    }
#endif

    return 0;
}

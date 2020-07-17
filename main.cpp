#include <algorithm>
#include "delaydefine.h"
#include "layerthread.h"

int main()
{
    initLayerResource();

    // 死循环一直等到picker
    while (true)
    {
        Sleep(1); // 避免直接卡死

        // 判断卷积子线程
        if (judgeConvolutionThreads())
        {
            // 超过层次上限
            if (current_layer >= MAX_LAYER)
                break;

            // 只是某一层结束

        }
    }

    return 0;
}

#include "convolution.h"

FeatureMap *convolution(FeatureMap *image, Kernel *kernel)
{
    int new_side = image->side - kernel->side + 1;
    FeatureMap* result = new FeatureMap(image->kernel, new_side, 1);
    INT8*** map = result->map;

    // 累加（注意：这里坐标反着的，先是y，再是x）
    for (int y = 0; y < new_side; y++)
    {
        for (int x = 0; x < new_side; x++)
        {
            // 新图的位置：map[y][x][ch]
            // TODO：这里可以加个缓存来加快速度
            INT8& v = map[y][x][0];
            for (int i = 0; i < kernel->side; i++)
                for (int j = 0; j < kernel->side; j++)
                    for (int k = 0; k < kernel->channel; k++)
                        v += image->map[i][j][k];
        }
    }
    return result;
}

INT8 ***create3D(int y, int x, int z)
{
    INT8*** bits = new INT8**[y];
    for (int i = 0; i < y; i++)
    {
        bits[i] = new INT8*[x];
        for (int j = 0; j < x; j++)
        {
            bits[i][j] = new INT8[z];
            for (int k = 0; k < z; k++)
                bits[i][j][k] = 0;
        }
    }
    return bits;
}

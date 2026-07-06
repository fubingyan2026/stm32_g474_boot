/*
        Copyright 2016 - 2023 Benjamin Vedder	benjamin@vedder.se

        This file is part of the VESC firmware.

        The VESC firmware is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        The VESC firmware is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program.  If not, see <http://www.gnu.org/licenses/>.
        */

#include "utils_math.h"

#include <stdlib.h>
#include <string.h>

/** @brief 32 点 FFT 基频正弦查找表（bin1 用） */
const float utils_tab_sin_32_1[] = {
    0.000000, 0.195090, 0.382683, 0.555570, 0.707107, 0.831470, 0.923880, 0.980785,
    1.000000, 0.980785, 0.923880, 0.831470, 0.707107, 0.555570, 0.382683, 0.195090,
    0.000000, -0.195090, -0.382683, -0.555570, -0.707107, -0.831470, -0.923880, -0.980785,
    -1.000000, -0.980785, -0.923880, -0.831470, -0.707107, -0.555570, -0.382683, -0.195090
};

/** @brief 32 点 FFT 二次谐波正弦查找表（bin2 用） */
const float utils_tab_sin_32_2[] = {
    0.000000, 0.382683, 0.707107, 0.923880, 1.000000, 0.923880, 0.707107, 0.382683,
    0.000000, -0.382683, -0.707107, -0.923880, -1.000000, -0.923880, -0.707107, -0.382683,
    -0.000000, 0.382683, 0.707107, 0.923880, 1.000000, 0.923880, 0.707107, 0.382683,
    0.000000, -0.382683, -0.707107, -0.923880, -1.000000, -0.923880, -0.707107, -0.382683
};

/** @brief 32 点 FFT 基频余弦查找表（bin1 用） */
const float utils_tab_cos_32_1[] = {
    1.000000, 0.980785, 0.923880, 0.831470, 0.707107, 0.555570, 0.382683, 0.195090,
    0.000000, -0.195090, -0.382683, -0.555570, -0.707107, -0.831470, -0.923880, -0.980785,
    -1.000000, -0.980785, -0.923880, -0.831470, -0.707107, -0.555570, -0.382683, -0.195090,
    -0.000000, 0.195090, 0.382683, 0.555570, 0.707107, 0.831470, 0.923880, 0.980785
};

/** @brief 32 点 FFT 二次谐波余弦查找表（bin2 用） */
const float utils_tab_cos_32_2[] = {
    1.000000, 0.923880, 0.707107, 0.382683, 0.000000, -0.382683, -0.707107, -0.923880,
    -1.000000, -0.923880, -0.707107, -0.382683, -0.000000, 0.382683, 0.707107, 0.923880,
    1.000000, 0.923880, 0.707107, 0.382683, 0.000000, -0.382683, -0.707107, -0.923880,
    -1.000000, -0.923880, -0.707107, -0.382683, -0.000000, 0.382683, 0.707107, 0.923880
};

/**
 * @brief 将角度映射到 [0, 1] 区间
 *
 * 给定一个角度范围 [min, max]（单位为弧度），计算输入角度 angle 在该范围内
 * 的归一化位置。返回值 0 表示 angle 等于 min，返回值 1 表示 angle 等于 max。
 *
 * 该函数正确处理了角度的循环特性（绕圈问题）。例如当 min=350°, max=10° 时，
 * 角度范围跨越了 0°/360° 的包裹点，函数会自动处理这种跨周期情况。
 *
 * 如果 angle 落在 [min, max] 范围之外，结果会被截断到 0 或 1。
 *
 * @param angle 待映射的角度值（弧度），任意值均可（会自动归一化）
 * @param min   范围起始角度（弧度）
 * @param max   范围结束角度（弧度）
 * @return      归一化后的位置，范围 [0, 1]；如果 max == min 则返回 -1
 */
float utils_map_angle(float angle, const float min, const float max)
{
    if (max == min) {
        return -1;
    }

    float range_pos = max - min;
    utils_norm_angle_rad(&range_pos);
    float range_neg = min - max;
    utils_norm_angle_rad(&range_neg);
    const float margin = range_neg * 0.5f;

    angle -= min;
    utils_norm_angle_rad(&angle);
    if (angle > M_PI * 2 - margin) {
        angle -= M_PI * 2;
    }

    float res = angle / range_pos;
    utils_truncate_number(&res, 0.0f, 1.0f);

    return res;
}

/**
 * @brief 对输入值施加死区映射
 *
 * 绝对值小于阈值 tres 的值被置为 0，大于阈值的值被线性重新映射，
 * 使得 tres 映射到 0、max 映射到 max，保证在死区边缘的连续性。
 *
 * 例如：value=0.6, tres=0.5, max=1.0 → 结果约 0.2（从死区边缘线性外推）
 *
 * @param value 待处理的输入值（会被原地修改）
 * @param tres  死区阈值
 * @param max   映射上限
 */
void utils_deadband(float* value, const float tres, const float max)
{
    if (fabsf(*value) < tres) {
        *value = 0.0f;
    } else {
        const float k = max / (max - tres);
        if (*value > 0.0f) {
            *value = k * *value + max * (1.0f - k);
        } else {
            *value = -(k * -*value + max * (1.0f - k));
        }
    }
}

/**
 * @brief 计算两个弧度角之间的差值
 *
 * 返回差值始终在 [-PI, +PI] 范围内，通过 utils_norm_angle_rad 自动处理环绕。
 *
 * @param angle1 第一个角度（弧度）
 * @param angle2 第二个角度（弧度）
 * @return       两个角度的差值（弧度），范围 [-PI, PI]
 */
float utils_angle_difference_rad(const float angle1, const float angle2)
{
    float difference = angle1 - angle2;
    utils_norm_angle_rad(&difference);
    return difference;
}

/**
 * @brief 在弧度角之间进行插值
 *
 * 相比普通线性插值，此函数正确处理了角度的循环特性：当 a1 和 a2 跨越 ±PI 边界时，
 * 会自动选择较短路径进行插值，避免插值结果绕远路。
 *
 * 例如：a1=170°, a2=-170°，普通插值中点会是 0°，而这里选择较短路径得到 180°（PI）。
 *
 * @param a1        第一个角度（弧度）
 * @param a2        第二个角度（弧度）
 * @param weight_a1 a1 的权重，1.0 时结果为 a1，0.0 时结果为 a2
 * @return          插值后的角度（弧度），已归一化到 [-PI, PI]
 */
float utils_interpolate_angles_rad(float a1, float a2, float weight_a1)
{
    while ((a1 - a2) > M_PI)
        a2 += 2.0f * M_PI;
    while ((a2 - a1) > M_PI)
        a1 += 2.0f * M_PI;

    float res = a1 * weight_a1 + a2 * (1.0f - weight_a1);
    utils_norm_angle_rad(&res);
    return res;
}

/**
 * @brief 返回三个浮点数中的中间值（中位数）
 *
 * @param a 第一个值
 * @param b 第二个值
 * @param c 第三个值
 * @return  三个值中的中间值
 */
float utils_middle_of_3(float a, float b, float c)
{
    float middle;

    if ((a <= b) && (a <= c)) {
        middle = (b <= c) ? b : c;
    } else if ((b <= a) && (b <= c)) {
        middle = (a <= c) ? a : c;
    } else {
        middle = (a <= b) ? a : b;
    }
    return middle;
}

/**
 * @brief 返回三个整数中的中间值（中位数）
 *
 * @param a 第一个值
 * @param b 第二个值
 * @param c 第三个值
 * @return  三个值中的中间值
 */
int utils_middle_of_3_int(int a, int b, int c)
{
    int middle;

    if ((a <= b) && (a <= c)) {
        middle = (b <= c) ? b : c;
    } else if ((b <= a) && (b <= c)) {
        middle = (a <= c) ? a : c;
    } else {
        middle = (a <= b) ? a : b;
    }
    return middle;
}

/**
 * @brief 返回两个数中绝对值较小的那个
 *
 * @param va 第一个值
 * @param vb 第二个值
 * @return   va 和 vb 中绝对值较小者（保留原始符号）
 */
float utils_min_abs(const float va, const float vb)
{
    float res;
    if (fabsf(va) < fabsf(vb)) {
        res = va;
    } else {
        res = vb;
    }

    return res;
}

/**
 * @brief 返回两个数中绝对值较大的那个
 *
 * @param va 第一个值
 * @param vb 第二个值
 * @return   va 和 vb 中绝对值较大者（保留原始符号）
 */
float utils_max_abs(float va, float vb)
{
    float res;
    if (fabsf(va) > fabsf(vb)) {
        res = va;
    } else {
        res = vb;
    }

    return res;
}

/**
 * @brief 将一个字节转换为二进制字符串表示
 *
 * 将整数 x 的低 8 位转换为 8 字符的二进制字符串（如 "01011010"），
 * 结果追加写入到 b 指向的缓冲区（b 必须已初始化为空字符串）。
 *
 * @param x 待转换的字节值（仅使用低 8 位）
 * @param b 输出缓冲区，至少 9 字节（8 字符 + 结束符）
 */
void utils_byte_to_binary(const int x, char* b)
{
    b[0] = '\0';

    for (int z = 128; z > 0; z >>= 1) {
        strcat(b, (x & z) == z ? "1" : "0");
    }
}

/**
 * @brief 油门曲线映射函数
 *
 * 将归一化输入 val ∈ [-1, 1] 按指定曲线模式映射到输出，支持正反向独立曲率。
 * 正向（val ≥ 0）使用 curve_acc 曲率，反向（val < 0）使用 curve_brake 曲率。
 *
 * 四种模式：
 * - mode=0 指数模式：ret = 1-(1-|val|)^(1+curve) 或 |val|^(1-curve)
 * - mode=1 自然指数：ret = 1-(exp(curve*(1-|val|))-1)/(exp(curve)-1)
 * - mode=2 多项式：  ret = 1-(1-|val|)/(1+curve*|val|) 或 |val|/(1-curve*(1-|val|))
 * - mode=3+线性：    ret = |val|（无曲线）
 *
 * 参考：http://math.stackexchange.com/questions/297768
 *
 * @param val         归一化输入值，会被截断到 [-1, 1]
 * @param curve_acc   正向（加速）曲率，0 为线性，正值越大约束越强
 * @param curve_brake 反向（制动）曲率，0 为线性
 * @param mode        曲线模式：0=指数, 1=自然指数, 2=多项式, 其他=线性
 * @return            映射后的输出值，范围 [-1, 1]，符号与 val 一致
 */
float utils_throttle_curve(float val, const float curve_acc, const float curve_brake, const int mode)
{
    float ret = 0.0f;

    if (val < -1.0f) {
        val = -1.0f;
    }

    if (val > 1.0f) {
        val = 1.0f;
    }

    const float val_a = fabsf(val);

    float curve;
    if (val >= 0.0f) {
        curve = curve_acc;
    } else {
        curve = curve_brake;
    }

    if (mode == 0) { // 指数模式
        if (curve >= 0.0f) {
            ret = 1.0f - powf(1.0f - val_a, 1.0f + curve);
        } else {
            ret = powf(val_a, 1.0f - curve);
        }
    } else if (mode == 1) { // 自然指数模式
        if (fabsf(curve) < 0.0f) {
            ret = val_a;
        } else {
            if (curve >= 0.0f) {
                ret = 1.0f - ((expf(curve * (1.0f - val_a)) - 1.0f) / (expf(curve) - 1.0f));
            } else {
                ret = (expf(-curve * val_a) - 1.0f) / (expf(-curve) - 1.0f);
            }
        }
    } else if (mode == 2) { // 多项式模式
        if (curve >= 0.0f) {
            ret = 1.0f - ((1.0f - val_a) / (1.0f + curve * val_a));
        } else {
            ret = val_a / (1.0f - curve * (1.0f - val_a));
        }
    } else { // 线性模式
        ret = val_a;
    }

    if (val < 0.0f) {
        ret = -ret;
    }

    return ret;
}

/**
 * @brief 32 点 FFT 的 DC 分量（bin0）计算
 *
 * 实际上就是 32 个采样点的算术平均值。对于纯实数输入，bin0 即为直流偏置。
 *
 * @param real_in 输入实数数组（32 点）
 * @param real    输出实部（直流分量）
 * @param imag    输出虚部（恒为 0）
 */
void utils_fft32_bin0(const float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;

    for (int i = 0; i < 32; i++) {
        *real += real_in[i];
    }

    *real /= 32.0f;
}

/**
 * @brief 32 点 FFT 的基频分量（bin1）计算
 *
 * 将 32 个采样点与基频正弦/余弦表做相关运算，提取基频（fs/32）的幅度和相位。
 *
 * @param real_in 输入实数数组（32 点）
 * @param real    输出实部
 * @param imag    输出虚部
 */
void utils_fft32_bin1(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;
    for (int i = 0; i < 32; i++) {
        *real += real_in[i] * utils_tab_cos_32_1[i];
        *imag -= real_in[i] * utils_tab_sin_32_1[i];
    }
    *real /= 32.0f;
    *imag /= 32.0f;
}

/**
 * @brief 32 点 FFT 的二次谐波分量（bin2）计算
 *
 * 提取 2×fs/32（即 fs/16）频率分量的幅度和相位。
 *
 * @param real_in 输入实数数组（32 点）
 * @param real    输出实部
 * @param imag    输出虚部
 */
void utils_fft32_bin2(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;
    for (int i = 0; i < 32; i++) {
        *real += real_in[i] * utils_tab_cos_32_2[i];
        *imag -= real_in[i] * utils_tab_sin_32_2[i];
    }
    *real /= 32.0f;
    *imag /= 32.0f;
}

/**
 * @brief 16 点 FFT 的 DC 分量（bin0）计算
 *
 * 16 个采样点的算术平均值。
 *
 * @param real_in 输入实数数组（16 点）
 * @param real    输出实部（直流分量）
 * @param imag    输出虚部（恒为 0）
 */
void utils_fft16_bin0(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;

    for (int i = 0; i < 16; i++) {
        *real += real_in[i];
    }

    *real /= 16.0f;
}

/**
 * @brief 16 点 FFT 的基频分量（bin1）计算
 *
 * 使用 32 点查找表的偶数下标（步长 2）对 16 点数据做基频提取。
 *
 * @param real_in 输入实数数组（16 点）
 * @param real    输出实部
 * @param imag    输出虚部
 */
void utils_fft16_bin1(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;
    for (int i = 0; i < 16; i++) {
        *real += real_in[i] * utils_tab_cos_32_1[2 * i];
        *imag -= real_in[i] * utils_tab_sin_32_1[2 * i];
    }
    *real /= 16.0f;
    *imag /= 16.0f;
}

/**
 * @brief 16 点 FFT 的二次谐波分量（bin2）计算
 *
 * @param real_in 输入实数数组（16 点）
 * @param real    输出实部
 * @param imag    输出虚部
 */
void utils_fft16_bin2(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;
    for (int i = 0; i < 16; i++) {
        *real += real_in[i] * utils_tab_cos_32_2[2 * i];
        *imag -= real_in[i] * utils_tab_sin_32_2[2 * i];
    }
    *real /= 16.0f;
    *imag /= 16.0f;
}

/**
 * @brief 8 点 FFT 的 DC 分量（bin0）计算
 *
 * 8 个采样点的算术平均值。
 *
 * @param real_in 输入实数数组（8 点）
 * @param real    输出实部（直流分量）
 * @param imag    输出虚部（恒为 0）
 */
void utils_fft8_bin0(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;

    for (int i = 0; i < 8; i++) {
        *real += real_in[i];
    }

    *real /= 8.0f;
}

/**
 * @brief 8 点 FFT 的基频分量（bin1）计算
 *
 * 使用 32 点查找表的每第 4 个下标（步长 4）对 8 点数据做基频提取。
 *
 * @param real_in 输入实数数组（8 点）
 * @param real    输出实部
 * @param imag    输出虚部
 */
void utils_fft8_bin1(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;
    for (int i = 0; i < 8; i++) {
        *real += real_in[i] * utils_tab_cos_32_1[4 * i];
        *imag -= real_in[i] * utils_tab_sin_32_1[4 * i];
    }
    *real /= 8.0f;
    *imag /= 8.0f;
}

/**
 * @brief 8 点 FFT 的二次谐波分量（bin2）计算
 *
 * @param real_in 输入实数数组（8 点）
 * @param real    输出实部
 * @param imag    输出虚部
 */
void utils_fft8_bin2(float* real_in, float* real, float* imag)
{
    *real = 0.0f;
    *imag = 0.0f;
    for (int i = 0; i < 8; i++) {
        *real += real_in[i] * utils_tab_cos_32_2[4 * i];
        *imag -= real_in[i] * utils_tab_sin_32_2[4 * i];
    }
    *real /= 8.0f;
    *imag /= 8.0f;
}

/**
 * @brief 锂电池归一化电压 → 剩余容量百分比映射
 *
 * 使用 4 阶多项式拟合 Samsung 30Q 电芯在 4.2V~3.2V 范围内的放电曲线。
 * 注意：此电压范围内实际可用容量约为标称 3Ah 的 85%（约 15% 不可用）。
 *
 * @param norm_v 归一化电压，输入会被截断到 [0, 1]
 * @return       剩余容量百分比（0~1），非线性和电压的关系
 */
float utils_batt_liion_norm_v_to_capacity(float norm_v)
{
    // 锂电池放电曲线的多项式拟合系数
    const float li_p[] = {
        -2.979767f, 5.487810f, -3.501286f, 1.675683f, 0.317147f
    };
    utils_truncate_number(&norm_v, 0.0f, 1.0f);
    float v2 = norm_v * norm_v;
    float v3 = v2 * norm_v;
    float v4 = v3 * norm_v;
    float v5 = v4 * norm_v;
    float capacity = li_p[0] * v5 + li_p[1] * v4 + li_p[2] * v3 + li_p[3] * v2 + li_p[4] * norm_v;
    return capacity;
}

/**
 * @brief qsort 比较函数：比较两个 uint16_t 值
 *
 * @param a 指向第一个值的指针
 * @param b 指向第二个值的指针
 * @return  差值（a - b）
 */
static int uint16_cmp_func(const void* a, const void* b)
{
    return (*(uint16_t*)a - *(uint16_t*)b);
}

/**
 * @brief 运行中值滤波器（滑动窗口）
 *
 * 维护一个长度为 filter_len 的环形缓冲区，每收到一个新采样就替换最旧的值，
 * 然后对窗口内所有数据排序取中位数。适合消除脉冲噪声同时保留信号边缘。
 *
 * 注意：每次调用都会执行 qsort，适合较低采样率场景；高频场景建议用更高效的
 * 在线中值滤波算法。
 *
 * @param buffer       环形缓冲区（调用者分配，长度 = filter_len）
 * @param buffer_index 当前写入位置索引（会被更新并自动取模）
 * @param filter_len   滤波器窗口长度
 * @param sample       新采样值
 * @return             当前窗口的中值
 */
uint16_t utils_median_filter_uint16_run(uint16_t* buffer,
    unsigned int* buffer_index, unsigned int filter_len, uint16_t sample)
{
    buffer[(*buffer_index)++] = sample;
    *buffer_index %= filter_len;
    uint16_t buffer_sorted[filter_len]; // 假设栈空间足够
    memcpy(buffer_sorted, buffer, sizeof(uint16_t) * filter_len);
    qsort(buffer_sorted, filter_len, sizeof(uint16_t), uint16_cmp_func);
    return buffer_sorted[filter_len / 2];
}

/**
 * @brief 3D 向量旋转（欧拉角方式）
 *
 * 使用 ZYX 顺序（先绕 Z 轴 yaw，再绕 Y 轴 pitch，再绕 X 轴 roll）的旋转矩阵
 * 对输入向量进行旋转。支持正向旋转和反向旋转（reverse=true 时使用转置矩阵）。
 *
 * 旋转矩阵元素在函数内根据 rotation 角度实时计算。
 *
 * @param input    输入向量 [x, y, z]
 * @param rotation 旋转角度 [roll, pitch, yaw]（弧度），按 Z→Y→X 顺序应用
 * @param output   输出向量 [x, y, z]（可以和 input 指向不同位置）
 * @param reverse  true=反向旋转（转置矩阵），false=正向旋转
 */
void utils_rotate_vector3(float* input, float* rotation, float* output, bool reverse)
{
    float s1, c1, s2, c2, s3, c3;

    if (rotation[2] != 0.0f) {
        s1 = sinf(rotation[2]);
        c1 = cosf(rotation[2]);
    } else {
        s1 = 0.0f;
        c1 = 1.0f;
    }

    if (rotation[1] != 0.0f) {
        s2 = sinf(rotation[1]);
        c2 = cosf(rotation[1]);
    } else {
        s2 = 0.0f;
        c2 = 1.0f;
    }

    if (rotation[0] != 0.0f) {
        s3 = sinf(rotation[0]);
        c3 = cosf(rotation[0]);
    } else {
        s3 = 0.0f;
        c3 = 1.0f;
    }

    float m11 = c1 * c2;
    float m12 = c1 * s2 * s3 - c3 * s1;
    float m13 = s1 * s3 + c1 * c3 * s2;
    float m21 = c2 * s1;
    float m22 = c1 * c3 + s1 * s2 * s3;
    float m23 = c3 * s1 * s2 - c1 * s3;
    float m31 = -s2;
    float m32 = c2 * s3;
    float m33 = c2 * c3;

    if (reverse) {
        output[0] = input[0] * m11 + input[1] * m21 + input[2] * m31;
        output[1] = input[0] * m12 + input[1] * m22 + input[2] * m32;
        output[2] = input[0] * m13 + input[1] * m23 + input[2] * m33;
    } else {
        output[0] = input[0] * m11 + input[1] * m12 + input[2] * m13;
        output[1] = input[0] * m21 + input[1] * m22 + input[2] * m23;
        output[2] = input[0] * m31 + input[1] * m32 + input[2] * m33;
    }
}

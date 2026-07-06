/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "filter.h"
#include "maths.h"

#define FAST_CODE 

/** @brief ln(2) 常量，用于 RC 低通滤波器参数计算 */
#define M_LN2_FLOAT 0.69314718055994530942f

/** @brief π 常量 */
#define M_PI_FLOAT 3.14159265358979323846f

/** @brief Butterworth 二阶滤波器品质因数 Q = 1/√2 ≈ 0.707，最大平坦度响应 */
#define BIQUAD_Q (0.70710678118654752440084436210485f)

// ============================================================================
// 空滤波器（直通）
// ============================================================================

/**
 * @brief 空滤波器：输入直通输出，不做任何滤波处理
 *
 * 用于占位或 bypass 场景，方便在不修改调用代码的情况下禁用滤波。
 *
 * @param filter 滤波器实例（未使用）
 * @param input  输入值
 * @return       原样返回输入值
 */
FAST_CODE float nullFilterApply(filter_t *filter, float input)
{
    return input;
}

// ============================================================================
// PT1 低通滤波器（一阶 RC 低通）
// 传递函数: H(s) = 1 / (1 + s * RC)
// 离散化: y[n] = y[n-1] + k * (x[n] - y[n-1])
//         k = dT / (RC + dT), RC = 1 / (2π * f_cut)
// ============================================================================

/**
 * @brief 计算 PT1 低通滤波器的增益系数 k
 *
 * k = dT / (RC + dT)，其中 RC = 1 / (2π × f_cut)。
 * k 越大，截止频率越高，响应越快；k 越小，滤波越强。
 *
 * @param f_cut 截止频率（Hz）
 * @param dT    采样周期（秒）
 * @return      滤波器增益系数 k，范围 (0, 1]
 */
float pt1FilterGain(uint16_t f_cut, float dT)
{
    float RC = 1 / (2 * M_PI_FLOAT * f_cut);
    return dT / (RC + dT);
}

/**
 * @brief 初始化 PT1 低通滤波器
 *
 * @param filter 滤波器实例
 * @param k      增益系数（可通过 pt1FilterGain 计算，或手动指定）
 */
void pt1FilterInit(pt1Filter_t *filter, float k)
{
    filter->state = 0.0f;
    filter->k = k;
}

/**
 * @brief 在线更新 PT1 滤波器的截止频率
 *
 * 直接修改增益系数 k，无需重新初始化，适合自适应滤波场景。
 *
 * @param filter 滤波器实例
 * @param k      新的增益系数
 */
void pt1FilterUpdateCutoff(pt1Filter_t *filter, float k)
{
    filter->k = k;
}

/**
 * @brief 对输入值施加 PT1 低通滤波
 *
 * 差分方程: y = y_prev + k × (x - y_prev)
 * 这是一个指数加权移动平均（EWMA），计算量极小，适合高频实时滤波。
 *
 * @param filter 滤波器实例
 * @param input  新采样值
 * @return       滤波后的输出值
 */
FAST_CODE float pt1FilterApply(pt1Filter_t *filter, float input)
{
    filter->state = filter->state + filter->k * (input - filter->state);
    return filter->state;
}

// ============================================================================
// Slew 滤波器（变化率限制器）
// 限制输出值的变化速率，类似一阶保持电路
// ============================================================================

/**
 * @brief 初始化 Slew 滤波器（变化率限制器）
 *
 * Slew 滤波器限制输出信号的变化速率，当状态超出阈值范围时，
 * 只允许信号沿当前方向以不超过 slewLimit 的步长变化。
 *
 * @param filter     滤波器实例
 * @param slewLimit  单步最大允许变化量（变化率限制）
 * @param threshold  阈值：|state|≥threshold 时启用变化率限制，否则直接跟随输入
 */
void slewFilterInit(slewFilter_t *filter, float slewLimit, float threshold)
{
    filter->state = 0.0f;
    filter->slewLimit = slewLimit;
    filter->threshold = threshold;
}

/**
 * @brief 对输入值施加 Slew 滤波（变化率限制）
 *
 * 工作逻辑：
 * - 当 |state| ≥ threshold 且输入在允许变化范围内：直接跟随输入
 * - 当 |state| ≥ threshold 且输入超出允许范围：保持当前状态（即限制变化率）
 * - 当 |state| < threshold：直接跟随输入（不限制）
 *
 * 典型用途：电机控制中限制电流/转矩指令的变化速率，防止冲击。
 *
 * @param filter 滤波器实例
 * @param input  新输入值
 * @return       滤波后的输出值（变化率受限）
 */
FAST_CODE float slewFilterApply(slewFilter_t *filter, float input)
{
    if (filter->state >= filter->threshold)
    {
        if (input >= filter->state - filter->slewLimit)
        {
            filter->state = input;
        }
    }
    else if (filter->state <= -filter->threshold)
    {
        if (input <= filter->state + filter->slewLimit)
        {
            filter->state = input;
        }
    }
    else
    {
        filter->state = input;
    }
    return filter->state;
}

// ============================================================================
// 双二阶滤波器（Biquad Filter）
// 通用二阶 IIR 滤波器，支持低通/带通/陷波三种类型
//
// 传递函数: H(z) = (b0 + b1*z^-1 + b2*z^-2) / (a0 + a1*z^-1 + a2*z^-2)
//
// 系数计算基于 Audio EQ Cookbook：
// omega = 2π * f0 / fs
// alpha = sin(omega) / (2 * Q)
// ============================================================================

/**
 * @brief 计算陷波滤波器的品质因数 Q
 *
 * Q = f0 × f1 / (f0² - f1²)，其中 f0 为中心频率，f1 为 -3dB 下截止频率。
 * f2（上截止频率）= f0² / f1，保证陷波器对称性。
 *
 * @param centerFreq 中心频率 f0（Hz）
 * @param cutoffFreq 下截止频率 f1（Hz）
 * @return           品质因数 Q
 */
float filterGetNotchQ(float centerFreq, float cutoffFreq)
{
    return centerFreq * cutoffFreq / (centerFreq * centerFreq - cutoffFreq * cutoffFreq);
}

/**
 * @brief 初始化双二阶滤波器为低通模式（Butterworth 响应）
 *
 * 便捷函数，使用 Q = 1/√2（Butterworth 最大平坦度）初始化 LPF。
 *
 * @param filter      滤波器实例
 * @param filterFreq  截止频率（Hz）
 * @param refreshRate 采样率（Hz）
 */
void biquadFilterInitLPF(biquadFilter_t *filter, float filterFreq, uint32_t refreshRate)
{
    biquadFilterInit(filter, filterFreq, refreshRate, BIQUAD_Q, FILTER_LPF);
}

/**
 * @brief 初始化双二阶滤波器
 *
 * 根据滤波器类型计算对应的系数（基于 Audio EQ Cookbook 公式）：
 *
 * - FILTER_LPF（低通）: 2 阶 Butterworth，f0 以上 -12dB/oct
 *   b0 = b2 = (1-cos)/2, b1 = 1-cos, a0 = 1+alpha, a1 = -2*cos, a2 = 1-alpha
 *
 * - FILTER_NOTCH（陷波）: 在 f0 处产生窄带衰减
 *   b0 = b2 = 1, b1 = -2*cos, a0 = 1+alpha, a1 = -2*cos, a2 = 1-alpha
 *
 * - FILTER_BPF（带通）: 仅保留 f0 附近频率分量
 *   b0 = alpha, b1 = 0, b2 = -alpha, a0 = 1+alpha, a1 = -2*cos, a2 = 1-alpha
 *
 * 参考: http://www.ti.com/lit/an/slaa447/slaa447.pdf
 *
 * @param filter      滤波器实例
 * @param filterFreq  特征频率（Hz），对于 LPF 为截止频率，NOTCH/BPF 为中心频率
 * @param refreshRate 采样率（Hz）
 * @param Q           品质因数
 * @param filterType  滤波器类型
 */
void biquadFilterInit(biquadFilter_t *filter, float filterFreq, uint32_t refreshRate, float Q, biquadFilterType_e filterType)
{
    // 预扭曲后的数字角频率（归一化到采样率）
    // omega = 2π * f0 / fs，单位: 弧度/采样
    const float omega = 2.0f * M_PI_FLOAT * filterFreq * refreshRate * 0.000001f;
    const float sn = sin_approx(omega);
    const float cs = cos_approx(omega);
    const float alpha = sn / (2.0f * Q);

    float b0 = 0, b1 = 0, b2 = 0, a0 = 0, a1 = 0, a2 = 0;

    switch (filterType)
    {
    case FILTER_LPF:
        b0 = (1 - cs) * 0.5f;
        b1 = 1 - cs;
        b2 = (1 - cs) * 0.5f;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    case FILTER_NOTCH:
        b0 = 1;
        b1 = -2 * cs;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    case FILTER_BPF:
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    }

    // 归一化系数（除以 a0），使最终传递函数分母常数项为 1
    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;

    // 清零延迟线
    filter->x1 = filter->x2 = 0;
    filter->y1 = filter->y2 = 0;
}

/**
 * @brief 在线更新双二阶滤波器参数
 *
 * 重新计算滤波系数但保留延迟线状态（x1/x2/y1/y2），避免更新参数时产生
 * 输出跳变。适用于频率扫描或自适应滤波场景。
 *
 * @param filter      滤波器实例
 * @param filterFreq  新的特征频率（Hz）
 * @param refreshRate 采样率（Hz）
 * @param Q           新的品质因数
 * @param filterType  新的滤波器类型
 */
FAST_CODE void biquadFilterUpdate(biquadFilter_t *filter, float filterFreq, uint32_t refreshRate, float Q, biquadFilterType_e filterType)
{
    // 备份延迟线状态
    float x1 = filter->x1;
    float x2 = filter->x2;
    float y1 = filter->y1;
    float y2 = filter->y2;

    biquadFilterInit(filter, filterFreq, refreshRate, Q, filterType);

    // 恢复延迟线状态
    filter->x1 = x1;
    filter->x2 = x2;
    filter->y1 = y1;
    filter->y2 = y2;
}

/**
 * @brief 在线更新双二阶滤波器为低通模式
 *
 * 便捷函数，以 Butterworth Q 值和 LPF 类型更新滤波器。
 *
 * @param filter      滤波器实例
 * @param filterFreq  新的截止频率（Hz）
 * @param refreshRate 采样率（Hz）
 */
FAST_CODE void biquadFilterUpdateLPF(biquadFilter_t *filter, float filterFreq, uint32_t refreshRate)
{
    biquadFilterUpdate(filter, filterFreq, refreshRate, BIQUAD_Q, FILTER_LPF);
}

/**
 * @brief 使用直接 I 型（Direct Form 1）计算双二阶滤波器输出
 *
 * 差分方程：
 * y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 *
 * 优点：系数变化时较稳定，适合动态修改参数的场景。
 * 缺点：比 DF2 多两个状态变量，精度略低（但通常可接受）。
 *
 * @param filter 滤波器实例
 * @param input  新输入采样值
 * @return       滤波后的输出值
 */
FAST_CODE float biquadFilterApplyDF1(biquadFilter_t *filter, float input)
{
    /* 计算当前输出 */
    const float result = filter->b0 * input + filter->b1 * filter->x1 + filter->b2 * filter->x2 - filter->a1 * filter->y1 - filter->a2 * filter->y2;

    /* 移位: x[n-1] → x[n-2], x[n] → x[n-1] */
    filter->x2 = filter->x1;
    filter->x1 = input;

    /* 移位: y[n-1] → y[n-2], y[n] → y[n-1] */
    filter->y2 = filter->y1;
    filter->y1 = result;

    return result;
}

/**
 * @brief 使用直接 II 型（Direct Form 2 / 转置型）计算双二阶滤波器输出
 *
 * 差分方程：
 * y[n] = b0*x[n] + w1[n-1]
 * w1[n] = b1*x[n] - a1*y[n] + w2[n-1]
 * w2[n] = b2*x[n] - a2*y[n]
 *
 * 优点：比 DF1 少两个状态变量，数值精度更高。
 * 缺点：系数动态变化时可能产生暂态响应，适合固定参数的场景。
 *
 * @param filter 滤波器实例
 * @param input  新输入采样值
 * @return       滤波后的输出值
 */
FAST_CODE float biquadFilterApply(biquadFilter_t *filter, float input)
{
    const float result = filter->b0 * input + filter->x1;
    filter->x1 = filter->b1 * input - filter->a1 * result + filter->x2;
    filter->x2 = filter->b2 * input - filter->a2 * result;
    return result;
}

// ============================================================================
// 滞后滑动平均滤波器（Lagged Moving Average）
// 使用环形缓冲区维护滑动窗口内的和，每次采样仅需一次加减即可更新
// ============================================================================

/**
 * @brief 初始化滞后滑动平均滤波器
 *
 * 该滤波器维护一个长度为 windowSize 的滑动窗口，累加窗口内所有样本并取平均。
 * 在窗口未填满（primed=false）时，仅用已收集的样本计算平均。
 *
 * @param filter     滤波器实例
 * @param windowSize 滑动窗口长度
 * @param buf        环形缓冲区（调用者分配，长度 = windowSize）
 */
void laggedMovingAverageInit(laggedMovingAverage_t *filter, uint16_t windowSize, float *buf)
{
    filter->movingWindowIndex = 0;
    filter->windowSize = windowSize;
    filter->buf = buf;
    filter->primed = false;
}

/**
 * @brief 对输入值施加滞后滑动平均滤波
 *
 * 每次更新：从总和中减去最旧样本、加入新样本、移动指针。
 * O(1) 时间复杂度，与窗口大小无关。
 *
 * 在窗口填满前（primed=false），除数使用已收集样本数而非 windowSize。
 *
 * @param filter 滤波器实例
 * @param input  新采样值
 * @return       滑动窗口内的算术平均值
 */
FAST_CODE float laggedMovingAverageUpdate(laggedMovingAverage_t *filter, float input)
{
    filter->movingSum -= filter->buf[filter->movingWindowIndex];
    filter->buf[filter->movingWindowIndex] = input;
    filter->movingSum += input;

    if (++filter->movingWindowIndex == filter->windowSize)
    {
        filter->movingWindowIndex = 0;
        filter->primed = true;
    }

    const uint16_t denom = filter->primed ? filter->windowSize : filter->movingWindowIndex;
    return filter->movingSum / denom;
}

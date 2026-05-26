#include <cmath>
#include <cstdio>
#include <cstring>

constexpr int N = 2048;
constexpr int M = N / 2;

static void mdct(const double* x, double* X) {
    for (int k = 0; k < M; ++k) {
        double sum = 0.0;
        for (int n = 0; n < N; ++n) {
            double arg = 2.0 * M_PI / static_cast<double>(N) *
                         (static_cast<double>(n) + static_cast<double>(N) / 4.0 + 0.5) *
                         (static_cast<double>(k) + 0.5);
            sum += x[n] * std::cos(arg);
        }
        X[k] = sum;
    }
}

static void imdct(const double* X, double* y) {
    double scale = 4.0 / static_cast<double>(N);
    for (int n = 0; n < N; ++n) {
        double sum = 0.0;
        for (int k = 0; k < M; ++k) {
            double arg = 2.0 * M_PI / static_cast<double>(N) *
                         (static_cast<double>(n) + static_cast<double>(N) / 4.0 + 0.5) *
                         (static_cast<double>(k) + 0.5);
            sum += X[k] * std::cos(arg);
        }
        y[n] = sum * scale;
    }
}

int main() {
    // Test 1: Check if MDCT is energy-preserving
    double x[N];
    for (int i = 0; i < N; ++i) x[i] = 1.0;  // DC
    double X[M];
    mdct(x, X);
    double x_energy = 0, X_energy = 0;
    for (int i = 0; i < N; ++i) x_energy += x[i] * x[i];
    for (int i = 0; i < M; ++i) X_energy += X[i] * X[i];
    printf("Energy: input=%.4f, spectrum=%.4f, ratio=%.6f\n", x_energy, X_energy, X_energy/x_energy);

    // Test 2: MDCT -> IMDCT round-trip (no windowing, no overlap)
    double y[N];
    imdct(X, y);
    double err = 0;
    for (int i = 0; i < N; ++i) {
        double d = y[i] - x[i];
        err += d * d;
    }
    printf("MDCT->IMDCT MSE (no window): %.6f\n", err / N);

    // Test 3: Full chain with window + overlap-add (single frame, zero overlap buffer)
    double window[N];
    for (int i = 0; i < N; ++i) window[i] = std::sin(M_PI * (i + 0.5) / N);

    double xw[N];
    for (int i = 0; i < N; ++i) xw[i] = x[i] * window[i];
    mdct(xw, X);
    imdct(X, y);
    for (int i = 0; i < N; ++i) y[i] *= window[i];

    // Overlap-add: first half = y[0..1023] (assuming zero overlap buffer)
    double output[M];
    double overlap[M];
    for (int i = 0; i < M; ++i) {
        output[i] = y[i];  // zero overlap buffer
        overlap[i] = y[M + i];
    }
    printf("\nSingle frame (DC=1.0, zero overlap):\n");
    printf("  output[0]=%.4f, output[512]=%.4f, output[1023]=%.4f\n", output[0], output[512], output[1023]);
    printf("  Expected: output[i] = window[i]^2 (since no overlap to add)\n");
    printf("  window[0]^2=%.4f, window[512]^2=%.4f, window[1023]^2=%.4f\n",
           window[0]*window[0], window[512]*window[512], window[1023]*window[1023]);

    // Test 4: Two-frame overlap-add (frame 0 zero padded in first half)
    // Frame 0: [0*M, DC*M]
    double pcm0[N] = {0};
    for (int i = M; i < N; ++i) pcm0[i] = 1.0;
    double xw0[N], y0[N];
    for (int i = 0; i < N; ++i) xw0[i] = pcm0[i] * window[i];
    mdct(xw0, X);
    imdct(X, y0);
    for (int i = 0; i < N; ++i) y0[i] *= window[i];

    // Frame 1: [DC*M, DC*M]
    double pcm1[N];
    for (int i = 0; i < N; ++i) pcm1[i] = 1.0;
    double xw1[N], y1[N];
    for (int i = 0; i < N; ++i) xw1[i] = pcm1[i] * window[i];
    mdct(xw1, X);
    imdct(X, y1);
    for (int i = 0; i < N; ++i) y1[i] *= window[i];

    // Frame 0 output (cold start)
    double out0[M], ov0[M];
    for (int i = 0; i < M; ++i) {
        out0[i] = y0[i];  // overlap buf is zero
        ov0[i] = y0[M + i];
    }

    // Frame 1 output (overlap-add with frame 0)
    double out1[M];
    for (int i = 0; i < M; ++i) {
        out1[i] = ov0[i] + y1[i];
    }

    printf("\nTwo-frame overlap-add (DC=1.0):\n");
    printf("  Frame 0 cold start output[0]=%.6f, [512]=%.6f, [1023]=%.6f\n", out0[0], out0[512], out0[1023]);
    printf("  Frame 1 output[0]=%.6f, [512]=%.6f, [1023]=%.6f\n", out1[0], out1[512], out1[1023]);

    double tdac_check = window[0]*window[0] + window[M]*window[M];
    printf("  TDAC check: w[0]^2 + w[1024]^2 = %.6f + %.6f = %.6f\n", window[0]*window[0], window[M]*window[M], tdac_check);

    // Test 5: What exactly is the sum for frame 1?
    printf("\n  Frame 1 detailed: ov0[0]=%.6f, y1[0]=%.6f, sum=%.6f\n", ov0[0], y1[0], ov0[0]+y1[0]);

    // The expected TDAC: output = 1.0 for all i (since input is DC=1.0)
    double mse = 0;
    for (int i = 0; i < M; ++i) {
        double d = out1[i] - 1.0;
        mse += d * d;
    }
    printf("  Frame 1 MSE (DC=1.0): %.10f\n", mse / M);

    // Test 6: MDCT coefficient magnitudes for DC and sine
    double sin_x[N];
    for (int i = 0; i < N; ++i)
        sin_x[i] = 20000.0 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / 44100.0);

    double sin_xw[N];
    for (int i = 0; i < N; ++i) sin_xw[i] = sin_x[i] * window[i];
    mdct(sin_xw, X);
    double max_coeff = 0;
    for (int i = 0; i < M; ++i)
        if (std::abs(X[i]) > max_coeff) max_coeff = std::abs(X[i]);
    printf("\nTest 6 - MDCT coefficient magnitudes:\n");
    printf("  amp=20000: max MDCT coeff = %.2f\n", max_coeff);
    for (int i = 0; i < N; ++i) sin_x[i] = 5000.0 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / 44100.0);
    for (int i = 0; i < N; ++i) sin_xw[i] = sin_x[i] * window[i];
    mdct(sin_xw, X);
    max_coeff = 0;
    for (int i = 0; i < M; ++i)
        if (std::abs(X[i]) > max_coeff) max_coeff = std::abs(X[i]);
    printf("  amp=5000: max MDCT coeff = %.2f\n", max_coeff);
    for (int i = 0; i < N; ++i) sin_x[i] = 100.0 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / 44100.0);
    for (int i = 0; i < N; ++i) sin_xw[i] = sin_x[i] * window[i];
    mdct(sin_xw, X);
    max_coeff = 0;
    for (int i = 0; i < M; ++i)
        if (std::abs(X[i]) > max_coeff) max_coeff = std::abs(X[i]);
    printf("  amp=100:  max MDCT coeff = %.2f\n", max_coeff);

    // Test 7: Three-frame overlap-add for amp=20000 sine (skip cold start)
    // Frame 0 (cold start): [0*1024, sine(-1024..-1)]
    // Frame 1: [sine(0..2047)]
    // Frame 2: [sine(1024..3071)]
    // Measure Frame 2 output vs sine(1024..2047)
    double pcm_st[3][N];
    for (int f = 0; f < 3; ++f) {
        for (int i = 0; i < N; ++i) {
            int ti = (f - 1) * M + i;
            if (ti >= 0) {
                pcm_st[f][i] = 20000.0 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(ti) / 44100.0);
            } else {
                pcm_st[f][i] = 0.0;
            }
        }
    }

    double overlap_buf[M] = {0};
    for (int f = 0; f < 3; ++f) {
        double xw[N], y_out[N];
        for (int i = 0; i < N; ++i) xw[i] = pcm_st[f][i] * window[i];
        mdct(xw, X);
        imdct(X, y_out);
        for (int i = 0; i < N; ++i) y_out[i] *= window[i];

        double frame_out[M];
        for (int i = 0; i < M; ++i) {
            frame_out[i] = overlap_buf[i] + y_out[i];
            overlap_buf[i] = y_out[M + i];
        }

        if (f == 2) {
            double mse3 = 0;
            for (int i = 0; i < M; ++i) {
                double orig = 20000.0 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(M + i) / 44100.0);
                double d = frame_out[i] - orig;
                mse3 += d * d;
            }
            mse3 /= M;
            double psnr3 = 10.0 * std::log10(32767.0 * 32767.0 / mse3);
            printf("\nTest 7 - Three-frame sine (skip cold start, no quantization):\n");
            printf("  MSE=%.6f, PSNR=%.2f dB (should be >80 dB)\n", mse3, psnr3);
            printf("  First 10: %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n",
                   frame_out[0], frame_out[1], frame_out[2], frame_out[3], frame_out[4],
                   frame_out[5], frame_out[6], frame_out[7], frame_out[8], frame_out[9]);
        }
    }

    return 0;
}
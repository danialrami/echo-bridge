#pragma once

// ShyFFT - A simple FFT implementation for embedded systems
// Based on Cooley-Tukey FFT algorithm

#include <cmath>
#include <complex>

template <typename T, size_t N>
class ShyFFT {
public:
    ShyFFT() {
        // Precompute twiddle factors
        for (size_t i = 0; i < N / 2; i++) {
            T angle = -2.0 * M_PI * i / N;
            twiddles_real_[i] = cos(angle);
            twiddles_imag_[i] = sin(angle);
        }
    }
    
    void Init() {
        // Precompute bit-reversed indices
        for (size_t i = 0; i < N; i++) {
            bit_reverse_[i] = BitReverse(i, log2N());
        }
    }
    
    // Direct FFT (time domain to frequency domain)
    void Direct(const T* input, T* output) {
        // Copy input to output with bit reversal
        for (size_t i = 0; i < N; i++) {
            output[i] = input[bit_reverse_[i]];
        }
        
        // Perform FFT
        for (size_t stage = 1; stage <= log2N(); stage++) {
            size_t m = 1 << stage;
            size_t m2 = m >> 1;
            
            for (size_t k = 0; k < N; k += m) {
                for (size_t j = 0; j < m2; j++) {
                    size_t i1 = k + j;
                    size_t i2 = i1 + m2;
                    
                    T re = output[i2] * twiddles_real_[j * N / m] - 0 * twiddles_imag_[j * N / m];
                    T im = output[i2] * twiddles_imag_[j * N / m] + 0 * twiddles_real_[j * N / m];
                    
                    output[i2] = output[i1] - re;
                    output[i1] = output[i1] + re;
                }
            }
        }
    }
    
    // Inverse FFT (frequency domain to time domain)
    void Inverse(T* real, T* imag, size_t size) {
        // Perform complex conjugate
        for (size_t i = 0; i < size; i++) {
            imag[i] = -imag[i];
        }
        
        // Perform FFT
        Complex(real, imag, size);
        
        // Scale output
        T scale = 1.0 / size;
        for (size_t i = 0; i < size; i++) {
            real[i] *= scale;
            imag[i] *= scale;
        }
    }
    
    // Complex FFT (frequency domain to frequency domain)
    void Complex(T* real, T* imag, size_t size) {
        // Copy input to temporary buffer with bit reversal
        T temp_real[N];
        T temp_imag[N];
        
        for (size_t i = 0; i < size; i++) {
            temp_real[bit_reverse_[i]] = real[i];
            temp_imag[bit_reverse_[i]] = imag[i];
        }
        
        // Perform FFT
        for (size_t stage = 1; stage <= log2N(); stage++) {
            size_t m = 1 << stage;
            size_t m2 = m >> 1;
            
            for (size_t k = 0; k < size; k += m) {
                for (size_t j = 0; j < m2; j++) {
                    size_t i1 = k + j;
                    size_t i2 = i1 + m2;
                    
                    T re = temp_real[i2] * twiddles_real_[j * N / m] - temp_imag[i2] * twiddles_imag_[j * N / m];
                    T im = temp_real[i2] * twiddles_imag_[j * N / m] + temp_imag[i2] * twiddles_real_[j * N / m];
                    
                    temp_real[i2] = temp_real[i1] - re;
                    temp_imag[i2] = temp_imag[i1] - im;
                    temp_real[i1] = temp_real[i1] + re;
                    temp_imag[i1] = temp_imag[i1] + im;
                }
            }
        }
        
        // Copy result back to input
        for (size_t i = 0; i < size; i++) {
            real[i] = temp_real[i];
            imag[i] = temp_imag[i];
        }
    }
    
private:
    // Precomputed twiddle factors
    T twiddles_real_[N / 2];
    T twiddles_imag_[N / 2];
    
    // Precomputed bit-reversed indices
    size_t bit_reverse_[N];
    
    // Compute log2(N)
    constexpr size_t log2N() const {
        size_t log2n = 0;
        size_t n = N;
        while (n > 1) {
            n >>= 1;
            log2n++;
        }
        return log2n;
    }
    
    // Bit-reverse an index
    size_t BitReverse(size_t index, size_t bits) const {
        size_t result = 0;
        for (size_t i = 0; i < bits; i++) {
            result = (result << 1) | (index & 1);
            index >>= 1;
        }
        return result;
    }
};

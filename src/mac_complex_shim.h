/* MSHV macOS C99 complex shim
 *
 * On macOS / libc++, <complex.h> only pulls in the C++ <complex> template
 * machinery — it does not expose the C99 macros (_Complex_I, I) nor the C
 * scalar helpers (cabs, creal, cimag, conj) that the MSHV decoders rely on.
 * Without those, fftw3.h's FFTW_DEFINE_COMPLEX selector also picks the
 * fall-back "double[2]" typedef for fftw_complex, which breaks scalar
 * arithmetic across the codebase.
 *
 * Re-establish the C99 path with clang/GCC built-ins so we don't conflict
 * with libc++'s std:: overloads.
 *
 * Include this *after* <complex.h> and *before* <Hv_Lib_fftw/fftw3.h>.
 */
#ifndef MSHV_MAC_COMPLEX_SHIM_H
#define MSHV_MAC_COMPLEX_SHIM_H

#if defined(_MACOS_)

#ifndef _Complex_I
#define _Complex_I ((double _Complex)__builtin_complex(0.0, 1.0))
#endif
#ifndef I
#define I _Complex_I
#endif

#ifdef __cplusplus
static inline double creal(double _Complex z)  { return __real__ z; }
static inline double cimag(double _Complex z)  { return __imag__ z; }
static inline double cabs (double _Complex z)
{
    double r = __real__ z, i = __imag__ z;
    return __builtin_sqrt(r*r + i*i);
}
static inline double _Complex conj(double _Complex z)
{
    return __builtin_complex(__real__ z, -(__imag__ z));
}
static inline double _Complex cexp(double _Complex z)
{
    double r = __real__ z, i = __imag__ z;
    double e = __builtin_exp(r);
    return __builtin_complex(e * __builtin_cos(i), e * __builtin_sin(i));
}
static inline double _Complex csqrt(double _Complex z)
{
    // principal-branch complex sqrt; implementation per C99 G.6.4.2.
    double x = __real__ z, y = __imag__ z;
    double m = __builtin_sqrt(x*x + y*y);
    double rr = __builtin_sqrt(0.5 * (m + x));
    double ii = __builtin_sqrt(0.5 * (m - x));
    if (y < 0) ii = -ii;
    return __builtin_complex(rr, ii);
}
static inline double _Complex clog(double _Complex z)
{
    double x = __real__ z, y = __imag__ z;
    return __builtin_complex(0.5 * __builtin_log(x*x + y*y), __builtin_atan2(y, x));
}
static inline float crealf(float _Complex z)  { return __real__ z; }
static inline float cimagf(float _Complex z)  { return __imag__ z; }
static inline float cabsf (float _Complex z)
{
    float r = __real__ z, i = __imag__ z;
    return __builtin_sqrtf(r*r + i*i);
}
static inline float _Complex conjf(float _Complex z)
{
    return __builtin_complex((float)__real__ z, (float)-(__imag__ z));
}
static inline float _Complex cexpf(float _Complex z)
{
    float r = __real__ z, i = __imag__ z;
    float e = __builtin_expf(r);
    return __builtin_complex(e * __builtin_cosf(i), e * __builtin_sinf(i));
}
#endif // __cplusplus

#endif // _MACOS_
#endif // MSHV_MAC_COMPLEX_SHIM_H

/* Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )
   Copyright (c) 2020  Hayati Ayguen ( h_ayguen@web.de )
   Copyright (c) 2020  Dario Mambro ( dario.mambro@gmail.com )

   Based on original fortran 77 code from FFTPACKv4 from NETLIB
   (http://www.netlib.org/fftpack), authored by Dr Paul Swarztrauber
   of NCAR, in 1985.

   As confirmed by the NCAR fftpack software curators, the following
   FFTPACKv5 license applies to FFTPACKv4 sources. My changes are
   released under the same terms.

   FFTPACK license:

   http://www.cisl.ucar.edu/css/software/fftpack5/ftpk.html

   Copyright (c) 2004 the University Corporation for Atmospheric
   Research ("UCAR"). All rights reserved. Developed by NCAR's
   Computational and Information Systems Laboratory, UCAR,
   www.cisl.ucar.edu.

   Redistribution and use of the Software in source and binary forms,
   with or without modification, is permitted provided that the
   following conditions are met:

   - Neither the names of NCAR's Computational and Information Systems
   Laboratory, the University Corporation for Atmospheric Research,
   nor the names of its sponsors or contributors may be used to
   endorse or promote products derived from this Software without
   specific prior written permission.  

   - Redistributions of source code must retain the above copyright
   notices, this list of conditions, and the disclaimer below.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions, and the disclaimer below in the
   documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
   SOFTWARE.


   PFFFT : a Pretty Fast FFT.

   This file is largerly based on the original FFTPACK implementation, modified in
   order to take advantage of SIMD instructions of modern CPUs.
*/

/* this file requires architecture specific preprocessor definitions
 * it's only for library internal use
 */


/* define own constants required to turn off g++ extensions .. */
#ifndef M_PI
  #define M_PI    3.14159265358979323846  /* pi */
#endif

#ifndef M_SQRT2
  #define M_SQRT2 1.41421356237309504880  /* sqrt(2) */
#endif


int FUNC_SIMD_SIZE(void) { return SIMD_SZ; }

int FUNC_MIN_FFT_SIZE(pffft_transform_t transform) {
  /* unfortunately, the fft size must be a multiple of 16 for complex FFTs
     and 32 for real FFTs -- a lot of stuff would need to be rewritten to
     handle other cases (or maybe just switch to a scalar fft, I don't know..) */
  int simdSz = FUNC_SIMD_SIZE();
  if (transform == PFFFT_REAL)
    return ( 2 * simdSz * simdSz );
  else if (transform == PFFFT_COMPLEX)
    return ( simdSz * simdSz );
  else
    return 1;
}

int FUNC_IS_VALID_SIZE(int N, pffft_transform_t cplx) {
  const int N_min = FUNC_MIN_FFT_SIZE(cplx);
  int R = N;
  while (R >= 5*N_min && (R % 5) == 0)  R /= 5;
  while (R >= 3*N_min && (R % 3) == 0)  R /= 3;
  while (R >= 2*N_min && (R % 2) == 0)  R /= 2;
  return (R == N_min) ? 1 : 0;
}

int FUNC_NEAREST_SIZE(int N, pffft_transform_t cplx, int higher) {
  int d;
  const int N_min = FUNC_MIN_FFT_SIZE(cplx);
  if (N < N_min)
    N = N_min;
  d = (higher) ? N_min : -N_min;
  if (d > 0)
    N = N_min * ((N+N_min-1) / N_min);  /* round up */
  else
    N = N_min * (N / N_min);  /* round down */

  for (; ; N += d)
    if (FUNC_IS_VALID_SIZE(N, cplx))
      return N;
}

const char * FUNC_SIMD_ARCH(void) { return VARCH; }


/*
  passf2 and passb2 has been merged here, fsign = -1 for passf2, +1 for passb2
*/
static NEVER_INLINE(void) passf2_ps(int ido, int l1, const v4sf *cc, v4sf *ch, const float *wa1, float fsign) {
  int k, i;
  int l1ido = l1*ido;
  if (ido <= 2) {
    for (k=0; k < l1ido; k += ido, ch += ido, cc+= 2*ido) {
      ch[0]         = VADD(cc[0], cc[ido+0]);
      ch[l1ido]     = VSUB(cc[0], cc[ido+0]);
      ch[1]         = VADD(cc[1], cc[ido+1]);
      ch[l1ido + 1] = VSUB(cc[1], cc[ido+1]);
    }
  } else {
    for (k=0; k < l1ido; k += ido, ch += ido, cc += 2*ido) {
      for (i=0; i<ido-1; i+=2) {
        v4sf tr2 = VSUB(cc[i+0], cc[i+ido+0]);
        v4sf ti2 = VSUB(cc[i+1], cc[i+ido+1]);
        v4sf wr = LD_PS1(wa1[i]), wi = VMUL(LD_PS1(fsign), LD_PS1(wa1[i+1]));
        ch[i]   = VADD(cc[i+0], cc[i+ido+0]);
        ch[i+1] = VADD(cc[i+1], cc[i+ido+1]);
        VCPLXMUL(tr2, ti2, wr, wi);
        ch[i+l1ido]   = tr2;
        ch[i+l1ido+1] = ti2;
      }
    }
  }
}

/*
  passf3 and passb3 has been merged here, fsign = -1 for passf3, +1 for passb3
*/
static NEVER_INLINE(void) passf3_ps(int ido, int l1, const v4sf *cc, v4sf *ch,
                                    const float *wa1, const float *wa2, float fsign) {
  static const float taur = -0.5f;
  float taui = 0.866025403784439f*fsign;
  int i, k;
  v4sf tr2, ti2, cr2, ci2, cr3, ci3, dr2, di2, dr3, di3;
  int l1ido = l1*ido;
  float wr1, wi1, wr2, wi2;
  assert(ido > 2);
  for (k=0; k< l1ido; k += ido, cc+= 3*ido, ch +=ido) {
    for (i=0; i<ido-1; i+=2) {
      tr2 = VADD(cc[i+ido], cc[i+2*ido]);
      cr2 = VADD(cc[i], SVMUL(taur,tr2));
      ch[i]    = VADD(cc[i], tr2);
      ti2 = VADD(cc[i+ido+1], cc[i+2*ido+1]);
      ci2 = VADD(cc[i    +1], SVMUL(taur,ti2));
      ch[i+1]  = VADD(cc[i+1], ti2);
      cr3 = SVMUL(taui, VSUB(cc[i+ido], cc[i+2*ido]));
      ci3 = SVMUL(taui, VSUB(cc[i+ido+1], cc[i+2*ido+1]));
      dr2 = VSUB(cr2, ci3);
      dr3 = VADD(cr2, ci3);
      di2 = VADD(ci2, cr3);
      di3 = VSUB(ci2, cr3);
      wr1=wa1[i], wi1=fsign*wa1[i+1], wr2=wa2[i], wi2=fsign*wa2[i+1]; 
      VCPLXMUL(dr2, di2, LD_PS1(wr1), LD_PS1(wi1));
      ch[i+l1ido] = dr2; 
      ch[i+l1ido + 1] = di2;
      VCPLXMUL(dr3, di3, LD_PS1(wr2), LD_PS1(wi2));
      ch[i+2*l1ido] = dr3;
      ch[i+2*l1ido+1] = di3;
    }
  }
} /* passf3 */

static NEVER_INLINE(void) passf4_ps(int ido, int l1, const v4sf *cc, v4sf *ch,
                                    const float *wa1, const float *wa2, const float *wa3, float fsign) {
  /* isign == -1 for forward transform and +1 for backward transform */

  int i, k;
  v4sf ci2, ci3, ci4, cr2, cr3, cr4, ti1, ti2, ti3, ti4, tr1, tr2, tr3, tr4;
  int l1ido = l1*ido;
  if (ido == 2) {
    for (k=0; k < l1ido; k += ido, ch += ido, cc += 4*ido) {
      tr1 = VSUB(cc[0], cc[2*ido + 0]);
      tr2 = VADD(cc[0], cc[2*ido + 0]);
      ti1 = VSUB(cc[1], cc[2*ido + 1]);
      ti2 = VADD(cc[1], cc[2*ido + 1]);
      ti4 = VMUL(VSUB(cc[1*ido + 0], cc[3*ido + 0]), LD_PS1(fsign));
      tr4 = VMUL(VSUB(cc[3*ido + 1], cc[1*ido + 1]), LD_PS1(fsign));
      tr3 = VADD(cc[ido + 0], cc[3*ido + 0]);
      ti3 = VADD(cc[ido + 1], cc[3*ido + 1]);

      ch[0*l1ido + 0] = VADD(tr2, tr3);
      ch[0*l1ido + 1] = VADD(ti2, ti3);
      ch[1*l1ido + 0] = VADD(tr1, tr4);
      ch[1*l1ido + 1] = VADD(ti1, ti4);
      ch[2*l1ido + 0] = VSUB(tr2, tr3);
      ch[2*l1ido + 1] = VSUB(ti2, ti3);        
      ch[3*l1ido + 0] = VSUB(tr1, tr4);
      ch[3*l1ido + 1] = VSUB(ti1, ti4);
    }
  } else {
    for (k=0; k < l1ido; k += ido, ch+=ido, cc += 4*ido) {
      for (i=0; i<ido-1; i+=2) {
        float wr1, wi1, wr2, wi2, wr3, wi3;
        tr1 = VSUB(cc[i + 0], cc[i + 2*ido + 0]);
        tr2 = VADD(cc[i + 0], cc[i + 2*ido + 0]);
        ti1 = VSUB(cc[i + 1], cc[i + 2*ido + 1]);
        ti2 = VADD(cc[i + 1], cc[i + 2*ido + 1]);
        tr4 = VMUL(VSUB(cc[i + 3*ido + 1], cc[i + 1*ido + 1]), LD_PS1(fsign));
        ti4 = VMUL(VSUB(cc[i + 1*ido + 0], cc[i + 3*ido + 0]), LD_PS1(fsign));
        tr3 = VADD(cc[i + ido + 0], cc[i + 3*ido + 0]);
        ti3 = VADD(cc[i + ido + 1], cc[i + 3*ido + 1]);

        ch[i] = VADD(tr2, tr3);
        cr3    = VSUB(tr2, tr3);
        ch[i + 1] = VADD(ti2, ti3);
        ci3 = VSUB(ti2, ti3);

        cr2 = VADD(tr1, tr4);
        cr4 = VSUB(tr1, tr4);
        ci2 = VADD(ti1, ti4);
        ci4 = VSUB(ti1, ti4);
        wr1=wa1[i], wi1=fsign*wa1[i+1];
        VCPLXMUL(cr2, ci2, LD_PS1(wr1), LD_PS1(wi1));
        wr2=wa2[i], wi2=fsign*wa2[i+1]; 
        ch[i + l1ido] = cr2;
        ch[i + l1ido + 1] = ci2;

        VCPLXMUL(cr3, ci3, LD_PS1(wr2), LD_PS1(wi2));
        wr3=wa3[i], wi3=fsign*wa3[i+1]; 
        ch[i + 2*l1ido] = cr3;
        ch[i + 2*l1ido + 1] = ci3;

        VCPLXMUL(cr4, ci4, LD_PS1(wr3), LD_PS1(wi3));
        ch[i + 3*l1ido] = cr4;
        ch[i + 3*l1ido + 1] = ci4;
      }
    }
  }
} /* passf4 */

/*
  passf5 and passb5 has been merged here, fsign = -1 for passf5, +1 for passb5
*/
static NEVER_INLINE(void) passf5_ps(int ido, int l1, const v4sf *cc, v4sf *ch,
                                    const float *wa1, const float *wa2, 
                                    const float *wa3, const float *wa4, float fsign) {  
  static const float tr11 = .309016994374947f;
  const float ti11 = .951056516295154f*fsign;
  static const float tr12 = -.809016994374947f;
  const float ti12 = .587785252292473f*fsign;

  /* Local variables */
  int i, k;
  v4sf ci2, ci3, ci4, ci5, di3, di4, di5, di2, cr2, cr3, cr5, cr4, ti2, ti3,
    ti4, ti5, dr3, dr4, dr5, dr2, tr2, tr3, tr4, tr5;

  float wr1, wi1, wr2, wi2, wr3, wi3, wr4, wi4;

#define cc_ref(a_1,a_2) cc[(a_2-1)*ido + a_1 + 1]
#define ch_ref(a_1,a_3) ch[(a_3-1)*l1*ido + a_1 + 1]

  assert(ido > 2);
  for (k = 0; k < l1; ++k, cc += 5*ido, ch += ido) {
    for (i = 0; i < ido-1; i += 2) {
      ti5 = VSUB(cc_ref(i  , 2), cc_ref(i  , 5));
      ti2 = VADD(cc_ref(i  , 2), cc_ref(i  , 5));
      ti4 = VSUB(cc_ref(i  , 3), cc_ref(i  , 4));
      ti3 = VADD(cc_ref(i  , 3), cc_ref(i  , 4));
      tr5 = VSUB(cc_ref(i-1, 2), cc_ref(i-1, 5));
      tr2 = VADD(cc_ref(i-1, 2), cc_ref(i-1, 5));
      tr4 = VSUB(cc_ref(i-1, 3), cc_ref(i-1, 4));
      tr3 = VADD(cc_ref(i-1, 3), cc_ref(i-1, 4));
      ch_ref(i-1, 1) = VADD(cc_ref(i-1, 1), VADD(tr2, tr3));
      ch_ref(i  , 1) = VADD(cc_ref(i  , 1), VADD(ti2, ti3));
      cr2 = VADD(cc_ref(i-1, 1), VADD(SVMUL(tr11, tr2),SVMUL(tr12, tr3)));
      ci2 = VADD(cc_ref(i  , 1), VADD(SVMUL(tr11, ti2),SVMUL(tr12, ti3)));
      cr3 = VADD(cc_ref(i-1, 1), VADD(SVMUL(tr12, tr2),SVMUL(tr11, tr3)));
      ci3 = VADD(cc_ref(i  , 1), VADD(SVMUL(tr12, ti2),SVMUL(tr11, ti3)));
      cr5 = VADD(SVMUL(ti11, tr5), SVMUL(ti12, tr4));
      ci5 = VADD(SVMUL(ti11, ti5), SVMUL(ti12, ti4));
      cr4 = VSUB(SVMUL(ti12, tr5), SVMUL(ti11, tr4));
      ci4 = VSUB(SVMUL(ti12, ti5), SVMUL(ti11, ti4));
      dr3 = VSUB(cr3, ci4);
      dr4 = VADD(cr3, ci4);
      di3 = VADD(ci3, cr4);
      di4 = VSUB(ci3, cr4);
      dr5 = VADD(cr2, ci5);
      dr2 = VSUB(cr2, ci5);
      di5 = VSUB(ci2, cr5);
      di2 = VADD(ci2, cr5);
      wr1=wa1[i], wi1=fsign*wa1[i+1], wr2=wa2[i], wi2=fsign*wa2[i+1]; 
      wr3=wa3[i], wi3=fsign*wa3[i+1], wr4=wa4[i], wi4=fsign*wa4[i+1]; 
      VCPLXMUL(dr2, di2, LD_PS1(wr1), LD_PS1(wi1));
      ch_ref(i - 1, 2) = dr2;
      ch_ref(i, 2)     = di2;
      VCPLXMUL(dr3, di3, LD_PS1(wr2), LD_PS1(wi2));
      ch_ref(i - 1, 3) = dr3;
      ch_ref(i, 3)     = di3;
      VCPLXMUL(dr4, di4, LD_PS1(wr3), LD_PS1(wi3));
      ch_ref(i - 1, 4) = dr4;
      ch_ref(i, 4)     = di4;
      VCPLXMUL(dr5, di5, LD_PS1(wr4), LD_PS1(wi4));
      ch_ref(i - 1, 5) = dr5;
      ch_ref(i, 5)     = di5;
    }
  }
#undef ch_ref
#undef cc_ref
}

static NEVER_INLINE(void) radf2_ps(int ido, int l1, const v4sf * RESTRICT cc, v4sf * RESTRICT ch, const float *wa1) {
  static const float minus_one = -1.f;
  int i, k, l1ido = l1*ido;
  for (k=0; k < l1ido; k += ido) {
    v4sf a = cc[k], b = cc[k + l1ido];
    ch[2*k] = VADD(a, b);
    ch[2*(k+ido)-1] = VSUB(a, b);
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k=0; k < l1ido; k += ido) {
      for (i=2; i<ido; i+=2) {
        v4sf tr2 = cc[i - 1 + k + l1ido], ti2 = cc[i + k + l1ido];
        v4sf br = cc[i - 1 + k], bi = cc[i + k];
        VCPLXMULCONJ(tr2, ti2, LD_PS1(wa1[i - 2]), LD_PS1(wa1[i - 1])); 
        ch[i + 2*k] = VADD(bi, ti2);
        ch[2*(k+ido) - i] = VSUB(ti2, bi);
        ch[i - 1 + 2*k] = VADD(br, tr2);
        ch[2*(k+ido) - i -1] = VSUB(br, tr2);
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k=0; k < l1ido; k += ido) {
    ch[2*k + ido] = SVMUL(minus_one, cc[ido-1 + k + l1ido]);
    ch[2*k + ido-1] = cc[k + ido-1];
  }
} /* radf2 */


static NEVER_INLINE(void) radb2_ps(int ido, int l1, const v4sf *cc, v4sf *ch, const float *wa1) {
  static const float minus_two=-2;
  int i, k, l1ido = l1*ido;
  v4sf a,b,c,d, tr2, ti2;
  for (k=0; k < l1ido; k += ido) {
    a = cc[2*k]; b = cc[2*(k+ido) - 1];
    ch[k] = VADD(a, b);
    ch[k + l1ido] =VSUB(a, b);
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k = 0; k < l1ido; k += ido) {
      for (i = 2; i < ido; i += 2) {
        a = cc[i-1 + 2*k]; b = cc[2*(k + ido) - i - 1];
        c = cc[i+0 + 2*k]; d = cc[2*(k + ido) - i + 0];
        ch[i-1 + k] = VADD(a, b);
        tr2 = VSUB(a, b);
        ch[i+0 + k] = VSUB(c, d);
        ti2 = VADD(c, d);
        VCPLXMUL(tr2, ti2, LD_PS1(wa1[i - 2]), LD_PS1(wa1[i - 1]));
        ch[i-1 + k + l1ido] = tr2;
        ch[i+0 + k + l1ido] = ti2;
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k = 0; k < l1ido; k += ido) {
    a = cc[2*k + ido-1]; b = cc[2*k + ido];
    ch[k + ido-1] = VADD(a,a);
    ch[k + ido-1 + l1ido] = SVMUL(minus_two, b);
  }
} /* radb2 */

static void radf3_ps(int ido, int l1, const v4sf * RESTRICT cc, v4sf * RESTRICT ch,
                     const float *wa1, const float *wa2) {
  static const float taur = -0.5f;
  static const float taui = 0.866025403784439f;
  int i, k, ic;
  v4sf ci2, di2, di3, cr2, dr2, dr3, ti2, ti3, tr2, tr3, wr1, wi1, wr2, wi2;
  for (k=0; k<l1; k++) {
    cr2 = VADD(cc[(k + l1)*ido], cc[(k + 2*l1)*ido]);
    ch[3*k*ido] = VADD(cc[k*ido], cr2);
    ch[(3*k+2)*ido] = SVMUL(taui, VSUB(cc[(k + l1*2)*ido], cc[(k + l1)*ido]));
    ch[ido-1 + (3*k + 1)*ido] = VADD(cc[k*ido], SVMUL(taur, cr2));
  }
  if (ido == 1) return;
  for (k=0; k<l1; k++) {
    for (i=2; i<ido; i+=2) {
      ic = ido - i;
      wr1 = LD_PS1(wa1[i - 2]); wi1 = LD_PS1(wa1[i - 1]);
      dr2 = cc[i - 1 + (k + l1)*ido]; di2 = cc[i + (k + l1)*ido];
      VCPLXMULCONJ(dr2, di2, wr1, wi1);

      wr2 = LD_PS1(wa2[i - 2]); wi2 = LD_PS1(wa2[i - 1]);
      dr3 = cc[i - 1 + (k + l1*2)*ido]; di3 = cc[i + (k + l1*2)*ido];
      VCPLXMULCONJ(dr3, di3, wr2, wi2);
        
      cr2 = VADD(dr2, dr3);
      ci2 = VADD(di2, di3);
      ch[i - 1 + 3*k*ido] = VADD(cc[i - 1 + k*ido], cr2);
      ch[i + 3*k*ido] = VADD(cc[i + k*ido], ci2);
      tr2 = VADD(cc[i - 1 + k*ido], SVMUL(taur, cr2));
      ti2 = VADD(cc[i + k*ido], SVMUL(taur, ci2));
      tr3 = SVMUL(taui, VSUB(di2, di3));
      ti3 = SVMUL(taui, VSUB(dr3, dr2));
      ch[i - 1 + (3*k + 2)*ido] = VADD(tr2, tr3);
      ch[ic - 1 + (3*k + 1)*ido] = VSUB(tr2, tr3);
      ch[i + (3*k + 2)*ido] = VADD(ti2, ti3);
      ch[ic + (3*k + 1)*ido] = VSUB(ti3, ti2);
    }
  }
} /* radf3 */


static void radb3_ps(int ido, int l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
                     const float *wa1, const float *wa2)
{
  static const float taur = -0.5f;
  static const float taui = 0.866025403784439f;
  static const float taui_2 = 0.866025403784439f*2;
  int i, k, ic;
  v4sf ci2, ci3, di2, di3, cr2, cr3, dr2, dr3, ti2, tr2;
  for (k=0; k<l1; k++) {
    tr2 = cc[ido-1 + (3*k + 1)*ido]; tr2 = VADD(tr2,tr2);
    cr2 = VMADD(LD_PS1(taur), tr2, cc[3*k*ido]);
    ch[k*ido] = VADD(cc[3*k*ido], tr2);
    ci3 = SVMUL(taui_2, cc[(3*k + 2)*ido]);
    ch[(k + l1)*ido] = VSUB(cr2, ci3);
    ch[(k + 2*l1)*ido] = VADD(cr2, ci3);
  }
  if (ido == 1) return;
  for (k=0; k<l1; k++) {
    for (i=2; i<ido; i+=2) {
      ic = ido - i;
      tr2 = VADD(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido]);
      cr2 = VMADD(LD_PS1(taur), tr2, cc[i - 1 + 3*k*ido]);
      ch[i - 1 + k*ido] = VADD(cc[i - 1 + 3*k*ido], tr2);
      ti2 = VSUB(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido]);
      ci2 = VMADD(LD_PS1(taur), ti2, cc[i + 3*k*ido]);
      ch[i + k*ido] = VADD(cc[i + 3*k*ido], ti2);
      cr3 = SVMUL(taui, VSUB(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido]));
      ci3 = SVMUL(taui, VADD(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido]));
      dr2 = VSUB(cr2, ci3);
      dr3 = VADD(cr2, ci3);
      di2 = VADD(ci2, cr3);
      di3 = VSUB(ci2, cr3);
      VCPLXMUL(dr2, di2, LD_PS1(wa1[i-2]), LD_PS1(wa1[i-1]));
      ch[i - 1 + (k + l1)*ido] = dr2;
      ch[i + (k + l1)*ido] = di2;
      VCPLXMUL(dr3, di3, LD_PS1(wa2[i-2]), LD_PS1(wa2[i-1]));
      ch[i - 1 + (k + 2*l1)*ido] = dr3;
      ch[i + (k + 2*l1)*ido] = di3;
    }
  }
} /* radb3 */

static NEVER_INLINE(void) radf4_ps(int ido, int l1, const v4sf *RESTRICT cc, v4sf * RESTRICT ch,
                                   const float * RESTRICT wa1, const float * RESTRICT wa2, const float * RESTRICT wa3)
{
  static const float minus_hsqt2 = (float)-0.7071067811865475;
  int i, k, l1ido = l1*ido;
  {
    const v4sf *RESTRICT cc_ = cc, * RESTRICT cc_end = cc + l1ido; 
    v4sf * RESTRICT ch_ = ch;
    while (cc < cc_end) {
      /* this loop represents between 25% and 40% of total radf4_ps cost ! */
      v4sf a0 = cc[0], a1 = cc[l1ido];
      v4sf a2 = cc[2*l1ido], a3 = cc[3*l1ido];
      v4sf tr1 = VADD(a1, a3);
      v4sf tr2 = VADD(a0, a2);
      ch[2*ido-1] = VSUB(a0, a2);
      ch[2*ido  ] = VSUB(a3, a1);
      ch[0      ] = VADD(tr1, tr2);
      ch[4*ido-1] = VSUB(tr2, tr1);
      cc += ido; ch += 4*ido;
    }
    cc = cc_; ch = ch_;
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k = 0; k < l1ido; k += ido) {
      const v4sf * RESTRICT pc = (v4sf*)(cc + 1 + k);
      for (i=2; i<ido; i += 2, pc += 2) {
        int ic = ido - i;
        v4sf wr, wi, cr2, ci2, cr3, ci3, cr4, ci4;
        v4sf tr1, ti1, tr2, ti2, tr3, ti3, tr4, ti4;

        cr2 = pc[1*l1ido+0];
        ci2 = pc[1*l1ido+1];
        wr=LD_PS1(wa1[i - 2]);
        wi=LD_PS1(wa1[i - 1]);
        VCPLXMULCONJ(cr2,ci2,wr,wi);

        cr3 = pc[2*l1ido+0];
        ci3 = pc[2*l1ido+1];
        wr = LD_PS1(wa2[i-2]); 
        wi = LD_PS1(wa2[i-1]);
        VCPLXMULCONJ(cr3, ci3, wr, wi);

        cr4 = pc[3*l1ido];
        ci4 = pc[3*l1ido+1];
        wr = LD_PS1(wa3[i-2]); 
        wi = LD_PS1(wa3[i-1]);
        VCPLXMULCONJ(cr4, ci4, wr, wi);

        /* at this point, on SSE, five of "cr2 cr3 cr4 ci2 ci3 ci4" should be loaded in registers */

        tr1 = VADD(cr2,cr4);
        tr4 = VSUB(cr4,cr2); 
        tr2 = VADD(pc[0],cr3);
        tr3 = VSUB(pc[0],cr3);
        ch[i - 1 + 4*k] = VADD(tr1,tr2);
        ch[ic - 1 + 4*k + 3*ido] = VSUB(tr2,tr1); /* at this point tr1 and tr2 can be disposed */
        ti1 = VADD(ci2,ci4);
        ti4 = VSUB(ci2,ci4);
        ch[i - 1 + 4*k + 2*ido] = VADD(ti4,tr3);
        ch[ic - 1 + 4*k + 1*ido] = VSUB(tr3,ti4); /* dispose tr3, ti4 */
        ti2 = VADD(pc[1],ci3);
        ti3 = VSUB(pc[1],ci3);
        ch[i + 4*k] = VADD(ti1, ti2);
        ch[ic + 4*k + 3*ido] = VSUB(ti1, ti2);
        ch[i + 4*k + 2*ido] = VADD(tr4, ti3);
        ch[ic + 4*k + 1*ido] = VSUB(tr4, ti3);
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k=0; k<l1ido; k += ido) {
    v4sf a = cc[ido-1 + k + l1ido], b = cc[ido-1 + k + 3*l1ido];
    v4sf c = cc[ido-1 + k], d = cc[ido-1 + k + 2*l1ido];
    v4sf ti1 = SVMUL(minus_hsqt2, VADD(a, b));
    v4sf tr1 = SVMUL(minus_hsqt2, VSUB(b, a));
    ch[ido-1 + 4*k] = VADD(tr1, c);
    ch[ido-1 + 4*k + 2*ido] = VSUB(c, tr1);
    ch[4*k + 1*ido] = VSUB(ti1, d); 
    ch[4*k + 3*ido] = VADD(ti1, d); 
  }
} /* radf4 */


static NEVER_INLINE(void) radb4_ps(int ido, int l1, const v4sf * RESTRICT cc, v4sf * RESTRICT ch,
                                   const float * RESTRICT wa1, const float * RESTRICT wa2, const float *RESTRICT wa3)
{
  static const float minus_sqrt2 = (float)-1.414213562373095;
  static const float two = 2.f;
  int i, k, l1ido = l1*ido;
  v4sf ci2, ci3, ci4, cr2, cr3, cr4, ti1, ti2, ti3, ti4, tr1, tr2, tr3, tr4;
  {
    const v4sf *RESTRICT cc_ = cc, * RESTRICT ch_end = ch + l1ido; 
    v4sf *ch_ = ch;
    while (ch < ch_end) {
      v4sf a = cc[0], b = cc[4*ido-1];
      v4sf c = cc[2*ido], d = cc[2*ido-1];
      tr3 = SVMUL(two,d);
      tr2 = VADD(a,b);
      tr1 = VSUB(a,b);
      tr4 = SVMUL(two,c);
      ch[0*l1ido] = VADD(tr2, tr3);
      ch[2*l1ido] = VSUB(tr2, tr3);
      ch[1*l1ido] = VSUB(tr1, tr4);
      ch[3*l1ido] = VADD(tr1, tr4);
      
      cc += 4*ido; ch += ido;
    }
    cc = cc_; ch = ch_;
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k = 0; k < l1ido; k += ido) {
      const v4sf * RESTRICT pc = (v4sf*)(cc - 1 + 4*k);
      v4sf * RESTRICT ph = (v4sf*)(ch + k + 1);
      for (i = 2; i < ido; i += 2) {

        tr1 = VSUB(pc[i], pc[4*ido - i]);
        tr2 = VADD(pc[i], pc[4*ido - i]);
        ti4 = VSUB(pc[2*ido + i], pc[2*ido - i]);
        tr3 = VADD(pc[2*ido + i], pc[2*ido - i]);
        ph[0] = VADD(tr2, tr3);
        cr3 = VSUB(tr2, tr3);

        ti3 = VSUB(pc[2*ido + i + 1], pc[2*ido - i + 1]);
        tr4 = VADD(pc[2*ido + i + 1], pc[2*ido - i + 1]);
        cr2 = VSUB(tr1, tr4);
        cr4 = VADD(tr1, tr4);

        ti1 = VADD(pc[i + 1], pc[4*ido - i + 1]);
        ti2 = VSUB(pc[i + 1], pc[4*ido - i + 1]);

        ph[1] = VADD(ti2, ti3); ph += l1ido;
        ci3 = VSUB(ti2, ti3);
        ci2 = VADD(ti1, ti4);
        ci4 = VSUB(ti1, ti4);
        VCPLXMUL(cr2, ci2, LD_PS1(wa1[i-2]), LD_PS1(wa1[i-1]));
        ph[0] = cr2;
        ph[1] = ci2; ph += l1ido;
        VCPLXMUL(cr3, ci3, LD_PS1(wa2[i-2]), LD_PS1(wa2[i-1]));
        ph[0] = cr3;
        ph[1] = ci3; ph += l1ido;
        VCPLXMUL(cr4, ci4, LD_PS1(wa3[i-2]), LD_PS1(wa3[i-1]));
        ph[0] = cr4;
        ph[1] = ci4; ph = ph - 3*l1ido + 2;
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k=0; k < l1ido; k+=ido) {
    int i0 = 4*k + ido;
    v4sf c = cc[i0-1], d = cc[i0 + 2*ido-1];
    v4sf a = cc[i0+0], b = cc[i0 + 2*ido+0];
    tr1 = VSUB(c,d);
    tr2 = VADD(c,d);
    ti1 = VADD(b,a);
    ti2 = VSUB(b,a);
    ch[ido-1 + k + 0*l1ido] = VADD(tr2,tr2);
    ch[ido-1 + k + 1*l1ido] = SVMUL(minus_sqrt2, VSUB(ti1, tr1));
    ch[ido-1 + k + 2*l1ido] = VADD(ti2, ti2);
    ch[ido-1 + k + 3*l1ido] = SVMUL(minus_sqrt2, VADD(ti1, tr1));
  }
} /* radb4 */

static void radf5_ps(int ido, int l1, const v4sf * RESTRICT cc, v4sf * RESTRICT ch, 
                     const float *wa1, const float *wa2, const float *wa3, const float *wa4)
{
  static const float tr11 = .309016994374947f;
  static const float ti11 = .951056516295154f;
  static const float tr12 = -.809016994374947f;
  static const float ti12 = .587785252292473f;

  /* System generated locals */
  int cc_offset, ch_offset;

  /* Local variables */
  int i, k, ic;
  v4sf ci2, di2, ci4, ci5, di3, di4, di5, ci3, cr2, cr3, dr2, dr3, dr4, dr5,
    cr5, cr4, ti2, ti3, ti5, ti4, tr2, tr3, tr4, tr5;
  int idp2;


#define cc_ref(a_1,a_2,a_3) cc[((a_3)*l1 + (a_2))*ido + a_1]
#define ch_ref(a_1,a_2,a_3) ch[((a_3)*5 + (a_2))*ido + a_1]

  /* Parameter adjustments */
  ch_offset = 1 + ido * 6;
  ch -= ch_offset;
  cc_offset = 1 + ido * (1 + l1);
  cc -= cc_offset;

  /* Function Body */
  for (k = 1; k <= l1; ++k) {
    cr2 = VADD(cc_ref(1, k, 5), cc_ref(1, k, 2));
    ci5 = VSUB(cc_ref(1, k, 5), cc_ref(1, k, 2));
    cr3 = VADD(cc_ref(1, k, 4), cc_ref(1, k, 3));
    ci4 = VSUB(cc_ref(1, k, 4), cc_ref(1, k, 3));
    ch_ref(1, 1, k) = VADD(cc_ref(1, k, 1), VADD(cr2, cr3));
    ch_ref(ido, 2, k) = VADD(cc_ref(1, k, 1), VADD(SVMUL(tr11, cr2), SVMUL(tr12, cr3)));
    ch_ref(1, 3, k) = VADD(SVMUL(ti11, ci5), SVMUL(ti12, ci4));
    ch_ref(ido, 4, k) = VADD(cc_ref(1, k, 1), VADD(SVMUL(tr12, cr2), SVMUL(tr11, cr3)));
    ch_ref(1, 5, k) = VSUB(SVMUL(ti12, ci5), SVMUL(ti11, ci4));
    /* printf("pffft: radf5, k=%d ch_ref=%f, ci4=%f\n", k, ch_ref(1, 5, k), ci4); */
  }
  if (ido == 1) {
    return;
  }
  idp2 = ido + 2;
  for (k = 1; k <= l1; ++k) {
    for (i = 3; i <= ido; i += 2) {
      ic = idp2 - i;
      dr2 = LD_PS1(wa1[i-3]); di2 = LD_PS1(wa1[i-2]);
      dr3 = LD_PS1(wa2[i-3]); di3 = LD_PS1(wa2[i-2]);
      dr4 = LD_PS1(wa3[i-3]); di4 = LD_PS1(wa3[i-2]);
      dr5 = LD_PS1(wa4[i-3]); di5 = LD_PS1(wa4[i-2]);
      VCPLXMULCONJ(dr2, di2, cc_ref(i-1, k, 2), cc_ref(i, k, 2));
      VCPLXMULCONJ(dr3, di3, cc_ref(i-1, k, 3), cc_ref(i, k, 3));
      VCPLXMULCONJ(dr4, di4, cc_ref(i-1, k, 4), cc_ref(i, k, 4));
      VCPLXMULCONJ(dr5, di5, cc_ref(i-1, k, 5), cc_ref(i, k, 5));
      cr2 = VADD(dr2, dr5);
      ci5 = VSUB(dr5, dr2);
      cr5 = VSUB(di2, di5);
      ci2 = VADD(di2, di5);
      cr3 = VADD(dr3, dr4);
      ci4 = VSUB(dr4, dr3);
      cr4 = VSUB(di3, di4);
      ci3 = VADD(di3, di4);
      ch_ref(i - 1, 1, k) = VADD(cc_ref(i - 1, k, 1), VADD(cr2, cr3));
      ch_ref(i, 1, k) = VSUB(cc_ref(i, k, 1), VADD(ci2, ci3));
      tr2 = VADD(cc_ref(i - 1, k, 1), VADD(SVMUL(tr11, cr2), SVMUL(tr12, cr3)));
      ti2 = VSUB(cc_ref(i, k, 1), VADD(SVMUL(tr11, ci2), SVMUL(tr12, ci3)));
      tr3 = VADD(cc_ref(i - 1, k, 1), VADD(SVMUL(tr12, cr2), SVMUL(tr11, cr3)));
      ti3 = VSUB(cc_ref(i, k, 1), VADD(SVMUL(tr12, ci2), SVMUL(tr11, ci3)));
      tr5 = VADD(SVMUL(ti11, cr5), SVMUL(ti12, cr4));
      ti5 = VADD(SVMUL(ti11, ci5), SVMUL(ti12, ci4));
      tr4 = VSUB(SVMUL(ti12, cr5), SVMUL(ti11, cr4));
      ti4 = VSUB(SVMUL(ti12, ci5), SVMUL(ti11, ci4));
      ch_ref(i - 1, 3, k) = VSUB(tr2, tr5);
      ch_ref(ic - 1, 2, k) = VADD(tr2, tr5);
      ch_ref(i, 3, k) = VADD(ti2, ti5);
      ch_ref(ic, 2, k) = VSUB(ti5, ti2);
      ch_ref(i - 1, 5, k) = VSUB(tr3, tr4);
      ch_ref(ic - 1, 4, k) = VADD(tr3, tr4);
      ch_ref(i, 5, k) = VADD(ti3, ti4);
      ch_ref(ic, 4, k) = VSUB(ti4, ti3);
    }
  }
#undef cc_ref
#undef ch_ref
} /* radf5 */

static void radb5_ps(int ido, int l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch, 
                  const float *wa1, const float *wa2, const float *wa3, const float *wa4)
{
  static const float tr11 = .309016994374947f;
  static const float ti11 = .951056516295154f;
  static const float tr12 = -.809016994374947f;
  static const float ti12 = .587785252292473f;

  int cc_offset, ch_offset;

  /* Local variables */
  int i, k, ic;
  v4sf ci2, ci3, ci4, ci5, di3, di4, di5, di2, cr2, cr3, cr5, cr4, ti2, ti3,
    ti4, ti5, dr3, dr4, dr5, dr2, tr2, tr3, tr4, tr5;
  int idp2;

#define cc_ref(a_1,a_2,a_3) cc[((a_3)*5 + (a_2))*ido + a_1]
#define ch_ref(a_1,a_2,a_3) ch[((a_3)*l1 + (a_2))*ido + a_1]

  /* Parameter adjustments */
  ch_offset = 1 + ido * (1 + l1);
  ch -= ch_offset;
  cc_offset = 1 + ido * 6;
  cc -= cc_offset;

  /* Function Body */
  for (k = 1; k <= l1; ++k) {
    ti5 = VADD(cc_ref(1, 3, k), cc_ref(1, 3, k));
    ti4 = VADD(cc_ref(1, 5, k), cc_ref(1, 5, k));
    tr2 = VADD(cc_ref(ido, 2, k), cc_ref(ido, 2, k));
    tr3 = VADD(cc_ref(ido, 4, k), cc_ref(ido, 4, k));
    ch_ref(1, k, 1) = VADD(cc_ref(1, 1, k), VADD(tr2, tr3));
    cr2 = VADD(cc_ref(1, 1, k), VADD(SVMUL(tr11, tr2), SVMUL(tr12, tr3)));
    cr3 = VADD(cc_ref(1, 1, k), VADD(SVMUL(tr12, tr2), SVMUL(tr11, tr3)));
    ci5 = VADD(SVMUL(ti11, ti5), SVMUL(ti12, ti4));
    ci4 = VSUB(SVMUL(ti12, ti5), SVMUL(ti11, ti4));
    ch_ref(1, k, 2) = VSUB(cr2, ci5);
    ch_ref(1, k, 3) = VSUB(cr3, ci4);
    ch_ref(1, k, 4) = VADD(cr3, ci4);
    ch_ref(1, k, 5) = VADD(cr2, ci5);
  }
  if (ido == 1) {
    return;
  }
  idp2 = ido + 2;
  for (k = 1; k <= l1; ++k) {
    for (i = 3; i <= ido; i += 2) {
      ic = idp2 - i;
      ti5 = VADD(cc_ref(i  , 3, k), cc_ref(ic  , 2, k));
      ti2 = VSUB(cc_ref(i  , 3, k), cc_ref(ic  , 2, k));
      ti4 = VADD(cc_ref(i  , 5, k), cc_ref(ic  , 4, k));
      ti3 = VSUB(cc_ref(i  , 5, k), cc_ref(ic  , 4, k));
      tr5 = VSUB(cc_ref(i-1, 3, k), cc_ref(ic-1, 2, k));
      tr2 = VADD(cc_ref(i-1, 3, k), cc_ref(ic-1, 2, k));
      tr4 = VSUB(cc_ref(i-1, 5, k), cc_ref(ic-1, 4, k));
      tr3 = VADD(cc_ref(i-1, 5, k), cc_ref(ic-1, 4, k));
      ch_ref(i - 1, k, 1) = VADD(cc_ref(i-1, 1, k), VADD(tr2, tr3));
      ch_ref(i, k, 1) = VADD(cc_ref(i, 1, k), VADD(ti2, ti3));
      cr2 = VADD(cc_ref(i-1, 1, k), VADD(SVMUL(tr11, tr2), SVMUL(tr12, tr3)));
      ci2 = VADD(cc_ref(i  , 1, k), VADD(SVMUL(tr11, ti2), SVMUL(tr12, ti3)));
      cr3 = VADD(cc_ref(i-1, 1, k), VADD(SVMUL(tr12, tr2), SVMUL(tr11, tr3)));
      ci3 = VADD(cc_ref(i  , 1, k), VADD(SVMUL(tr12, ti2), SVMUL(tr11, ti3)));
      cr5 = VADD(SVMUL(ti11, tr5), SVMUL(ti12, tr4));
      ci5 = VADD(SVMUL(ti11, ti5), SVMUL(ti12, ti4));
      cr4 = VSUB(SVMUL(ti12, tr5), SVMUL(ti11, tr4));
      ci4 = VSUB(SVMUL(ti12, ti5), SVMUL(ti11, ti4));
      dr3 = VSUB(cr3, ci4);
      dr4 = VADD(cr3, ci4);
      di3 = VADD(ci3, cr4);
      di4 = VSUB(ci3, cr4);
      dr5 = VADD(cr2, ci5);
      dr2 = VSUB(cr2, ci5);
      di5 = VSUB(ci2, cr5);
      di2 = VADD(ci2, cr5);
      VCPLXMUL(dr2, di2, LD_PS1(wa1[i-3]), LD_PS1(wa1[i-2]));
      VCPLXMUL(dr3, di3, LD_PS1(wa2[i-3]), LD_PS1(wa2[i-2]));
      VCPLXMUL(dr4, di4, LD_PS1(wa3[i-3]), LD_PS1(wa3[i-2]));
      VCPLXMUL(dr5, di5, LD_PS1(wa4[i-3]), LD_PS1(wa4[i-2]));

      ch_ref(i-1, k, 2) = dr2; ch_ref(i, k, 2) = di2;
      ch_ref(i-1, k, 3) = dr3; ch_ref(i, k, 3) = di3;
      ch_ref(i-1, k, 4) = dr4; ch_ref(i, k, 4) = di4;
      ch_ref(i-1, k, 5) = dr5; ch_ref(i, k, 5) = di5;
    }
  }
#undef cc_ref
#undef ch_ref
} /* radb5 */

static NEVER_INLINE(v4sf *) rfftf1_ps(int n, const v4sf *input_readonly, v4sf *work1, v4sf *work2, 
                                      const float *wa, const int *ifac) {  
  v4sf *in  = (v4sf*)input_readonly;
  v4sf *out = (in == work2 ? work1 : work2);
  int nf = ifac[1], k1;
  int l2 = n;
  int iw = n-1;
  assert(in != out && work1 != work2);
  for (k1 = 1; k1 <= nf; ++k1) {
    int kh = nf - k1;
    int ip = ifac[kh + 2];
    int l1 = l2 / ip;
    int ido = n / l2;
    iw -= (ip - 1)*ido;
    switch (ip) {
      case 5: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        int ix4 = ix3 + ido;
        radf5_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4]);
      } break;
      case 4: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        radf4_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3]);
      } break;
      case 3: {
        int ix2 = iw + ido;
        radf3_ps(ido, l1, in, out, &wa[iw], &wa[ix2]);
      } break;
      case 2:
        radf2_ps(ido, l1, in, out, &wa[iw]);
        break;
      default:
        assert(0);
        break;
    }
    l2 = l1;
    if (out == work2) {
      out = work1; in = work2;
    } else {
      out = work2; in = work1;
    }
  }
  return in; /* this is in fact the output .. */
} /* rfftf1 */

static NEVER_INLINE(v4sf *) rfftb1_ps(int n, const v4sf *input_readonly, v4sf *work1, v4sf *work2, 
                                      const float *wa, const int *ifac) {  
  v4sf *in  = (v4sf*)input_readonly;
  v4sf *out = (in == work2 ? work1 : work2);
  int nf = ifac[1], k1;
  int l1 = 1;
  int iw = 0;
  assert(in != out);
  for (k1=1; k1<=nf; k1++) {
    int ip = ifac[k1 + 1];
    int l2 = ip*l1;
    int ido = n / l2;
    switch (ip) {
      case 5: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        int ix4 = ix3 + ido;
        radb5_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4]);
      } break;
      case 4: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        radb4_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3]);
      } break;
      case 3: {
        int ix2 = iw + ido;
        radb3_ps(ido, l1, in, out, &wa[iw], &wa[ix2]);
      } break;
      case 2:
        radb2_ps(ido, l1, in, out, &wa[iw]);
        break;
      default:
        assert(0);
        break;
    }
    l1 = l2;
    iw += (ip - 1)*ido;

    if (out == work2) {
      out = work1; in = work2;
    } else {
      out = work2; in = work1;
    }
  }
  return in; /* this is in fact the output .. */
}

static int decompose(int n, int *ifac, const int *ntryh) {
  int nl = n, nf = 0, i, j = 0;
  for (j=0; ntryh[j]; ++j) {
    int ntry = ntryh[j];
    while (nl != 1) {
      int nq = nl / ntry;
      int nr = nl - ntry * nq;
      if (nr == 0) {
        ifac[2+nf++] = ntry;
        nl = nq;
        if (ntry == 2 && nf != 1) {
          for (i = 2; i <= nf; ++i) {
            int ib = nf - i + 2;
            ifac[ib + 1] = ifac[ib];
          }
          ifac[2] = 2;
        }
      } else break;
    }
  }
  ifac[0] = n;
  ifac[1] = nf;  
  return nf;
}



static void rffti1_ps(int n, float *wa, int *ifac)
{
  static const int ntryh[] = { 4,2,3,5,0 };
  int k1, j, ii;

  int nf = decompose(n,ifac,ntryh);
  float argh = (2*(float)M_PI) / n;
  int is = 0;
  int nfm1 = nf - 1;
  int l1 = 1;
  for (k1 = 1; k1 <= nfm1; k1++) {
    int ip = ifac[k1 + 1];
    int ld = 0;
    int l2 = l1*ip;
    int ido = n / l2;
    int ipm = ip - 1;
    for (j = 1; j <= ipm; ++j) {
      float argld;
      int i = is, fi=0;
      ld += l1;
      argld = ld*argh;
      for (ii = 3; ii <= ido; ii += 2) {
        i += 2;
        fi += 1;
        wa[i - 2] = FUNC_COS(fi*argld);
        wa[i - 1] = FUNC_SIN(fi*argld);
      }
      is += ido;
    }
    l1 = l2;
  }
} /* rffti1 */

static void cffti1_ps(int n, float *wa, int *ifac)
{
  static const int ntryh[] = { 5,3,4,2,0 };
  int k1, j, ii;

  int nf = decompose(n,ifac,ntryh);
  float argh = (2*(float)M_PI) / n;
  int i = 1;
  int l1 = 1;
  for (k1=1; k1<=nf; k1++) {
    int ip = ifac[k1+1];
    int ld = 0;
    int l2 = l1*ip;
    int ido = n / l2;
    int idot = ido + ido + 2;
    int ipm = ip - 1;
    for (j=1; j<=ipm; j++) {
      float argld;
      int i1 = i, fi = 0;
      wa[i-1] = 1;
      wa[i] = 0;
      ld += l1;
      argld = ld*argh;
      for (ii = 4; ii <= idot; ii += 2) {
        i += 2;
        fi += 1;
        wa[i-1] = FUNC_COS(fi*argld);
        wa[i] = FUNC_SIN(fi*argld);
      }
      if (ip > 5) {
        wa[i1-1] = wa[i-1];
        wa[i1] = wa[i];
      }
    }
    l1 = l2;
  }
} /* cffti1 */


static v4sf *cfftf1_ps(int n, const v4sf *input_readonly, v4sf *work1, v4sf *work2, const float *wa, const int *ifac, int isign) {
  v4sf *in  = (v4sf*)input_readonly;
  v4sf *out = (in == work2 ? work1 : work2); 
  int nf = ifac[1], k1;
  int l1 = 1;
  int iw = 0;
  assert(in != out && work1 != work2);
  for (k1=2; k1<=nf+1; k1++) {
    int ip = ifac[k1];
    int l2 = ip*l1;
    int ido = n / l2;
    int idot = ido + ido;
    switch (ip) {
      case 5: {
        int ix2 = iw + idot;
        int ix3 = ix2 + idot;
        int ix4 = ix3 + idot;
        passf5_ps(idot, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4], isign);
      } break;
      case 4: {
        int ix2 = iw + idot;
        int ix3 = ix2 + idot;
        passf4_ps(idot, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], isign);
      } break;
      case 2: {
        passf2_ps(idot, l1, in, out, &wa[iw], isign);
      } break;
      case 3: {
        int ix2 = iw + idot;
        passf3_ps(idot, l1, in, out, &wa[iw], &wa[ix2], isign);
      } break;
      default:
        assert(0);
    }
    l1 = l2;
    iw += (ip - 1)*idot;
    if (out == work2) {
      out = work1; in = work2;
    } else {
      out = work2; in = work1;
    }
  }

  return in; /* this is in fact the output .. */
}


struct SETUP_STRUCT {
  int     N;
  int     Ncvec;  /* nb of complex simd vectors (N/4 if PFFFT_COMPLEX, N/8 if PFFFT_REAL) */
  int ifac[15];
  pffft_transform_t transform;
  v4sf *data;     /* allocated room for twiddle coefs */
  float *e;       /* points into 'data', N/4*3 elements */
  float *twiddle; /* points into 'data', N/4 elements */
};

SETUP_STRUCT *FUNC_NEW_SETUP(int N, pffft_transform_t transform) {
  SETUP_STRUCT *s = 0;
  int k, m;
  /* unfortunately, the fft size must be a multiple of 16 for complex FFTs 
     and 32 for real FFTs -- a lot of stuff would need to be rewritten to
     handle other cases (or maybe just switch to a scalar fft, I don't know..) */
  if (transform == PFFFT_REAL)    { if ((N%(2*SIMD_SZ*SIMD_SZ)) || N<=0) return s; }
  if (transform == PFFFT_COMPLEX) { if ((N%(  SIMD_SZ*SIMD_SZ)) || N<=0) return s; }
  s = (SETUP_STRUCT*)malloc(sizeof(SETUP_STRUCT));
  /* assert((N % 32) == 0); */
  s->N = N;
  s->transform = transform;  
  /* nb of complex simd vectors */
  s->Ncvec = (transform == PFFFT_REAL ? N/2 : N)/SIMD_SZ;
  s->data = (v4sf*)FUNC_ALIGNED_MALLOC(2*s->Ncvec * sizeof(v4sf));
  s->e = (float*)s->data;
  s->twiddle = (float*)(s->data + (2*s->Ncvec*(SIMD_SZ-1))/SIMD_SZ);  

  if (transform == PFFFT_REAL) {
    for (k=0; k < s->Ncvec; ++k) {
      int i = k/SIMD_SZ;
      int j = k%SIMD_SZ;
      for (m=0; m < SIMD_SZ-1; ++m) {
        float A = -2*(float)M_PI*(m+1)*k / N;
        s->e[(2*(i*3 + m) + 0) * SIMD_SZ + j] = FUNC_COS(A);
        s->e[(2*(i*3 + m) + 1) * SIMD_SZ + j] = FUNC_SIN(A);
      }
    }
    rffti1_ps(N/SIMD_SZ, s->twiddle, s->ifac);
  } else {
    for (k=0; k < s->Ncvec; ++k) {
      int i = k/SIMD_SZ;
      int j = k%SIMD_SZ;
      for (m=0; m < SIMD_SZ-1; ++m) {
        float A = -2*(float)M_PI*(m+1)*k / N;
        s->e[(2*(i*3 + m) + 0)*SIMD_SZ + j] = FUNC_COS(A);
        s->e[(2*(i*3 + m) + 1)*SIMD_SZ + j] = FUNC_SIN(A);
      }
    }
    cffti1_ps(N/SIMD_SZ, s->twiddle, s->ifac);
  }

  /* check that N is decomposable with allowed prime factors */
  for (k=0, m=1; k < s->ifac[1]; ++k) { m *= s->ifac[2+k]; }
  if (m != N/SIMD_SZ) {
    FUNC_DESTROY(s); s = 0;
  }

  return s;
}


void FUNC_DESTROY(SETUP_STRUCT *s) {
  if (!s)
    return;
  FUNC_ALIGNED_FREE(s->data);
  free(s);
}

#if ( SIMD_SZ == 4 )    /* !defined(PFFFT_SIMD_DISABLE) */

/* [0 0 1 2 3 4 5 6 7 8] -> [0 8 7 6 5 4 3 2 1] */
static void reversed_copy(int N, const v4sf *in, int in_stride, v4sf *out) {
  v4sf g0, g1;
  int k;
  INTERLEAVE2(in[0], in[1], g0, g1); in += in_stride;
  
  *--out = VSWAPHL(g0, g1); /* [g0l, g0h], [g1l g1h] -> [g1l, g0h] */
  for (k=1; k < N; ++k) {
    v4sf h0, h1;
    INTERLEAVE2(in[0], in[1], h0, h1); in += in_stride;
    *--out = VSWAPHL(g1, h0);
    *--out = VSWAPHL(h0, h1);
    g1 = h1;
  }
  *--out = VSWAPHL(g1, g0);
}

static void unreversed_copy(int N, const v4sf *in, v4sf *out, int out_stride) {
  v4sf g0, g1, h0, h1;
  int k;
  g0 = g1 = in[0]; ++in;
  for (k=1; k < N; ++k) {
    h0 = *in++; h1 = *in++;
    g1 = VSWAPHL(g1, h0);
    h0 = VSWAPHL(h0, h1);
    UNINTERLEAVE2(h0, g1, out[0], out[1]); out += out_stride;
    g1 = h1;
  }
  h0 = *in++; h1 = g0;
  g1 = VSWAPHL(g1, h0);
  h0 = VSWAPHL(h0, h1);
  UNINTERLEAVE2(h0, g1, out[0], out[1]);
}

void FUNC_ZREORDER(SETUP_STRUCT *setup, const float *in, float *out, pffft_direction_t direction) {
  int k, N = setup->N, Ncvec = setup->Ncvec;
  const v4sf *vin = (const v4sf*)in;
  v4sf *vout = (v4sf*)out;
  assert(in != out);
  if (setup->transform == PFFFT_REAL) {
    int k, dk = N/32;
    if (direction == PFFFT_FORWARD) {
      for (k=0; k < dk; ++k) {
        INTERLEAVE2(vin[k*8 + 0], vin[k*8 + 1], vout[2*(0*dk + k) + 0], vout[2*(0*dk + k) + 1]);
        INTERLEAVE2(vin[k*8 + 4], vin[k*8 + 5], vout[2*(2*dk + k) + 0], vout[2*(2*dk + k) + 1]);
      }
      reversed_copy(dk, vin+2, 8, (v4sf*)(out + N/2));
      reversed_copy(dk, vin+6, 8, (v4sf*)(out + N));
    } else {
      for (k=0; k < dk; ++k) {
        UNINTERLEAVE2(vin[2*(0*dk + k) + 0], vin[2*(0*dk + k) + 1], vout[k*8 + 0], vout[k*8 + 1]);
        UNINTERLEAVE2(vin[2*(2*dk + k) + 0], vin[2*(2*dk + k) + 1], vout[k*8 + 4], vout[k*8 + 5]);
      }
      unreversed_copy(dk, (v4sf*)(in + N/4), (v4sf*)(out + N - 6*SIMD_SZ), -8);
      unreversed_copy(dk, (v4sf*)(in + 3*N/4), (v4sf*)(out + N - 2*SIMD_SZ), -8);
    }
  } else {
    if (direction == PFFFT_FORWARD) {
      for (k=0; k < Ncvec; ++k) { 
        int kk = (k/4) + (k%4)*(Ncvec/4);
        INTERLEAVE2(vin[k*2], vin[k*2+1], vout[kk*2], vout[kk*2+1]);
      }
    } else {
      for (k=0; k < Ncvec; ++k) { 
        int kk = (k/4) + (k%4)*(Ncvec/4);
        UNINTERLEAVE2(vin[kk*2], vin[kk*2+1], vout[k*2], vout[k*2+1]);
      }
    }
  }
}

void FUNC_CPLX_FINALIZE(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/SIMD_SZ; /* number of 4x4 matrix blocks */
  v4sf r0, i0, r1, i1, r2, i2, r3, i3;
  v4sf sr0, dr0, sr1, dr1, si0, di0, si1, di1;
  assert(in != out);
  for (k=0; k < dk; ++k) {    
    r0 = in[8*k+0]; i0 = in[8*k+1];
    r1 = in[8*k+2]; i1 = in[8*k+3];
    r2 = in[8*k+4]; i2 = in[8*k+5];
    r3 = in[8*k+6]; i3 = in[8*k+7];
    VTRANSPOSE4(r0,r1,r2,r3);
    VTRANSPOSE4(i0,i1,i2,i3);
    VCPLXMUL(r1,i1,e[k*6+0],e[k*6+1]);
    VCPLXMUL(r2,i2,e[k*6+2],e[k*6+3]);
    VCPLXMUL(r3,i3,e[k*6+4],e[k*6+5]);

    sr0 = VADD(r0,r2); dr0 = VSUB(r0, r2);
    sr1 = VADD(r1,r3); dr1 = VSUB(r1, r3);
    si0 = VADD(i0,i2); di0 = VSUB(i0, i2);
    si1 = VADD(i1,i3); di1 = VSUB(i1, i3);

    /*
      transformation for each column is:
      
      [1   1   1   1   0   0   0   0]   [r0]
      [1   0  -1   0   0  -1   0   1]   [r1]
      [1  -1   1  -1   0   0   0   0]   [r2]
      [1   0  -1   0   0   1   0  -1]   [r3]
      [0   0   0   0   1   1   1   1] * [i0]
      [0   1   0  -1   1   0  -1   0]   [i1]
      [0   0   0   0   1  -1   1  -1]   [i2]
      [0  -1   0   1   1   0  -1   0]   [i3]    
    */
    
    r0 = VADD(sr0, sr1); i0 = VADD(si0, si1);
    r1 = VADD(dr0, di1); i1 = VSUB(di0, dr1);
    r2 = VSUB(sr0, sr1); i2 = VSUB(si0, si1);
    r3 = VSUB(dr0, di1); i3 = VADD(di0, dr1);
  
    *out++ = r0; *out++ = i0; *out++ = r1; *out++ = i1;
    *out++ = r2; *out++ = i2; *out++ = r3; *out++ = i3;
  }
}

void FUNC_CPLX_PREPROCESS(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/SIMD_SZ; /* number of 4x4 matrix blocks */
  v4sf r0, i0, r1, i1, r2, i2, r3, i3;
  v4sf sr0, dr0, sr1, dr1, si0, di0, si1, di1;
  assert(in != out);
  for (k=0; k < dk; ++k) {    
    r0 = in[8*k+0]; i0 = in[8*k+1];
    r1 = in[8*k+2]; i1 = in[8*k+3];
    r2 = in[8*k+4]; i2 = in[8*k+5];
    r3 = in[8*k+6]; i3 = in[8*k+7];

    sr0 = VADD(r0,r2); dr0 = VSUB(r0, r2);
    sr1 = VADD(r1,r3); dr1 = VSUB(r1, r3);
    si0 = VADD(i0,i2); di0 = VSUB(i0, i2);
    si1 = VADD(i1,i3); di1 = VSUB(i1, i3);

    r0 = VADD(sr0, sr1); i0 = VADD(si0, si1);
    r1 = VSUB(dr0, di1); i1 = VADD(di0, dr1);
    r2 = VSUB(sr0, sr1); i2 = VSUB(si0, si1);
    r3 = VADD(dr0, di1); i3 = VSUB(di0, dr1);

    VCPLXMULCONJ(r1,i1,e[k*6+0],e[k*6+1]);
    VCPLXMULCONJ(r2,i2,e[k*6+2],e[k*6+3]);
    VCPLXMULCONJ(r3,i3,e[k*6+4],e[k*6+5]);

    VTRANSPOSE4(r0,r1,r2,r3);
    VTRANSPOSE4(i0,i1,i2,i3);

    *out++ = r0; *out++ = i0; *out++ = r1; *out++ = i1;
    *out++ = r2; *out++ = i2; *out++ = r3; *out++ = i3;
  }
}


static ALWAYS_INLINE(void) FUNC_REAL_FINALIZE_4X4(const v4sf *in0, const v4sf *in1, const v4sf *in,
                            const v4sf *e, v4sf *out) {
  v4sf r0, i0, r1, i1, r2, i2, r3, i3;
  v4sf sr0, dr0, sr1, dr1, si0, di0, si1, di1;
  r0 = *in0; i0 = *in1;
  r1 = *in++; i1 = *in++; r2 = *in++; i2 = *in++; r3 = *in++; i3 = *in++;
  VTRANSPOSE4(r0,r1,r2,r3);
  VTRANSPOSE4(i0,i1,i2,i3);
 
  /*
    transformation for each column is:

    [1   1   1   1   0   0   0   0]   [r0]
    [1   0  -1   0   0  -1   0   1]   [r1]
    [1   0  -1   0   0   1   0  -1]   [r2]
    [1  -1   1  -1   0   0   0   0]   [r3]
    [0   0   0   0   1   1   1   1] * [i0]
    [0  -1   0   1  -1   0   1   0]   [i1]
    [0  -1   0   1   1   0  -1   0]   [i2]
    [0   0   0   0  -1   1  -1   1]   [i3]    
  */
  
  /* cerr << "matrix initial, before e , REAL:\n 1: " << r0 << "\n 1: " << r1 << "\n 1: " << r2 << "\n 1: " << r3 << "\n"; */
  /* cerr << "matrix initial, before e, IMAG :\n 1: " << i0 << "\n 1: " << i1 << "\n 1: " << i2 << "\n 1: " << i3 << "\n"; */

  VCPLXMUL(r1,i1,e[0],e[1]);
  VCPLXMUL(r2,i2,e[2],e[3]);
  VCPLXMUL(r3,i3,e[4],e[5]);

  /* cerr << "matrix initial, real part:\n 1: " << r0 << "\n 1: " << r1 << "\n 1: " << r2 << "\n 1: " << r3 << "\n"; */
  /* cerr << "matrix initial, imag part:\n 1: " << i0 << "\n 1: " << i1 << "\n 1: " << i2 << "\n 1: " << i3 << "\n"; */

  sr0 = VADD(r0,r2); dr0 = VSUB(r0,r2); 
  sr1 = VADD(r1,r3); dr1 = VSUB(r3,r1);
  si0 = VADD(i0,i2); di0 = VSUB(i0,i2); 
  si1 = VADD(i1,i3); di1 = VSUB(i3,i1);

  r0 = VADD(sr0, sr1);
  r3 = VSUB(sr0, sr1);
  i0 = VADD(si0, si1);
  i3 = VSUB(si1, si0);
  r1 = VADD(dr0, di1);
  r2 = VSUB(dr0, di1);
  i1 = VSUB(dr1, di0);
  i2 = VADD(dr1, di0);

  *out++ = r0;
  *out++ = i0;
  *out++ = r1;
  *out++ = i1;
  *out++ = r2;
  *out++ = i2;
  *out++ = r3;
  *out++ = i3;

}

static NEVER_INLINE(void) FUNC_REAL_FINALIZE(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/SIMD_SZ; /* number of 4x4 matrix blocks */
  /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

  v4sf_union cr, ci, *uout = (v4sf_union*)out;
  v4sf save = in[7], zero=VZERO();
  float xr0, xi0, xr1, xi1, xr2, xi2, xr3, xi3;
  static const float s = (float)M_SQRT2/2;

  cr.v = in[0]; ci.v = in[Ncvec*2-1];
  assert(in != out);
  FUNC_REAL_FINALIZE_4X4(&zero, &zero, in+1, e, out);

  /*
    [cr0 cr1 cr2 cr3 ci0 ci1 ci2 ci3]

    [Xr(1)]  ] [1   1   1   1   0   0   0   0]
    [Xr(N/4) ] [0   0   0   0   1   s   0  -s]
    [Xr(N/2) ] [1   0  -1   0   0   0   0   0]
    [Xr(3N/4)] [0   0   0   0   1  -s   0   s]
    [Xi(1)   ] [1  -1   1  -1   0   0   0   0]
    [Xi(N/4) ] [0   0   0   0   0  -s  -1  -s]
    [Xi(N/2) ] [0  -1   0   1   0   0   0   0]
    [Xi(3N/4)] [0   0   0   0   0  -s   1  -s]
  */

  xr0=(cr.f[0]+cr.f[2]) + (cr.f[1]+cr.f[3]); uout[0].f[0] = xr0;
  xi0=(cr.f[0]+cr.f[2]) - (cr.f[1]+cr.f[3]); uout[1].f[0] = xi0;
  xr2=(cr.f[0]-cr.f[2]);                     uout[4].f[0] = xr2;
  xi2=(cr.f[3]-cr.f[1]);                     uout[5].f[0] = xi2;
  xr1= ci.f[0] + s*(ci.f[1]-ci.f[3]);        uout[2].f[0] = xr1;
  xi1=-ci.f[2] - s*(ci.f[1]+ci.f[3]);        uout[3].f[0] = xi1;
  xr3= ci.f[0] - s*(ci.f[1]-ci.f[3]);        uout[6].f[0] = xr3;
  xi3= ci.f[2] - s*(ci.f[1]+ci.f[3]);        uout[7].f[0] = xi3; 

  for (k=1; k < dk; ++k) {
    v4sf save_next = in[8*k+7];
    FUNC_REAL_FINALIZE_4X4(&save, &in[8*k+0], in + 8*k+1,
                           e + k*6, out + k*8);
    save = save_next;
  }

}

static ALWAYS_INLINE(void) FUNC_REAL_PREPROCESS_4X4(const v4sf *in, 
                                             const v4sf *e, v4sf *out, int first) {
  v4sf r0=in[0], i0=in[1], r1=in[2], i1=in[3], r2=in[4], i2=in[5], r3=in[6], i3=in[7];
  /*
    transformation for each column is:

    [1   1   1   1   0   0   0   0]   [r0]
    [1   0   0  -1   0  -1  -1   0]   [r1]
    [1  -1  -1   1   0   0   0   0]   [r2]
    [1   0   0  -1   0   1   1   0]   [r3]
    [0   0   0   0   1  -1   1  -1] * [i0]
    [0  -1   1   0   1   0   0   1]   [i1]
    [0   0   0   0   1   1  -1  -1]   [i2]
    [0   1  -1   0   1   0   0   1]   [i3]    
  */

  v4sf sr0 = VADD(r0,r3), dr0 = VSUB(r0,r3); 
  v4sf sr1 = VADD(r1,r2), dr1 = VSUB(r1,r2);
  v4sf si0 = VADD(i0,i3), di0 = VSUB(i0,i3); 
  v4sf si1 = VADD(i1,i2), di1 = VSUB(i1,i2);

  r0 = VADD(sr0, sr1);
  r2 = VSUB(sr0, sr1);
  r1 = VSUB(dr0, si1);
  r3 = VADD(dr0, si1);
  i0 = VSUB(di0, di1);
  i2 = VADD(di0, di1);
  i1 = VSUB(si0, dr1);
  i3 = VADD(si0, dr1);

  VCPLXMULCONJ(r1,i1,e[0],e[1]);
  VCPLXMULCONJ(r2,i2,e[2],e[3]);
  VCPLXMULCONJ(r3,i3,e[4],e[5]);

  VTRANSPOSE4(r0,r1,r2,r3);
  VTRANSPOSE4(i0,i1,i2,i3);

  if (!first) {
    *out++ = r0;
    *out++ = i0;
  }
  *out++ = r1;
  *out++ = i1;
  *out++ = r2;
  *out++ = i2;
  *out++ = r3;
  *out++ = i3;
}

static NEVER_INLINE(void) FUNC_REAL_PREPROCESS(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/SIMD_SZ; /* number of 4x4 matrix blocks */
  /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

  v4sf_union Xr, Xi, *uout = (v4sf_union*)out;
  float cr0, ci0, cr1, ci1, cr2, ci2, cr3, ci3;
  static const float s = (float)M_SQRT2;
  assert(in != out);
  for (k=0; k < 4; ++k) {
    Xr.f[k] = ((float*)in)[8*k];
    Xi.f[k] = ((float*)in)[8*k+4];
  }

  FUNC_REAL_PREPROCESS_4X4(in, e, out+1, 1); /* will write only 6 values */

  /*
    [Xr0 Xr1 Xr2 Xr3 Xi0 Xi1 Xi2 Xi3]

    [cr0] [1   0   2   0   1   0   0   0]
    [cr1] [1   0   0   0  -1   0  -2   0]
    [cr2] [1   0  -2   0   1   0   0   0]
    [cr3] [1   0   0   0  -1   0   2   0]
    [ci0] [0   2   0   2   0   0   0   0]
    [ci1] [0   s   0  -s   0  -s   0  -s]
    [ci2] [0   0   0   0   0  -2   0   2]
    [ci3] [0  -s   0   s   0  -s   0  -s]
  */
  for (k=1; k < dk; ++k) {    
    FUNC_REAL_PREPROCESS_4X4(in+8*k, e + k*6, out-1+k*8, 0);
  }

  cr0=(Xr.f[0]+Xi.f[0]) + 2*Xr.f[2]; uout[0].f[0] = cr0;
  cr1=(Xr.f[0]-Xi.f[0]) - 2*Xi.f[2]; uout[0].f[1] = cr1;
  cr2=(Xr.f[0]+Xi.f[0]) - 2*Xr.f[2]; uout[0].f[2] = cr2;
  cr3=(Xr.f[0]-Xi.f[0]) + 2*Xi.f[2]; uout[0].f[3] = cr3;
  ci0= 2*(Xr.f[1]+Xr.f[3]);                       uout[2*Ncvec-1].f[0] = ci0;
  ci1= s*(Xr.f[1]-Xr.f[3]) - s*(Xi.f[1]+Xi.f[3]); uout[2*Ncvec-1].f[1] = ci1;
  ci2= 2*(Xi.f[3]-Xi.f[1]);                       uout[2*Ncvec-1].f[2] = ci2;
  ci3=-s*(Xr.f[1]-Xr.f[3]) - s*(Xi.f[1]+Xi.f[3]); uout[2*Ncvec-1].f[3] = ci3;
}


void FUNC_TRANSFORM_INTERNAL(SETUP_STRUCT *setup, const float *finput, float *foutput, v4sf *scratch,
                             pffft_direction_t direction, int ordered) {
  int k, Ncvec   = setup->Ncvec;
  int nf_odd = (setup->ifac[1] & 1);

  /* temporary buffer is allocated on the stack if the scratch pointer is NULL */
  int stack_allocate = (scratch == 0 ? Ncvec*2 : 1);
  VLA_ARRAY_ON_STACK(v4sf, scratch_on_stack, stack_allocate);

  const v4sf *vinput = (const v4sf*)finput;
  v4sf *voutput      = (v4sf*)foutput;
  v4sf *buff[2]      = { voutput, scratch ? scratch : scratch_on_stack };
  int ib = (nf_odd ^ ordered ? 1 : 0);

  assert(VALIGNED(finput) && VALIGNED(foutput));

  /* assert(finput != foutput); */
  if (direction == PFFFT_FORWARD) {
    ib = !ib;
    if (setup->transform == PFFFT_REAL) { 
      ib = (rfftf1_ps(Ncvec*2, vinput, buff[ib], buff[!ib],
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);      
      FUNC_REAL_FINALIZE(Ncvec, buff[ib], buff[!ib], (v4sf*)setup->e);
    } else {
      v4sf *tmp = buff[ib];
      for (k=0; k < Ncvec; ++k) {
        UNINTERLEAVE2(vinput[k*2], vinput[k*2+1], tmp[k*2], tmp[k*2+1]);
      }
      ib = (cfftf1_ps(Ncvec, buff[ib], buff[!ib], buff[ib], 
                      setup->twiddle, &setup->ifac[0], -1) == buff[0] ? 0 : 1);
      FUNC_CPLX_FINALIZE(Ncvec, buff[ib], buff[!ib], (v4sf*)setup->e);
    }
    if (ordered) {
      FUNC_ZREORDER(setup, (float*)buff[!ib], (float*)buff[ib], PFFFT_FORWARD);
    } else ib = !ib;
  } else {
    if (vinput == buff[ib]) { 
      ib = !ib; /* may happen when finput == foutput */
    }
    if (ordered) {
      FUNC_ZREORDER(setup, (float*)vinput, (float*)buff[ib], PFFFT_BACKWARD);
      vinput = buff[ib]; ib = !ib;
    }
    if (setup->transform == PFFFT_REAL) {
      FUNC_REAL_PREPROCESS(Ncvec, vinput, buff[ib], (v4sf*)setup->e);
      ib = (rfftb1_ps(Ncvec*2, buff[ib], buff[0], buff[1], 
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);
    } else {
      FUNC_CPLX_PREPROCESS(Ncvec, vinput, buff[ib], (v4sf*)setup->e);
      ib = (cfftf1_ps(Ncvec, buff[ib], buff[0], buff[1], 
                      setup->twiddle, &setup->ifac[0], +1) == buff[0] ? 0 : 1);
      for (k=0; k < Ncvec; ++k) {
        INTERLEAVE2(buff[ib][k*2], buff[ib][k*2+1], buff[ib][k*2], buff[ib][k*2+1]);
      }
    }
  }
  
  if (buff[ib] != voutput) {
    /* extra copy required -- this situation should only happen when finput == foutput */
    assert(finput==foutput);
    for (k=0; k < Ncvec; ++k) {
      v4sf a = buff[ib][2*k], b = buff[ib][2*k+1];
      voutput[2*k] = a; voutput[2*k+1] = b;
    }
    ib = !ib;
  }
  assert(buff[ib] == voutput);
}

void FUNC_ZCONVOLVE_ACCUMULATE(SETUP_STRUCT *s, const float *a, const float *b, float *ab, float scaling) {
  int Ncvec = s->Ncvec;
  const v4sf * RESTRICT va = (const v4sf*)a;
  const v4sf * RESTRICT vb = (const v4sf*)b;
  v4sf * RESTRICT vab = (v4sf*)ab;

#ifdef __arm__
  __builtin_prefetch(va);
  __builtin_prefetch(vb);
  __builtin_prefetch(vab);
  __builtin_prefetch(va+2);
  __builtin_prefetch(vb+2);
  __builtin_prefetch(vab+2);
  __builtin_prefetch(va+4);
  __builtin_prefetch(vb+4);
  __builtin_prefetch(vab+4);
  __builtin_prefetch(va+6);
  __builtin_prefetch(vb+6);
  __builtin_prefetch(vab+6);
# ifndef __clang__
#   define ZCONVOLVE_USING_INLINE_NEON_ASM
# endif
#endif

  float ar, ai, br, bi, abr, abi;
#ifndef ZCONVOLVE_USING_INLINE_ASM
  v4sf vscal = LD_PS1(scaling);
  int i;
#endif

  assert(VALIGNED(a) && VALIGNED(b) && VALIGNED(ab));
  ar = ((v4sf_union*)va)[0].f[0];
  ai = ((v4sf_union*)va)[1].f[0];
  br = ((v4sf_union*)vb)[0].f[0];
  bi = ((v4sf_union*)vb)[1].f[0];
  abr = ((v4sf_union*)vab)[0].f[0];
  abi = ((v4sf_union*)vab)[1].f[0];
 
#ifdef ZCONVOLVE_USING_INLINE_ASM
  /* inline asm version, unfortunately miscompiled by clang 3.2,
   * at least on ubuntu.. so this will be restricted to gcc */
  const float *a_ = a, *b_ = b; float *ab_ = ab;
  int N = Ncvec;
  asm volatile("mov         r8, %2                  \n"
               "vdup.f32    q15, %4                 \n"
               "1:                                  \n"
               "pld         [%0,#64]                \n"
               "pld         [%1,#64]                \n"
               "pld         [%2,#64]                \n"
               "pld         [%0,#96]                \n"
               "pld         [%1,#96]                \n"
               "pld         [%2,#96]                \n"
               "vld1.f32    {q0,q1},   [%0,:128]!         \n"
               "vld1.f32    {q4,q5},   [%1,:128]!         \n"
               "vld1.f32    {q2,q3},   [%0,:128]!         \n"
               "vld1.f32    {q6,q7},   [%1,:128]!         \n"
               "vld1.f32    {q8,q9},   [r8,:128]!          \n"
               
               "vmul.f32    q10, q0, q4             \n"
               "vmul.f32    q11, q0, q5             \n"
               "vmul.f32    q12, q2, q6             \n" 
               "vmul.f32    q13, q2, q7             \n"                 
               "vmls.f32    q10, q1, q5             \n"
               "vmla.f32    q11, q1, q4             \n"
               "vld1.f32    {q0,q1}, [r8,:128]!     \n"
               "vmls.f32    q12, q3, q7             \n"
               "vmla.f32    q13, q3, q6             \n"
               "vmla.f32    q8, q10, q15            \n"
               "vmla.f32    q9, q11, q15            \n"
               "vmla.f32    q0, q12, q15            \n"
               "vmla.f32    q1, q13, q15            \n"
               "vst1.f32    {q8,q9},[%2,:128]!    \n"
               "vst1.f32    {q0,q1},[%2,:128]!    \n"
               "subs        %3, #2                  \n"
               "bne         1b                      \n"
               : "+r"(a_), "+r"(b_), "+r"(ab_), "+r"(N) : "r"(scaling) : "r8", "q0","q1","q2","q3","q4","q5","q6","q7","q8","q9", "q10","q11","q12","q13","q15","memory");
#else
  /* default routine, works fine for non-arm cpus with current compilers */
  for (i=0; i < Ncvec; i += 2) {
    v4sf ar, ai, br, bi;
    ar = va[2*i+0]; ai = va[2*i+1];
    br = vb[2*i+0]; bi = vb[2*i+1];
    VCPLXMUL(ar, ai, br, bi);
    vab[2*i+0] = VMADD(ar, vscal, vab[2*i+0]);
    vab[2*i+1] = VMADD(ai, vscal, vab[2*i+1]);
    ar = va[2*i+2]; ai = va[2*i+3];
    br = vb[2*i+2]; bi = vb[2*i+3];
    VCPLXMUL(ar, ai, br, bi);
    vab[2*i+2] = VMADD(ar, vscal, vab[2*i+2]);
    vab[2*i+3] = VMADD(ai, vscal, vab[2*i+3]);
  }
#endif
  if (s->transform == PFFFT_REAL) {
    ((v4sf_union*)vab)[0].f[0] = abr + ar*br*scaling;
    ((v4sf_union*)vab)[1].f[0] = abi + ai*bi*scaling;
  }
}

void FUNC_ZCONVOLVE_NO_ACCU(SETUP_STRUCT *s, const float *a, const float *b, float *ab, float scaling) {
  v4sf vscal = LD_PS1(scaling);
  const v4sf * RESTRICT va = (const v4sf*)a;
  const v4sf * RESTRICT vb = (const v4sf*)b;
  v4sf * RESTRICT vab = (v4sf*)ab;
  float sar, sai, sbr, sbi;
  const int NcvecMulTwo = 2*s->Ncvec;  /* int Ncvec = s->Ncvec; */
  int k; /* was i -- but always used "2*i" - except at for() */

#ifdef __arm__
  __builtin_prefetch(va);
  __builtin_prefetch(vb);
  __builtin_prefetch(vab);
  __builtin_prefetch(va+2);
  __builtin_prefetch(vb+2);
  __builtin_prefetch(vab+2);
  __builtin_prefetch(va+4);
  __builtin_prefetch(vb+4);
  __builtin_prefetch(vab+4);
  __builtin_prefetch(va+6);
  __builtin_prefetch(vb+6);
  __builtin_prefetch(vab+6);
# ifndef __clang__
#   define ZCONVOLVE_USING_INLINE_NEON_ASM
# endif
#endif

  assert(VALIGNED(a) && VALIGNED(b) && VALIGNED(ab));
  sar = ((v4sf_union*)va)[0].f[0];
  sai = ((v4sf_union*)va)[1].f[0];
  sbr = ((v4sf_union*)vb)[0].f[0];
  sbi = ((v4sf_union*)vb)[1].f[0];

  /* default routine, works fine for non-arm cpus with current compilers */
  for (k=0; k < NcvecMulTwo; k += 4) {
    v4sf var, vai, vbr, vbi;
    var = va[k+0]; vai = va[k+1];
    vbr = vb[k+0]; vbi = vb[k+1];
    VCPLXMUL(var, vai, vbr, vbi);
    vab[k+0] = VMUL(var, vscal);
    vab[k+1] = VMUL(vai, vscal);
    var = va[k+2]; vai = va[k+3];
    vbr = vb[k+2]; vbi = vb[k+3];
    VCPLXMUL(var, vai, vbr, vbi);
    vab[k+2] = VMUL(var, vscal);
    vab[k+3] = VMUL(vai, vscal);
  }

  if (s->transform == PFFFT_REAL) {
    ((v4sf_union*)vab)[0].f[0] = sar*sbr*scaling;
    ((v4sf_union*)vab)[1].f[0] = sai*sbi*scaling;
  }
}


#else  /* #if ( SIMD_SZ == 4 )   * !defined(PFFFT_SIMD_DISABLE) */

/* standard routine using scalar floats, without SIMD stuff. */

#define pffft_zreorder_nosimd FUNC_ZREORDER
void pffft_zreorder_nosimd(SETUP_STRUCT *setup, const float *in, float *out, pffft_direction_t direction) {
  int k, N = setup->N;
  if (setup->transform == PFFFT_COMPLEX) {
    for (k=0; k < 2*N; ++k) out[k] = in[k];
    return;
  }
  else if (direction == PFFFT_FORWARD) {
    float x_N = in[N-1];
    for (k=N-1; k > 1; --k) out[k] = in[k-1]; 
    out[0] = in[0];
    out[1] = x_N;
  } else {
    float x_N = in[1];
    for (k=1; k < N-1; ++k) out[k] = in[k+1]; 
    out[0] = in[0];
    out[N-1] = x_N;
  }
}

#define pffft_transform_internal_nosimd FUNC_TRANSFORM_INTERNAL
void pffft_transform_internal_nosimd(SETUP_STRUCT *setup, const float *input, float *output, float *scratch,
                                    pffft_direction_t direction, int ordered) {
  int Ncvec   = setup->Ncvec;
  int nf_odd = (setup->ifac[1] & 1);

  /* temporary buffer is allocated on the stack if the scratch pointer is NULL */
  int stack_allocate = (scratch == 0 ? Ncvec*2 : 1);
  VLA_ARRAY_ON_STACK(v4sf, scratch_on_stack, stack_allocate);
  float *buff[2];
  int ib;
  if (scratch == 0) scratch = scratch_on_stack;
  buff[0] = output; buff[1] = scratch;

  if (setup->transform == PFFFT_COMPLEX) ordered = 0; /* it is always ordered. */
  ib = (nf_odd ^ ordered ? 1 : 0);

  if (direction == PFFFT_FORWARD) {
    if (setup->transform == PFFFT_REAL) {
      ib = (rfftf1_ps(Ncvec*2, input, buff[ib], buff[!ib],
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);      
    } else {
      ib = (cfftf1_ps(Ncvec, input, buff[ib], buff[!ib], 
                      setup->twiddle, &setup->ifac[0], -1) == buff[0] ? 0 : 1);
    }
    if (ordered) {
      FUNC_ZREORDER(setup, buff[ib], buff[!ib], PFFFT_FORWARD); ib = !ib;
    }
  } else {    
    if (input == buff[ib]) { 
      ib = !ib; /* may happen when finput == foutput */
    }
    if (ordered) {
      FUNC_ZREORDER(setup, input, buff[!ib], PFFFT_BACKWARD); 
      input = buff[!ib];
    }
    if (setup->transform == PFFFT_REAL) {
      ib = (rfftb1_ps(Ncvec*2, input, buff[ib], buff[!ib], 
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);
    } else {
      ib = (cfftf1_ps(Ncvec, input, buff[ib], buff[!ib], 
                      setup->twiddle, &setup->ifac[0], +1) == buff[0] ? 0 : 1);
    }
  }
  if (buff[ib] != output) {
    int k;
    /* extra copy required -- this situation should happens only when finput == foutput */
    assert(input==output);
    for (k=0; k < Ncvec; ++k) {
      float a = buff[ib][2*k], b = buff[ib][2*k+1];
      output[2*k] = a; output[2*k+1] = b;
    }
    ib = !ib;
  }
  assert(buff[ib] == output);
}

#define pffft_zconvolve_accumulate_nosimd FUNC_ZCONVOLVE_ACCUMULATE
void pffft_zconvolve_accumulate_nosimd(SETUP_STRUCT *s, const float *a, const float *b,
                                       float *ab, float scaling) {
  int NcvecMulTwo = 2*s->Ncvec;  /* int Ncvec = s->Ncvec; */
  int k; /* was i -- but always used "2*i" - except at for() */

  if (s->transform == PFFFT_REAL) {
    /* take care of the fftpack ordering */
    ab[0] += a[0]*b[0]*scaling;
    ab[NcvecMulTwo-1] += a[NcvecMulTwo-1]*b[NcvecMulTwo-1]*scaling;
    ++ab; ++a; ++b; NcvecMulTwo -= 2;
  }
  for (k=0; k < NcvecMulTwo; k += 2) {
    float ar, ai, br, bi;
    ar = a[k+0]; ai = a[k+1];
    br = b[k+0]; bi = b[k+1];
    VCPLXMUL(ar, ai, br, bi);
    ab[k+0] += ar*scaling;
    ab[k+1] += ai*scaling;
  }
}

#define pffft_zconvolve_no_accu_nosimd FUNC_ZCONVOLVE_NO_ACCU
void pffft_zconvolve_no_accu_nosimd(SETUP_STRUCT *s, const float *a, const float *b,
                                    float *ab, float scaling) {
  int NcvecMulTwo = 2*s->Ncvec;  /* int Ncvec = s->Ncvec; */
  int k; /* was i -- but always used "2*i" - except at for() */

  if (s->transform == PFFFT_REAL) {
    /* take care of the fftpack ordering */
    ab[0] += a[0]*b[0]*scaling;
    ab[NcvecMulTwo-1] += a[NcvecMulTwo-1]*b[NcvecMulTwo-1]*scaling;
    ++ab; ++a; ++b; NcvecMulTwo -= 2;
  }
  for (k=0; k < NcvecMulTwo; k += 2) {
    float ar, ai, br, bi;
    ar = a[k+0]; ai = a[k+1];
    br = b[k+0]; bi = b[k+1];
    VCPLXMUL(ar, ai, br, bi);
    ab[k+0] = ar*scaling;
    ab[k+1] = ai*scaling;
  }
}


#endif /* #if ( SIMD_SZ == 4 )    * !defined(PFFFT_SIMD_DISABLE) */


void FUNC_TRANSFORM_UNORDRD(SETUP_STRUCT *setup, const float *input, float *output, float *work, pffft_direction_t direction) {
  FUNC_TRANSFORM_INTERNAL(setup, input, output, (v4sf*)work, direction, 0);
}

void FUNC_TRANSFORM_ORDERED(SETUP_STRUCT *setup, const float *input, float *output, float *work, pffft_direction_t direction) {
  FUNC_TRANSFORM_INTERNAL(setup, input, output, (v4sf*)work, direction, 1);
}


#if ( SIMD_SZ == 4 )

#define assertv4(v,f0,f1,f2,f3) assert(v.f[0] == (f0) && v.f[1] == (f1) && v.f[2] == (f2) && v.f[3] == (f3))

/* detect bugs with the vector support macros */
void FUNC_VALIDATE_SIMD_A(void) {
  float f[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
  v4sf_union a0, a1, a2, a3, t, u; 
  memcpy(a0.f, f, 4*sizeof(float));
  memcpy(a1.f, f+4, 4*sizeof(float));
  memcpy(a2.f, f+8, 4*sizeof(float));
  memcpy(a3.f, f+12, 4*sizeof(float));

  t = a0; u = a1; t.v = VZERO();
  printf("VZERO=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); assertv4(t, 0, 0, 0, 0);
  t.v = VADD(a1.v, a2.v);
  printf("VADD(4:7,8:11)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); assertv4(t, 12, 14, 16, 18);
  t.v = VMUL(a1.v, a2.v);
  printf("VMUL(4:7,8:11)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); assertv4(t, 32, 45, 60, 77);
  t.v = VMADD(a1.v, a2.v,a0.v);
  printf("VMADD(4:7,8:11,0:3)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); assertv4(t, 32, 46, 62, 80);

  INTERLEAVE2(a1.v,a2.v,t.v,u.v);
  printf("INTERLEAVE2(4:7,8:11)=[%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3], u.f[0], u.f[1], u.f[2], u.f[3]);
  assertv4(t, 4, 8, 5, 9); assertv4(u, 6, 10, 7, 11);
  UNINTERLEAVE2(a1.v,a2.v,t.v,u.v);
  printf("UNINTERLEAVE2(4:7,8:11)=[%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3], u.f[0], u.f[1], u.f[2], u.f[3]);
  assertv4(t, 4, 6, 8, 10); assertv4(u, 5, 7, 9, 11);

  t.v=LD_PS1(f[15]);
  printf("LD_PS1(15)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]);
  assertv4(t, 15, 15, 15, 15);
  t.v = VSWAPHL(a1.v, a2.v);
  printf("VSWAPHL(4:7,8:11)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]);
  assertv4(t, 8, 9, 6, 7);
  VTRANSPOSE4(a0.v, a1.v, a2.v, a3.v);
  printf("VTRANSPOSE4(0:3,4:7,8:11,12:15)=[%2g %2g %2g %2g] [%2g %2g %2g %2g] [%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", 
         a0.f[0], a0.f[1], a0.f[2], a0.f[3], a1.f[0], a1.f[1], a1.f[2], a1.f[3], 
         a2.f[0], a2.f[1], a2.f[2], a2.f[3], a3.f[0], a3.f[1], a3.f[2], a3.f[3]); 
  assertv4(a0, 0, 4, 8, 12); assertv4(a1, 1, 5, 9, 13); assertv4(a2, 2, 6, 10, 14); assertv4(a3, 3, 7, 11, 15);
}


static void pffft_assert1( float result, float ref, const char * vartxt, const char * functxt, int * numErrs, const char * f, int lineNo )
{
  if ( !( fabs( result - ref ) < 0.01F ) )
  {
    fprintf(stderr, "%s: assert for %s at %s(%d)\n  expected %f  value %f\n", functxt, vartxt, f, lineNo, ref, result);
    ++(*numErrs);
  }
}

static void pffft_assert4(  vsfscalar v0, vsfscalar v1, vsfscalar v2, vsfscalar v3,
  float a, float b, float c, float d, const char * functxt, int * numErrs, const char * f, int lineNo )
{
  pffft_assert1( v0, a, "[0]", functxt, numErrs, f, lineNo );
  pffft_assert1( v1, b, "[1]", functxt, numErrs, f, lineNo );
  pffft_assert1( v2, c, "[2]", functxt, numErrs, f, lineNo );
  pffft_assert1( v3, d, "[3]", functxt, numErrs, f, lineNo );
}

#define PFFFT_ASSERT4( V, a, b, c, d, FUNCTXT )  pffft_assert4( (V).f[0], (V).f[1], (V).f[2], (V).f[3], a, b, c, d, FUNCTXT, &numErrs, __FILE__, __LINE__ )


int FUNC_VALIDATE_SIMD_EX(FILE * DbgOut)
{
  int numErrs = 0;

  {
    v4sf_union C;
    int k;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: { }\n" );
    }
    C.v = VZERO();
    if (DbgOut) {
      fprintf(DbgOut, "VZERO(a) => C) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( C, 0.0F, 0.0F, 0.0F, 0.0F, "VZERO() Out C" );
  }

  {
    v4sf_union C;
    float a = 42.0F;
    int k;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: a = {\n" );
      fprintf(DbgOut, "  Inp a:  %f\n", a );
      fprintf(DbgOut, "}\n" );
    }
    C.v = LD_PS1(a);
    if (DbgOut) {
      fprintf(DbgOut, "LD_PS1(a) => C) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( C, 42.0F, 42.0F, 42.0F, 42.0F, "LD_PS1() Out C" );
  }

  {
    v4sf_union C;
    float a[16];
    int numAligned = 0, numUnaligned = 0;
    int k;
    const char * pUn;
    for ( k = 0; k < 16; ++k ) a[k] = k+1;

    for ( k = 0; k + 3 < 16; ++k )
    {
      const float * ptr = &a[k];
      if (DbgOut)
        fprintf(DbgOut, "\ninput: a = [ %f, %f, %f, %f ]\n", ptr[0], ptr[1], ptr[2], ptr[3] );
      if ( VALIGNED(ptr) )
      {
        C.v = VLOAD_ALIGNED( ptr );
        pUn = "";
        ++numAligned;
      }
      else
      {
        C.v = VLOAD_UNALIGNED( ptr );
        pUn = "UN";
        ++numUnaligned;
      }
      if (DbgOut) {
        fprintf(DbgOut, "C = VLOAD_%sALIGNED(&a[%d]) => {\n", pUn, k );
        fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
        fprintf(DbgOut, "}\n" );
      }
      //PFFFT_ASSERT4( C, 32.0F, 34.0F, 36.0F, 38.0F, "VADD(): Out C" );
      
      if ( numAligned >= 1 && numUnaligned >= 4 )
        break;
    }
    if ( numAligned < 1 ) {
      fprintf(stderr, "VALIGNED() should have found at least 1 occurence!");
      ++numErrs;
    }
    if ( numUnaligned < 4 ) {
      fprintf(stderr, "!VALIGNED() should have found at least 4 occurences!");
      ++numErrs;
    }
  }

  {
    v4sf_union A, B, C;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = 20 + k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    C.v = VADD(A.v, B.v);
    if (DbgOut) {
      fprintf(DbgOut, "C = VADD(A,B) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "VADD(): Inp A" );
    PFFFT_ASSERT4( B, 21.0F, 22.0F, 23.0F, 24.0F, "VADD(): Inp B" );
    PFFFT_ASSERT4( C, 32.0F, 34.0F, 36.0F, 38.0F, "VADD(): Out C" );
  }

  {
    v4sf_union A, B, C;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 20 + 2*k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    C.v = VSUB(A.v, B.v);
    if (DbgOut) {
      fprintf(DbgOut, "C = VSUB(A,B) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 21.0F, 23.0F, 25.0F, 27.0F, "VSUB(): Inp A" );
    PFFFT_ASSERT4( B, 11.0F, 12.0F, 13.0F, 14.0F, "VSUB(): Inp B" );
    PFFFT_ASSERT4( C, 10.0F, 11.0F, 12.0F, 13.0F, "VSUB(): Out C" );
  }

  {
    v4sf_union A, B, C;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    C.v = VMUL(A.v, B.v);
    if (DbgOut) {
      fprintf(DbgOut, "C = VMUL(A,B) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "VMUL(): Inp A" );
    PFFFT_ASSERT4( B,  1.0F,  2.0F,  3.0F,  4.0F, "VMUL(): Inp B" );
    PFFFT_ASSERT4( C, 11.0F, 24.0F, 39.0F, 56.0F, "VMUL(): Out C" );
  }

  {
    v4sf_union A, B, C, D;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 10 + k;
    for ( k = 0; k < 4; ++k )  D.f[k] = 40 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B,C = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "  Inp C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    D.v = VMADD(A.v, B.v, C.v);
    if (DbgOut) {
      fprintf(DbgOut, "D = VMADD(A,B,C) => {\n" );
      fprintf(DbgOut, "  Out D:  %f, %f, %f, %f\n", D.f[0], D.f[1], D.f[2], D.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "VMADD(): Inp A" );
    PFFFT_ASSERT4( B,  1.0F,  2.0F,  3.0F,  4.0F, "VMADD(): Inp B" );
    PFFFT_ASSERT4( C, 10.0F, 11.0F, 12.0F, 13.0F, "VMADD(): Inp C" );
    PFFFT_ASSERT4( D, 21.0F, 35.0F, 51.0F, 69.0F, "VMADD(): Out D" );
  }

  {
    v4sf_union A, B, C, D;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = 20 + k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;
    for ( k = 0; k < 4; ++k )  D.f[k] = 40 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    INTERLEAVE2(A.v, B.v, C.v, D.v);
    if (DbgOut) {
      fprintf(DbgOut, "INTERLEAVE2(A,B, => C,D) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "  Out D:  %f, %f, %f, %f\n", D.f[0], D.f[1], D.f[2], D.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "INTERLEAVE2() Inp A" );
    PFFFT_ASSERT4( B, 21.0F, 22.0F, 23.0F, 24.0F, "INTERLEAVE2() Inp B" );
    PFFFT_ASSERT4( C, 11.0F, 21.0F, 12.0F, 22.0F, "INTERLEAVE2() Out C" );
    PFFFT_ASSERT4( D, 13.0F, 23.0F, 14.0F, 24.0F, "INTERLEAVE2() Out D" );
  }

  {
    v4sf_union A, B, C, D;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = 20 + k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;
    for ( k = 0; k < 4; ++k )  D.f[k] = 40 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    UNINTERLEAVE2(A.v, B.v, C.v, D.v);
    if (DbgOut) {
      fprintf(DbgOut, "UNINTERLEAVE2(A,B, => C,D) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "  Out D:  %f, %f, %f, %f\n", D.f[0], D.f[1], D.f[2], D.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "UNINTERLEAVE2() Inp A" );
    PFFFT_ASSERT4( B, 21.0F, 22.0F, 23.0F, 24.0F, "UNINTERLEAVE2() Inp B" );
    PFFFT_ASSERT4( C, 11.0F, 13.0F, 21.0F, 23.0F, "UNINTERLEAVE2() Out C" );
    PFFFT_ASSERT4( D, 12.0F, 14.0F, 22.0F, 24.0F, "UNINTERLEAVE2() Out D" );
  }

  {
    v4sf_union A, B, C, D;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = 20 + k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;
    for ( k = 0; k < 4; ++k )  D.f[k] = 40 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B,C,D = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "  Inp C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "  Inp D:  %f, %f, %f, %f\n", D.f[0], D.f[1], D.f[2], D.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    VTRANSPOSE4(A.v, B.v, C.v, D.v);
    if (DbgOut) {
      fprintf(DbgOut, "VTRANSPOSE4(A,B,C,D) => {\n" );
      fprintf(DbgOut, "  Out A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Out B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "  Out D:  %f, %f, %f, %f\n", D.f[0], D.f[1], D.f[2], D.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 21.0F, 31.0F, 41.0F, "VTRANSPOSE4(): Out A" );
    PFFFT_ASSERT4( B, 12.0F, 22.0F, 32.0F, 42.0F, "VTRANSPOSE4(): Out B" );
    PFFFT_ASSERT4( C, 13.0F, 23.0F, 33.0F, 43.0F, "VTRANSPOSE4(): Out C" );
    PFFFT_ASSERT4( D, 14.0F, 24.0F, 34.0F, 44.0F, "VTRANSPOSE4(): Out D" );
  }

  {
    v4sf_union A, B, C;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  B.f[k] = 20 + k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A,B = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "  Inp B:  %f, %f, %f, %f\n", B.f[0], B.f[1], B.f[2], B.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    C.v = VSWAPHL(A.v, B.v);
    if (DbgOut) {
      fprintf(DbgOut, "C = VSWAPHL(A,B) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "VSWAPHL(): Inp A" );
    PFFFT_ASSERT4( B, 21.0F, 22.0F, 23.0F, 24.0F, "VSWAPHL(): Inp B" );
    PFFFT_ASSERT4( C, 21.0F, 22.0F, 13.0F, 14.0F, "VSWAPHL(): Out C" );
  }

  {
    v4sf_union A, C;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;
    for ( k = 0; k < 4; ++k )  C.f[k] = 30 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    C.v = VREV_S(A.v);
    if (DbgOut) {
      fprintf(DbgOut, "C = VREV_S(A) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "VREV_S(): Inp A" );
    PFFFT_ASSERT4( C, 14.0F, 13.0F, 12.0F, 11.0F, "VREV_S(): Out C" );
  }

  {
    v4sf_union A, C;
    int k;
    for ( k = 0; k < 4; ++k )  A.f[k] = 10 + k+1;

    if (DbgOut) {
      fprintf(DbgOut, "\ninput: A = {\n" );
      fprintf(DbgOut, "  Inp A:  %f, %f, %f, %f\n", A.f[0], A.f[1], A.f[2], A.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    C.v = VREV_C(A.v);
    if (DbgOut) {
      fprintf(DbgOut, "C = VREV_C(A) => {\n" );
      fprintf(DbgOut, "  Out C:  %f, %f, %f, %f\n", C.f[0], C.f[1], C.f[2], C.f[3] );
      fprintf(DbgOut, "}\n" );
    }
    PFFFT_ASSERT4( A, 11.0F, 12.0F, 13.0F, 14.0F, "VREV_C(): Inp A" );
    PFFFT_ASSERT4( C, 13.0F, 14.0F, 11.0F, 12.0F, "VREV_C(): Out A" );
  }

  return numErrs;
}

#else  /* if ( SIMD_SZ == 4 ) */

void FUNC_VALIDATE_SIMD_A()
{
}

int FUNC_VALIDATE_SIMD_EX(FILE * DbgOut)
{
  return -1;
}

#endif  /* end if ( SIMD_SZ == 4 ) */


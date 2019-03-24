#include <math.h>
#include <stdlib.h>

#include "overdrive.h"

void compute_filter_coeffs(iir_1p* cf, unsigned int type, float fs, float f0)
{
    float w0 = 2.0*M_PI*f0/fs;
    float a0, a1;
    float b0, b1;
    float g = 1.0;  // This could be brought out into a user-configurable param

    switch(type){
         case LPF1P:
            //1-pole high pass filter coefficients
            // H(z) = g * (1 - z^-1)/(1 - a1*z^-1)
            // Direct Form 1:
            //    h[n] = g * ( b0*x[n] - b1*x[n-1] ) - a1*y[n-1]
            // In below implementation gain is redistributed to the numerator:
            //    h[n] = gb0*x[n] - gb1*x[n-1] - a1*y[n-1]
            a1 = -expf(-w0);
            g = (1.0 + a1)/1.12; //0.12 zero improves RC filter emulation at higher freqs.
            b0 = g;
            b1 = 0.12*g;
            break;
         case HPF1P:
            //1-pole high pass filter coefficients
            // H(z) = g * (1 - z^-1)/(1 - a1*z^-1)
            // Direct Form 1:
            //    h[n] = g * ( b0*x[n] -  b1*x[n-1] ) - a1*y[n-1]
            // In below implementation gain is redistributed to the numerator:
            //    h[n] = g*x[n] - g*x[n-1] - a1*y[n-1]
            a1 = -expf(-w0);
            g = (1.0 - a1)*0.5;
            b0 = g;
            b1 = -g;
            break;

        default:
            break;
    }


    cf->b0 = b0;
    cf->b1 = b1;
    cf->a1 = -a1;  // filter implementation uses addition instead of subtraction

    cf->y1 = 0.0;
    cf->x1 = 0.0;

}

// Allocate the overdrive struct and set default values
overdrive* make_overdrive(overdrive* od, unsigned int oversample, unsigned int bsz, float fs)
{
    od = (overdrive*) malloc(sizeof(overdrive));
    od->procbuf = (float*) malloc(sizeof(float)*bsz*oversample);

    for(int i=0; i<bsz; i++)
    {
        od->procbuf[i] = 0.0;
    }

    od->blksz = bsz;
    od->oversample = oversample;
    od->fs = ((float) oversample)*fs;

    // Set defaults
    od->gain = 30.0;
    od->tone = 0.5;
    od->level = 0.5;
    od->bypass = true;

    // Setup EQ stages
    compute_filter_coeffs(&(od->pre_emph), HPF1P, od->fs, 720.0);
    compute_filter_coeffs(&(od->post_emph), LPF1P, od->fs, 800.0);
    compute_filter_coeffs(&(od->tone_lp), LPF1P, od->fs, 1200.0);
    compute_filter_coeffs(&(od->tone_hp), HPF1P, od->fs, 700.0);

    return od;
}

void overdrive_cleanup(overdrive* od)
{
    free(od->procbuf);
    free(od);
}

inline float tick_filter_1p(iir_1p* f, float x)
{
    f->y1 = f->b0*x + f->b1*f->x1 + f->a1*f->y1;
    f->x1 = x;
    return f->y1;
}


inline float sqr(float x)
{
    return x*x;
}

// Clipping functions
float thrs = 0.8;
float nthrs = -0.72;
float f=1.25;
void clipper_tick(int N, float* x)  // Add in gain processing and dry mix
{
    for(int i=0; i<N; i++)
    {
        //Hard limiting
        if(x[i] >= 1.2) x[i] = 1.2;
        if(x[i] <= -1.12) x[i] = -1.12;

        //Soft clipping
        if(x[i] > thrs){
            x[i] -= f*sqr(x[i] - thrs);
        }
        if(x[i] < nthrs){
            x[i] += f*sqr(x[i] - nthrs);
        }
        x[i] *= 0.7;
    }
}

void cubic_clip(int N, float asym, float* x) 
{
    float xn = 0.0;
    for(unsigned int n = 0; n < N; n++)
    {
        xn = x[n] + asym;
        if (xn<=-1.0)
            x[n] = -2.0/3.0;
        else if ((xn>-1.0) && (xn<1.0))
            x[n] = (xn) - ((1.0/3.0) * (xn*xn*xn));
        else if (xn>=1.0)
            x[n] = 2.0/3.0;
    }
}

// Set EQ parameters to non-default
// Could be real-time user-configurable, but meant for
// configuring the type of overdrive
void od_set_cut_pre_emp(overdrive* od, float fc)
{
    compute_filter_coeffs(&(od->pre_emph), HPF1P, od->fs, fc);
}

void od_set_cut_post_emp(overdrive* od, float fc)
{
    compute_filter_coeffs(&(od->post_emph), HPF1P, od->fs, fc);
}

void od_set_cut_tone_lp(overdrive* od, float fc)
{
    compute_filter_coeffs(&(od->tone_lp), HPF1P, od->fs, fc);
}

void od_set_cut_tone_hp(overdrive* od, float fc)
{
    compute_filter_coeffs(&(od->tone_hp), HPF1P, od->fs, fc);
}

// Typical real-time user-configurable parameters
void od_set_drive(overdrive* od, float drive_db)   // 0 dB to 45 dB
{
    float drv = drive_db;

    if (drv < 0.0)
        drv = 0.0;
    else if(drv > 45.0)
        drv = 45.0;

    od->gain = powf(10.0, drv/20.0);
}

void od_set_tone(overdrive* od, float hf_level_db) // high pass boost/cut, +/- 12dB
{
    float tone = hf_level_db;

    if (tone < -12.0)
        tone = -12.0;
    else if (tone > 12.0)
        tone = 12.0;

    od->tone = powf(10.0, tone/20.0);
}

void od_set_level(overdrive* od, float outlevel_db) // -40 dB to +0 dB
{
    float vol = outlevel_db;

    if (vol < -40.0)
        vol = 40.0;
    if (vol > 0.0)
        vol = 0.0;

    od->level = powf(10.0, vol/20.0);
}

bool od_set_bypass(overdrive* od, bool bypass)
{
	if(!bypass)
	{
		if(od->bypass)
			od->bypass = false;
		else
        {
            od->bypass = true;
        }
	}
	else
	{
		od->bypass = true;
	}

	return od->bypass;
}

// Run the overdrive effect
void overdrive_tick(overdrive* od, float* x)
{
    unsigned int n = od->blksz;

    if(od->bypass)
        return;
    // Run pre-emphasis filter
    for(int i = 0; i<n; i++)
    {
        od->procbuf[i] = tick_filter_1p(&(od->pre_emph), od->gain*x[i]);
    }

    // Run the clipper
    clipper_tick(n, od->procbuf);  // This quadratic function seems to generate less objectionable artefacts
    //cubic_clip(n, 0.0, od->procbuf);

    // Add clean back in like typical OD circuit
    for(int i = 0; i<n; i++)
    {
        x[i] += od->procbuf[i];
    }

    for(int i = 0; i<n; i++)
    {
        x[i] = tick_filter_1p(&(od->post_emph), od->level*x[i]);
        x[i] = tick_filter_1p(&(od->tone_lp), x[i]) + od->tone*tick_filter_1p(&(od->tone_hp), x[i]);
    }
}

/*****************************************************************************/
// File: main5.cpp
// Author: David Taubman & Renee Lu
// Last Revised: 22 July, 2020
/*****************************************************************************/
// Copyright 2007, David Taubman, The University of New South Wales (UNSW)
/*****************************************************************************/

#include "io_bmp.h"
#include "image_comps.h"
#include <math.h>
#include <tuple>
#include <vector>
#include <iostream> 
#include <cstdlib>
#include <time.h>
#include <algorithm>

using namespace std;

/* ========================================================================= */
/*                 IMPLEMENTATION OF 'my_image_comp' FUNCTIONS               */
/* ========================================================================= */

/*---------------------------------------------------------------------------*/
/*                  my_image_comp::perform_boundary_extension                */
/*                           Symmetric Extension                             */
/*---------------------------------------------------------------------------*/

void my_image_comp::perform_boundary_extension()
{
    int r, c;

    // First extend upwards by border
    float* first_line = buf;
    for (r = 1; r <= border; r++)
        for (c = 0; c < width; c++)
            first_line[-r * stride + c] = first_line[r * stride + c];

    // Now extend downwards by border
    float* last_line = buf + (height - 1) * stride;
    for (r = 1; r <= border; r++)
        for (c = 0; c < width; c++)
            last_line[r * stride + c] = last_line[-r * stride + c];

    // Now extend all rows to the left and to the right
    float* left_edge = buf - border * stride;
    float* right_edge = left_edge + width - 1;
    for (r = height + 2 * border; r > 0; r--, left_edge += stride, right_edge += stride)
        for (c = 1; c <= border; c++)
        {
            left_edge[-c] = left_edge[c];
            right_edge[c] = right_edge[-c];
        }
}

/* ========================================================================= */
/*                              Global Functions                             */
/* ========================================================================= */

/*---------------------------------------------------------------------------*/
/*                                apply_filter                               */
/*---------------------------------------------------------------------------*/

void apply_LOG_filter(my_image_comp* in, my_image_comp* out, my_image_comp* inter_1,
    my_image_comp* inter_2, my_image_comp* y1, my_image_comp* y2,
    float sigma, int H, float alpha, int debug)
{
#define PI 3.1415926F
#define FILTER_TAPS (2*H+1)

    /* Decompose Laplacian of Gaussian filter into 4 filters */
    // Origin of filters is at position filter[H]
    // Filters are initialised to 1
    vector<float> h_11_f(FILTER_TAPS, 1); // Partial of s1 -> s1 component
    vector<float> h_12_f(FILTER_TAPS, 1); // Partial of s1 -> s2 component
    vector<float> h_21_f(FILTER_TAPS, 1); // Partial of s2 -> s1 component
    vector<float> h_22_f(FILTER_TAPS, 1); // Partial of s2 -> s2 component
    for (int location = -H; location <= H; location++)
    {
        h_11_f[H + location] = (location * location - sigma * sigma) /
            (2 * PI * pow(sigma, 6) * exp((location * location) / (2 * sigma * sigma)));
        h_12_f[H + location] = exp(-(location * location) / (2 * sigma * sigma));
        h_21_f[H + location] = exp(-(location * location) / (2 * sigma * sigma));
        h_22_f[H + location] = (location * location - sigma * sigma) /
            (2 * PI * pow(sigma, 6) * exp((location * location) / (2 * sigma * sigma)));
    }

    /* Debugging filter */
    if (debug)
    {
        printf("Checking the h_11 vector...\n");
        for (int i = 0; i < h_11_f.size(); i++)
            printf("%f   ", h_11_f[i]);
        printf("\n");
        printf("Checking the h_12 vector...\n");
        for (int i = 0; i < h_12_f.size(); i++)
            printf("%f   ", h_12_f[i]);
        printf("\n");
        printf("Checking the h_21 vector...\n");
        for (int i = 0; i < h_21_f.size(); i++)
            printf("%f   ", h_21_f[i]);
        printf("\n");
        printf("Checking the h_22 vector...\n");
        for (int i = 0; i < h_22_f.size(); i++)
            printf("%f   ", h_22_f[i]);
        printf("\n");
    }

    /* Find BIBO gains for each filter for integer processing */
    float A_h11 = 0, A_h12 = 0, A_h21 = 0, A_h22 = 0;
    for (int i = 0; i < FILTER_TAPS; i++)
    {
        A_h11 += abs(h_11_f[i]);
        A_h12 += abs(h_12_f[i]);
        A_h21 += abs(h_21_f[i]);
        A_h22 += abs(h_22_f[i]);
    }    
    float A = 0;    // Overall BIBO gain
    for (int row = -H; row <= H; row++)
        for (int col = -H; col <= H; col++)
            A += abs(h_11_f[col+H] * h_12_f[row+H] - h_21_f[col+H] * h_22_f[row+H]);

    /* Find K for integer processing */
    // 32 bits = 8 bits + K bits + log2(A) bits
    int K = 32 - 8 - log2(A); 
   
    /* Take 128 away from the input for integer processing */
    // Use signed integers for full dynamic range
    for (int r = 0; r < in->height; r++)
        for (int c = 0; c < in->width; c++)
        {
            float* ip = in->buf + r * in->stride + c;
            *ip = ip[0] - 128;
        }

    /* Convert all filter values to integers */
    // A = <a . 2^K>
    vector<int> h_11(FILTER_TAPS, 1); // Partial of s1 -> s1 component
    vector<int> h_12(FILTER_TAPS, 1); // Partial of s1 -> s2 component
    vector<int> h_21(FILTER_TAPS, 1); // Partial of s2 -> s1 component
    vector<int> h_22(FILTER_TAPS, 1); // Partial of s2 -> s2 component
    for (int i = 0; i < FILTER_TAPS; i++)
    {
        h_11[i] = round(h_11_f[i] * pow(2, K));
        h_12[i] = round(h_12_f[i] * pow(2, K));
        h_21[i] = round(h_21_f[i] * pow(2, K));
        h_22[i] = round(h_22_f[i] * pow(2, K));
    }

    /* Debug integer filters */
    if (debug)
    {
        printf("BIBO %f K %d\n", A, K);
        printf("h_11 integers K = %d A = %f\n", K, A_h11);
        for (int i = 0; i < FILTER_TAPS; i++) {
            printf("%d ", h_11[i]);
        }
        printf("\n");
        printf("h_12 integers K = %d A = %f\n", K, A_h12);
        for (int i = 0; i < FILTER_TAPS; i++) {
            printf("%d ", h_12[i]);
        }
        printf("\n");
        printf("h_21 integers K = %d A = %f\n", K, A_h21);
        for (int i = 0; i < FILTER_TAPS; i++) {
            printf("%d ", h_21[i]);
        }
        printf("\n");
        printf("h_22 integers K = %d A = %f\n", K, A_h22);
        for (int i = 0; i < FILTER_TAPS; i++) {
            printf("%d ", h_22[i]);
        }
        printf("\n");
    }

    /* Check that inputs have enough boundary extension for filtering */
    assert(in->border >= H);
    assert(inter_1->border >= H);
    assert(inter_2->border >= H);

    /* Perform the separable convolution */

    // Horizontal Convolution inter_1 = x * h_11
    for (int r = 0; r < out->height; r++)
        for (int c = 0; c < out->width; c++)
        {
            float* ip = in->buf + r * in->stride + c;
            float* op = inter_1->buf + r * inter_1->stride + c;
            int sum = 0;
            for (int filter_spot = -H; filter_spot <= H; filter_spot++)
                sum += ip[filter_spot] * h_11[H + filter_spot];
            *op = (sum >> K);
        }
    // Symmetrically extend inter_1
    inter_1->perform_boundary_extension();
    // Vertical (Stripe) Convolution y1 = (x * h_11) * h_12
    for (int r = 0; r < out->height; r++)
        for (int c = 0; c < out->width; c++)
        {
            float* ip = inter_1->buf + r * inter_1->stride + c;
            float* op = y1->buf + r * y1->stride + c;
            int sum = 0;
            for (int filter_spot = -H; filter_spot <= H; filter_spot++)
                sum += ip[filter_spot * inter_1->stride] * h_12[H + filter_spot];
            *op = (sum >> K);
        }

    // Horizontal Convolution inter_2 = x * h_21
    for (int r = 0; r < out->height; r++)
        for (int c = 0; c < out->width; c++)
        {
            float* ip = in->buf + r * in->stride + c;
            float* op = inter_2->buf + r * inter_2->stride + c;
            int sum = 0;
            for (int filter_spot = -H; filter_spot <= H; filter_spot++)
                sum += ip[filter_spot] * h_21[H + filter_spot];
            *op = (sum >> K);
        }
    // Symmetrically extend inter_2
    inter_2->perform_boundary_extension();
    // Vertical (Stripe) Convolution y2 = (x * h_21) * h_22
    for (int r = 0; r < out->height; r++)
        for (int c = 0; c < out->width; c++)
        {
            float* ip = inter_2->buf + r * inter_2->stride + c;
            float* op = y2->buf + r * y2->stride + c;
            int sum = 0;
            for (int filter_spot = -H; filter_spot <= H; filter_spot++)
                sum += ip[filter_spot * inter_2->stride] * h_22[H + filter_spot];
            *op = (sum >> K);
        }

    // Sum y1 + y2
    for (int r = 0; r < out->height; r++)
        for (int c = 0; c < out->width; c++)
        {
            float* y1_p = y1->buf + r * y1->stride + c;
            float* y2_p = y2->buf + r * y2->stride + c;
            float* op = out->buf + r * out->stride + c;
            int sum = y1_p[0] + y2_p[0];
            *op = (sum*alpha) + 128;
        }
}

/*---------------------------------------------------------------------------*/
/*                                    main                                   */
/*---------------------------------------------------------------------------*/

int
main(int argc, char* argv[])
{
    /* Get the args */
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s <input bmp file> <output bmp file> <sigma> <alpha>\n", argv[0]);
        return -1;
    }
    float sigma = atof(argv[3]);    // Determines LoG filter co-efficients
    float alpha = atoi(argv[4]);    // Scaling factor alpha
    int H = ceil(3 * sigma);        // Extent of filter, for Nyquist Bandlimited

    /* Create the input image storage */
    int err_code = 0;
    try {
        // Read the input image
        bmp_in in;
        if ((err_code = bmp_in__open(&in, argv[1])) != 0)
            throw err_code;
        // Get input and input border dimensions
        int width = in.cols, height = in.rows;
        int n, num_comps = in.num_components;   // Number of colour components
        // Initialise the input storage
        my_image_comp* input_comps = new my_image_comp[num_comps];
        for (n = 0; n < num_comps; n++)
            input_comps[n].init(height, width, H); // Leave a border of H

        int r; // Declare row index
        io_byte* line = new io_byte[width * num_comps];
        for (r = height - 1; r >= 0; r--)
        { // "r" holds the true row index we are reading, since the image is
          // stored upside down in the BMP file.
            if ((err_code = bmp_in__get_line(&in, line)) != 0)
                throw err_code;
            for (n = 0; n < num_comps; n++)
            {
                io_byte* src = line + n; // Points to first sample of component n
                float* dst = input_comps[n].buf + r * input_comps[n].stride;
                for (int c = 0; c < width; c++, src += num_comps)
                    dst[c] = (float)*src; // The cast to type "float" is not
                          // strictly required here, since bytes can always be
                          // converted to floats without any loss of information.
            }
        }
        bmp_in__close(&in);

        /*------------------------------- TASK 2 -------------------------------*/
        int debug = 0;

        // Symmetric extension for input
        for (n = 0; n < num_comps; n++)
            input_comps[n].perform_boundary_extension();

        // Allocate storage for the filtered outputs
        my_image_comp* output_comps = new my_image_comp[num_comps];  // output = y1 + y2
        my_image_comp* inter_1_comps = new my_image_comp[num_comps]; // intermediate y1   
        my_image_comp* inter_2_comps = new my_image_comp[num_comps]; // intermediate y2
        my_image_comp* y1_comps = new my_image_comp[num_comps];      // Partial of s1
        my_image_comp* y2_comps = new my_image_comp[num_comps];      // Partial of s2
        for (n = 0; n < num_comps; n++)
        {
            output_comps[n].init(height, width, 0);
            inter_1_comps[n].init(height, width, H);
            inter_2_comps[n].init(height, width, H);
            y1_comps[n].init(height, width, 0);
            y2_comps[n].init(height, width, 0);
        }

        // Process the image, all in floating point (easy)
        for (n = 0; n < num_comps; n++)
            apply_LOG_filter(input_comps + n, output_comps + n, inter_1_comps + n,
                inter_2_comps + n, y1_comps + n, y2_comps + n, sigma, H, alpha, debug);

        /*----------------------------------------------------------------------*/

       // Write the image back out again
        bmp_out out;
        if ((err_code = bmp_out__open(&out, argv[2], width, height, num_comps)) != 0)
            throw err_code;
        for (r = height - 1; r >= 0; r--)
        { // "r" holds the true row index we are writing, since the image is
          // written upside down in BMP files.
            for (n = 0; n < num_comps; n++)
            {
                io_byte* dst = line + n; // Points to first sample of component n
                float* src = output_comps[n].buf + r * output_comps[n].stride;
                for (int c = 0; c < width; c++, dst += num_comps)
                {
                    // Deal with overflow and underflow
                    if (src[c] > 255)
                        src[c] = 255;
                    else if (src[c] < 0)
                        src[c] = 0;
                    // Write output to destination
                    *dst = (int)src[c];
                }
            }
            bmp_out__put_line(&out, line);
        }
        bmp_out__close(&out);
        delete[] line;
        delete[] input_comps;
        delete[] output_comps;
    }
    catch (int exc) {
        if (exc == IO_ERR_NO_FILE)
            fprintf(stderr, "Cannot open supplied input or output file.\n");
        else if (exc == IO_ERR_FILE_HEADER)
            fprintf(stderr, "Error encountered while parsing BMP file header.\n");
        else if (exc == IO_ERR_UNSUPPORTED)
            fprintf(stderr, "Input uses an unsupported BMP file format.\n  Current "
                "simple example supports only 8-bit and 24-bit data.\n");
        else if (exc == IO_ERR_FILE_TRUNC)
            fprintf(stderr, "Input or output file truncated unexpectedly.\n");
        else if (exc == IO_ERR_FILE_NOT_OPEN)
            fprintf(stderr, "Trying to access a file which is not open!(?)\n");
        return -1;
    }
    return 0;
}
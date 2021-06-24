#define _CRT_SECURE_NO_WARNINGS
#define data_size 48600
#define input_data_width 32
#define input_data_ch 1
//input data(input_data_width, input_data_width)
#define conv1_in_width input_data_width
#define conv1_in_ch input_data_ch
#define conv1_out_width 24
#define conv1_out_ch 256
#define conv1_stride 1
#define conv2_in_width conv1_out_width
#define conv2_in_ch conv1_out_ch
#define conv2_out_ch 256
#define conv2_out_width 8
#define conv2_stride 2
#define kernel_width 9
#define num_primary_caps 2048
#define dim_primary_caps 8
//primarycaps(dim_primary_caps, num_primary_caps)
#define dim_predic_vector 16
#define num_class 5
//prediction_vectors(num_primary_caps, num_class, dim_predic_vector)
#define num_iterations 1
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "fixedpoint.h"

#include "system.h"
#include "io.h"
#include <stdio.h>
#include "fixedpoint.h"
#include <sys/alt_cache.h>
#include "sys/alt_alarm.h"
#define input_BASE       0x32A8C40
#define kernel0_BASE      0x2880010
#define kernel1_BASE      0x0
#define output_BASE      0x3954630
#define csr_BASE         0x04000000




void convolution(char* input, char* kernel, char* output, int input_width,
    int input_ch, int output_width, int output_ch, int stride) {//conv1: 5, 7, 5 pri: 5, 6, 0
    int tmp;
    int i, j, k, p, l, m;
    for (i = 0; i < output_width; i++) {
        for (j = 0; j < output_width; j++) {
            for (k = 0; k < output_ch; k++) {
                tmp = 0;
                for (p = 0; p < input_ch; p++) {
                    for (l = 0; l < kernel_width; l++) {
                        for (m = 0; m < kernel_width; m++) {
                            tmp += (input[input_width * input_width * p + input_width * (i * stride + l) + (j * stride + m)]*
                                        kernel[k * kernel_width * kernel_width * input_ch + p * kernel_width * kernel_width + l * kernel_width + m]);
                            /*tmp += mul_16(input[input_width * input_width * p + input_width * (i * stride + l) + (j * stride + m)],
                                        kernel[k * kernel_width * kernel_width * input_ch + p * kernel_width * kernel_width + l * kernel_width + m]);*/
                        }
                    }
                }
                if(stride==1)   output[k * output_width * output_width + i * output_width + j] = CLIP_8(tmp>>7);
                else            output[k * output_width * output_width + i * output_width + j] = CLIP_8(tmp>>11);
            }
        }
    }
}

void ReLU(char* input, char* output) {
    for (int i=0; i < conv1_out_ch * conv1_out_width * conv1_out_width; i++) {
        if (input[i] < 0) output[i] = 0;
        else output[i]=input[i];
    }
}
void squash(char* input, char* output, int dim_caps, int num_caps, int in_fraction, int out_fraction) {
    int tmp;
    for (int i=0; i < num_caps; i++) {
        tmp = 0;
        for (int j=0; j < dim_caps; j++) {
            tmp += abs(input[i * dim_caps + j]);
        }
        for (int k=0; k < dim_caps; k++) {
            output[i * dim_caps + k] = CLIP_8((input[i * dim_caps + k]<<(out_fraction)) / (tmp + (1<<in_fraction)));
        }
    }
}
void squash_(short* input, char* output, int dim_caps, int num_caps, int in_fraction, int out_fraction) {
    int tmp;
    for (int i=0; i < num_caps; i++) {
        tmp = 0;
        for (int j=0; j < dim_caps; j++) {
            tmp += abs(input[i * dim_caps + j]);
        }
        for (int k=0; k < dim_caps; k++) {
            output[i * dim_caps + k] = CLIP_8((input[i * dim_caps + k]<<(out_fraction)) / (tmp + (1<<in_fraction)));
        }
    }
}
void prediction_vectors(char* input, char* weight_matrix, short* output) {//7, 6, 16/13
    int tmp;
    for (int l = 0; l < num_primary_caps; l++) {
        for (int k = 0; k < num_class; k++) {
            for (int j = 0; j < dim_predic_vector; j++) {
                tmp = 0;
                for (int i = 0; i < dim_primary_caps; i++) {
                    tmp += (weight_matrix[l * dim_predic_vector * dim_primary_caps * num_class + dim_predic_vector * dim_primary_caps * k + dim_primary_caps * j + i] *
                            input[i + l * dim_primary_caps]);
                    /*output[j + dim_predic_vector * k + dim_predic_vector * num_class * l] =
                        add_16(output[j + dim_predic_vector * k + dim_predic_vector * num_class * l],
                            CLIP_16(weight_matrix[l * dim_predic_vector * dim_primary_caps * num_class + dim_predic_vector * dim_primary_caps * k + dim_primary_caps * j + i] *
                                input[i + l * dim_primary_caps]));*/
                }
                output[j + dim_predic_vector * k + dim_predic_vector * num_class * l] = CLIP_16(tmp);
            }
        }
    }
}


void dynamic_routing(short* uhat, char* bias, char* v_j) {// 16/13, 10, 7

    short s_j[num_class * dim_predic_vector];
    //char u_vj1[num_primary_caps * num_class];
    int tmp = 0;
    int i, j, k, l;

    for (i = 0;i < num_iterations;i++) {
        //softmax(b_ij, c_ij);
        for (j = 0;j < num_class;j++) {
            for (k = 0;k < dim_predic_vector;k++) {
                tmp = 0;
                for (l = 0;l < num_primary_caps;l++) {
                    //tmp += ((c_ij[num_class * l + j] * uhat[num_class * dim_predic_vector * l + dim_predic_vector * j + k]));
                    tmp += uhat[num_class * dim_predic_vector * l + dim_predic_vector * j + k];
                }
                s_j[j * dim_predic_vector + k] = CLIP_16((tmp + bias[j * dim_predic_vector + k]<<3));// 16/13
                //v_j[j * dim_predic_vector + k] = CLIP_16(s_j[j * dim_predic_vector + k]);
            }
        }
        squash_(s_j, v_j, dim_predic_vector, num_class, 13, 7);
        /*
        for (int j = 0; j < dim_predic_vector; j++) {
            L1[j] = 0; L2[j] = 0; L8[j] = 0;
            for (int k = 0; k < num_class; k++) {
                //printf("s_j : %f\n", fx2fp(s_j[k * dim_predic_vector + j]));
                L1[j] = add_16(L1[j], mul_16(a_, qabs(s_j[k * dim_predic_vector + j])));
                if (qabs(s_j[k * dim_predic_vector + j]) > L8[j]) L8[j] = qabs(s_j[k * dim_predic_vector + j]);
            }
            tmp = mul_16(b_, L8[j]);
            L2[j] = add_16(L1[j], tmp);
            //printf(" L2 %d ",L2[j]);
            for (int k = 0; k < num_class; k++) {
                v_j[j + k * dim_predic_vector] = CLIP_16((s_j[j + k * dim_predic_vector] << fraction) / L2[j]);
            }
        }
        //squash(s_j, v_j, dim_predic_vector, num_class);
        for (j = 0; j < num_primary_caps; j++) {
            for (k = 0; k < num_class; k++) {
                tmp = 0;
                for (l = 0; l < dim_predic_vector; l++) {
                    tmp = add_16(tmp, mul_16(v_j[k * dim_predic_vector + l], uhat[j * num_class * dim_predic_vector + k * dim_predic_vector + l]));
                }
                b_ij[num_class * j + k] += tmp;
            }
        }*/
    }
}

int main() {


	volatile char* input_ptr = (char*)input_BASE;
	volatile char* kernel0_ptr = (char*)kernel0_BASE;
	volatile char* kernel1_ptr = (char*)kernel1_BASE;
	volatile char* output_ptr = (char*)output_BASE;
	volatile char* csr_ptr = (char*)csr_BASE;
	volatile char* LABEL =0x3ffffa0;
    volatile float* bias_ptr =0x32a8830;
	volatile char* buffer=output_BASE;
	volatile char* W_ptr=0x28A8820;

	volatile char* dr_output_ptr=0x3400000;
    FILE* fp, * num_wrong;
    int start_time, finish_time, total_time;
    int predict = 0;
    int max = 0;
    float accuracy;
    int predict_sum;
    int correct = 0;
    int tmp_predict = 0;
    int wrong = 0;
    FILE* FI;
    FILE* FL;
    fp = fopen("/mnt/host/char_label_smallNORB.bin", "rb"); if (fp == NULL){
        printf("Cannot open file.\n");
        exit(1);
    }
    fread(LABEL, sizeof(char), data_size, fp);
    printf("label : OK\n");
    printf("\n");
    ///////////////////////////////////////////

    fp = fopen("/mnt/host/char_conv1_kernel_smallNORB.bin", "rb"); if (fp == NULL){
        printf("Cannot open file.\n");
        exit(1);
    }
    fread(conv1_kernel, sizeof(char), conv1_out_ch * conv1_in_ch * kernel_width * kernel_width, fp);
    printf("conv1_kernel : OK\n");
    printf("\n");
    ///////////////////////////////////////////

    fp = fopen("/mnt/host/char_conv2_kernel_smallNORB.bin", "rb"); if (fp == NULL){
        printf("Cannot open file.\n");
        exit(1);
    }
    fread(conv2_kernel, sizeof(char), conv2_out_ch * conv2_in_ch * kernel_width * kernel_width, fp);
    printf("conv2_kernel : OK\n");
    printf("\n");
    ///////////////////////////////////////////

    fp = fopen("/mnt/host/char_digit_caps_W_smallNORB.bin", "rb"); if (fp == NULL){
        printf("Cannot open file.\n");
        exit(1);
    }
    fread(digits_W, sizeof(char), num_primary_caps * num_class * dim_predic_vector * dim_primary_caps, fp);
    printf("digits_W : OK\n");
    printf("\n");
    ///////////////////////////////////////////
    fp = fopen("/mnt/host/char_digit_caps_bias_smallNORB.bin", "rb"); if (fp == NULL){
        printf("Cannot open file.\n");
        exit(1);
    }
    fread(bias_ptr, sizeof(char), num_class * dim_predic_vector, fp);
    printf("digits_bias : OK\n");
    printf("\n");
    ///////////////////////////////////////////

    fp = fopen("/mnt/host/char_digit_caps_W_smallNORB.bin", "rb"); if (fp == NULL)
        {
            printf("Cannot open file.\n");
            exit(1);
        }
        fread(W_ptr, sizeof(char), 1152 * 10 * 16 * 8, fp);

        fclose(fp);
        printf("digits_W : OK\n");
            printf("\n");
       //for (int i=0; i<6400; i++){
       //        IOWR(0x3000000, i , 0);
       //}
       //fclose(fp);

       fp = fopen("/mnt/host/char_conv1_kernel_smallNORB.bin", "rb"); if (fp == NULL)
        {
            printf("Cannot open file.\n");
            exit(1);
        }
       fread(buffer, sizeof(char), 20736, fp);
       for(int i=0;  i<20736;i++){
          IOWR_8DIRECT(kernel0_ptr,i,buffer[i]);
       }
       printf("conv1_kernel : OK\n");
           printf("\n");
       fp = fopen("/mnt/host/char_conv2_kernel_smallNORB.bin", "rb"); if (fp == NULL)
       {
           printf("Cannot open file.\n");
           exit(1);
       }
       for(int i=0;i<256*256;i++){
           fread(buffer, sizeof(char), 81, fp);
           for(int j=0;  j< 81;j++){
               IOWR_8DIRECT(kernel1_ptr+i*81,j,buffer[j]);
           }
       }
       printf("conv2_kernel : OK\n");
           printf("\n");

           FL = fopen("/mnt/host/char_label_smallNORB.bin", "rb");
       if (FL == NULL) {
           printf("Cannot open file.\n");
           exit(1);
       }


       fp = fopen("/mnt/host/char_dataset_smallNORB.bin", "rb"); if (fp == NULL){
           printf("Cannot open file.\n");
           exit(1);
       }
    for (int k = 0; k < data_size; k++) {
        fread(buffer, sizeof(char), input_data_ch * input_data_width * input_data_width, fp);
        for(int i=0;  i<32*32;i++){
                 IOWR_8DIRECT(input_ptr,i,buffer[i]);
        }
        fread(LABEL, sizeof(float), 1, FL);
        printf("\n");
        start_time = alt_nticks();
        convolution(input_ptr, kernel0_ptr,  output_ptr, conv1_in_width, conv1_in_ch, conv1_out_width, conv1_out_ch, conv1_stride);

        ReLU(output_ptr,input_ptr);
        finish_time = alt_nticks();
                total_time = ((finish_time - start_time));
                printf("1D Conv + ReLu time: %d ms\n", total_time);

        start_time = alt_nticks();
        convolution(input_ptr, kernel1_ptr, output_ptr, conv2_in_width, conv2_in_ch, conv2_out_width, conv2_out_ch, conv2_stride);

        finish_time = alt_nticks();
        total_time = ((finish_time - start_time));
                        printf("2D Conv time: %d ms\n", total_time);
        char conv2_output_transpose[num_primary_caps * dim_primary_caps];
        start_time = alt_nticks();
        for (int i = 0; i < dim_primary_caps; i++) {
            for (int j = 0; j < num_primary_caps; j++) {
                //conv2_output_transpose[j * dim_primary_caps + i] = conv2_output[i * num_primary_caps + j];
                input_ptr[i * num_primary_caps + j] = output_ptr[i * num_primary_caps + j];
            }
        }
        squash(input_ptr,output_ptr,dim_primary_caps,num_primary_caps, 0, 7);

        prediction_vectors(output_ptr, W_ptr, input_ptr);
        finish_time = alt_nticks();
                          total_time = ((finish_time - start_time));
                          printf("transpose + u_hat time: %d s\n", total_time);
        printf("\n");

        start_time = alt_nticks();
        dynamic_routing(input_ptr, bias_ptr, output_ptr);
        finish_time = alt_nticks();
                             total_time = ((finish_time - start_time));
                             printf("dr time: %d ms\n", total_time);


        printf("\n");
        for (int i = 0; i < num_class; i++) {
            for (int j = 0; j < dim_predic_vector; j++) {
                printf("%d, ", (output_ptr[j + i * dim_predic_vector]));
            }
            printf("\n");
        }


        max = 0;
        for (int i = 0; i < num_class; i++) {
            predict_sum = 0;
            for (int j = 0; j < dim_predic_vector; j++) {
                tmp_predict = (output_ptr[i * dim_predic_vector + j] * output_ptr[i * dim_predic_vector + j]);
                predict_sum += tmp_predict;
            }
            if (predict_sum > max) {
                max = predict_sum;
                predict = i;
            }
        }

        printf("(%d) pred : %d / ", k + 1, predict);
        if (predict == (int)*(LABEL))  correct++;
        printf("target: %d / ", (int)*(LABEL));
        accuracy = (float)correct / (k + 1);
        printf("accuracy : %.2f\n\n", accuracy * 100);

    }
    fclose(fp);
    return 0;
}

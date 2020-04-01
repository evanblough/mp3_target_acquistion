#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include "string.h"

//Launcher Values
#define LAUNCHER_NODE           "/dev/launcher0"
#define LAUNCHER_FIRE           0x10
#define LAUNCHER_STOP           0x20
#define LAUNCHER_UP             0x02
#define LAUNCHER_DOWN           0x01
#define LAUNCHER_LEFT           0x04
#define LAUNCHER_RIGHT          0x08
#define LAUNCHER_UP_LEFT        (LAUNCHER_UP | LAUNCHER_LEFT)
#define LAUNCHER_DOWN_LEFT      (LAUNCHER_DOWN | LAUNCHER_LEFT)
#define LAUNCHER_UP_RIGHT       (LAUNCHER_UP | LAUNCHER_RIGHT)
#define LAUNCHER_DOWN_RIGHT     (LAUNCHER_DOWN | LAUNCHER_RIGHT)
#define BUTTONS_BASE_ADDR        0x41200000
#define SWITCHES_BASE_ADDR        0x41210000

//Picture Values
#define MEM_INTERFACE        "/dev/mem"
#define CRCB_MASK(val) ((unsigned char)((val & 0xFF00) >> 8))
#define LUMA_MASK ((unsigned short) 0x00FF)

struct detect_config {
    unsigned char cb_threshold_low;
    unsigned char cb_threshold_high;
    unsigned char cr_threshold_low;
    unsigned char cr_threshold_high;
    unsigned char y_threshold_high;
    unsigned char y_threshold_low;
    int offset_max;
    int num_not_green_max;
};

void display_config(struct detect_config *config);

/**
 * @brief calculates the number of surronding pixels that fall into the tolerance values set in config
 * @param frame_buffer_ptr 1920x1080 ycbcr format 4:2:2 where frame_buffer_ptr[even] = cr/Y; framebuffer_ptr[odd] = cb/Y
 * @param i center pixel of surronding pixels to be checked
 * @param j center pixel of surronding pixels to be checked
 * @param config struct that holds threshold values. Set these high and low values to detect colors falling in that range. Ex: Green cbmin 0 cbmax 128 crmin 0 crmax 50
 * @return returns the number of pixels that were within tolerance bounds, until the number of out of bounds colors exceeds num_not_green_max
 */
int calc_largest_radius(volatile unsigned short *frame_buffer_ptr, int i, int j, struct detect_config *config);

void log_frame(volatile unsigned short *frame_buffer);

void display_active_map(struct detect_config *config, volatile unsigned short *output, volatile unsigned short *input);

extern int errno;

static void launcher_cmd(int fd, int cmd) {
    int retval = 0;
    fflush(stdout);
    fflush(stderr);
    fprintf(stdout, "attempting to write\n");
    retval = write(fd, &cmd, 1);

    while (retval != 1) {
        if (retval < 0) {
            fprintf(stderr, "Could not send command to %s (error %d)\n", LAUNCHER_NODE, retval);
        } else if (retval == 0) {
            fprintf(stdout, "Command busy, waiting...\n");
        }
        fprintf(stdout, "retval is %d\n", retval);
    }

    if (cmd == LAUNCHER_FIRE) {
        usleep(5000000);
    }
    return;
}

int main(int argc, char *argv[]) {
    volatile unsigned short *out_buffer_ptr;
    int mem_fd;
    int fd;
    int cmd = LAUNCHER_STOP;
    char *dev = LAUNCHER_NODE;
    unsigned int duration = 500;
    fd = open(dev, O_RDWR);
    if (fd == -1) {
        perror("Couldn't open file: %m");
        exit(1);
    }
    mem_fd = open(MEM_INTERFACE, O_RDWR);
    if (mem_fd == -1) {
        perror("Couldn't open file: %m");
        exit(1);
    }
    int vdma_addr;
    printf("Supply VDMA in addr\n");
    scanf("%x", &vdma_addr);
    printf("VDMA in addr is %x\n", vdma_addr);
    volatile unsigned short *frame_buffer_ptr = (unsigned short *) mmap(NULL, 1920 * 1080 * 2, PROT_READ, MAP_SHARED,
                                                                        mem_fd, (off_t) vdma_addr);
    if (frame_buffer_ptr == MAP_FAILED) {
        printf("MMAP FAIL\n");
        return -1;
    }
    if (argc == 2 && strncmp("-display", argv[1], 8) == 0) {
        printf("Supply VDMA out addr\n");
        scanf("%x", &vdma_addr);
        printf("VDMA out addr is %x\n", vdma_addr);
        out_buffer_ptr = (unsigned short *) mmap(NULL, 1920 * 1080 * 2, PROT_WRITE, MAP_SHARED, mem_fd,
                                                 (off_t) vdma_addr);
        if (out_buffer_ptr == MAP_FAILED) {
            printf("MMAP FAIL\n");
            return -1;
        }
    }
    //Verbose print pixel values
    if (argc == 2 && strncmp("-v", argv[1], 2) == 0) {
        log_frame(frame_buffer_ptr);
    }
    //Test Log frame function
    if (argc == 2 && strncmp("-t", argv[1], 2) == 0) {
        frame_buffer_ptr = malloc(1920 * 1080 * 2);
        int m, n;
        for (m = 0; m < 1080; m++) {
            for (n = 0; n < 1920; n++) {
                frame_buffer_ptr[m * 1920 + n] = (unsigned short) (m * 1920 + n % 65535);
            }
        }
        log_frame(frame_buffer_ptr);
    }

    struct detect_config *config = malloc(sizeof(struct detect_config));
    config->cb_threshold_high = 0;
    config->cb_threshold_low = 0;
    config->cr_threshold_high = 0;
    config->cr_threshold_low = 0;
    config->y_threshold_high = 0;
    config->y_threshold_low = 0;
    config->offset_max = 0;
    config->num_not_green_max = 0;
    size_t bytes;
    int i_max, j_max, radius_max, i, j;
    i_max = 0;
    j_max = 0;
    radius_max = 0;
    int bytes_read = 0;
    while (-1 != bytes_read) {
        //Stop Config Phase

        printf("Supply Threshold CBMax CBMin CRMax CRMin\n");
        int a, b, c, d;
        bytes_read = scanf("%d %d %d %d", &a, &b, &c, &d);
        if (bytes_read == -1) {
            break;
        }
        config->cb_threshold_high = a;
        config->cb_threshold_low = b;
        config->cr_threshold_high = c;
        config->cr_threshold_low = d;
        printf("Supply Threshold YMax YMin MaxRadius Tolerance\n");
        bytes_read = scanf("%d %d %d %d", &a, &b, &config->offset_max, &config->num_not_green_max);
        if (bytes_read == -1) {
            break;
        }
        config->y_threshold_high = a;
        config->y_threshold_low = b;
        display_config(config);

        //Run Alg on one frame
        //Assuming Green target for now
        //Assuming pointer assignment is correct
        unsigned char cb, cr, y;

        int temp_radius = 0;
        radius_max = 0;
        //Every 4th Row
        for (i = 0; i < 1080; i++) {
            //Every 8th Col_ish
            for (j = 210; j < 1920 - 210; j += 2) {
                cr = CRCB_MASK(frame_buffer_ptr[i * 1920 + j]);
                cb = CRCB_MASK(frame_buffer_ptr[i * 1920 + j + 1]);
                y = LUMA_MASK & frame_buffer_ptr[i * 1920 + j];
                //Green pixel
                if (cr < config->cr_threshold_high && cr > config->cr_threshold_low && cb < config->cb_threshold_high &&
                    cb > config->cb_threshold_low) {
                    if (y < config->y_threshold_high && y > config->y_threshold_low) {
                        temp_radius = calc_largest_radius(frame_buffer_ptr, i, j, config);
                        if (temp_radius > radius_max) {
                            i_max = i;
                            j_max = j;
                            radius_max = temp_radius;
                        }
                    }
                }
            }
        }
        printf("Center is x: %d, y: %d\n", j_max, i_max);
        if (argc == 2 && strncmp("-display", argv[1], 8) == 0) {

            printf("Input center to examine: x, y, channel");
            scanf("%d %d %d", &a, &b, &c);
            unsigned char pixel[3];
            for (i = b - 4; i < b + 4; i++) {
                for (j = a - 4; j < a + 4; j++) {
                    if (i > 0 && i < 1080 && j > 0 && j < 1920) {
                        if (j % 2) {
                            pixel[0] = CRCB_MASK(frame_buffer_ptr[i * 1920 + j]);
                        } else {
                            pixel[1] = CRCB_MASK(frame_buffer_ptr[i * 1920 + j]);
                        }
                        pixel[2] = LUMA_MASK & frame_buffer_ptr[i * 1920 + j];
                        if (j == a + 4) {
                            printf("%d\n", pixel[c]);
                        } else {
                            printf("%d, ", pixel[c]);
                        }

                    }
                }
            }
        }

    }
    //Jmax and Imax should be center of target try to move in direction that moves imax and jmax to center j1920/2 + i1080/2
    close(fd);
    return EXIT_SUCCESS;
}

int calc_largest_radius(volatile unsigned short *frame_buffer_ptr, int i, int j, struct detect_config *config) {
    int num_green_pixels = 0;
    int num_not_green = 0;
    int x, y;
    int offset = 1;
    while (offset < config->offset_max) {
        //Left and Right Columns
        for (y = i - offset; y <= i + offset; y++) {
            int index1, index2;
            index1 = y * 1920 + j + offset;
            index2 = y * 1920 + j - offset;
            //Valid bounds check;
            if (index1 > 0 && index1 < 1920 * 1080) {
                //CB
                if (index1 % 2) {
                    //If outside color bounds
                    if (CRCB_MASK(frame_buffer_ptr[index1]) > config->cb_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index1]) < config->cb_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
                    //CR
                else {
                    if (CRCB_MASK(frame_buffer_ptr[index1]) > config->cr_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index1]) < config->cr_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
            }
            if (index2 > 0 && index2 < 1920 * 1080) {
                //CB
                if (index2 % 2) {
                    //If outside color bounds
                    if (CRCB_MASK(frame_buffer_ptr[index2]) > config->cb_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index2]) < config->cb_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
                    //CR
                else {
                    if (CRCB_MASK(frame_buffer_ptr[index2]) > config->cr_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index2]) < config->cr_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
            }
        }
        //Top and Bottom Row
        for (x = j - offset; x <= j + offset; x++) {
            int index1, index2;
            index1 = (i + offset) * 1920 + x;
            index2 = (i - offset) * 1920 + x;
            //Valid bounds check;
            if (index1 > 0 && index1 < 1920 * 1080) {
                //CB
                if (index1 % 2) {
                    //If outside color bounds
                    if (CRCB_MASK(frame_buffer_ptr[index1]) > config->cb_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index1]) < config->cb_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
                    //CR
                else {
                    if (CRCB_MASK(frame_buffer_ptr[index1]) > config->cr_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index1]) < config->cr_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
            }
            if (index2 > 0 && index2 < 1920 * 1080) {
                //CB
                if (index2 % 2) {
                    //If outside color bounds
                    if (CRCB_MASK(frame_buffer_ptr[index2]) > config->cb_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index2]) < config->cb_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
                    //CR
                else {
                    if (CRCB_MASK(frame_buffer_ptr[index2]) > config->cr_threshold_high ||
                        CRCB_MASK(frame_buffer_ptr[index2]) < config->cr_threshold_low) {
                        num_not_green++;
                    }
                        //Y
                    else if ((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high ||
                             (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low) {
                        num_not_green++;
                    }
                        //Else count another green pixel
                    else {
                        num_green_pixels++;
                    }
                }
            }
        }
        if (num_not_green > config->num_not_green_max) {
            return num_green_pixels;
        }
        offset++;
    }
    return num_green_pixels;
}

void display_config(struct detect_config *config) {
    printf("CBMAX: %d\tCBMIN: %d\tCRMAX: %d\t\tCRMIN: %d\n", config->cb_threshold_high, config->cb_threshold_low,
           config->cr_threshold_high, config->cr_threshold_low);
    printf("YMAX: %d\tYMIN: %d\tOFF_MAX: %d\tNUM_!GREEN: %d\n", config->y_threshold_high, config->y_threshold_low,
           config->offset_max, config->num_not_green_max);
}

void log_frame(volatile unsigned short *frame_buffer) {
    FILE *cr_csv[4];
    FILE *cb_csv[4];
    FILE *y_csv[4];
    char cr_name[8] = "cr0.csv";
    char cb_name[8] = "cb0.csv";
    char y_name[8] = "cb0.csv";
    int i, j;
    for (i = 0; i < 4; i++) {
        cr_name[2] = i + '0';
        cb_name[2] = i + '0';
        y_name[2] = i + '0';
        cr_csv[i] = fopen(cr_name, "w");
        cb_csv[i] = fopen(cb_name, "w");
        y_csv[i] = fopen(y_name, "w");
    }
    int k;
    for (i = 0; i < 1080; i++) {
        for (j = 0; j < 1920; j++) {
            k = i * 4 / 1080;
            //Y
            if (i == 1079 && j == 1919) {
                fprintf(y_csv[k], "%d", LUMA_MASK & frame_buffer[i * 1920 + j]);
            } else {
                fprintf(y_csv[k], "%d,", LUMA_MASK & frame_buffer[i * 1920 + j]);
            }
            //CB
            if (j % 2) {
                if (i == 1079 && j == 1919) {
                    fprintf(cb_csv[k], "%d,%d", CRCB_MASK(frame_buffer[i * 1920 + j]),
                            CRCB_MASK(frame_buffer[i * 1920 + j]));
                } else {
                    fprintf(cb_csv[k], "%d,%d,", CRCB_MASK(frame_buffer[i * 1920 + j]),
                            CRCB_MASK(frame_buffer[i * 1920 + j]));
                }
            }
                //CR
            else {
                if (j == 1918) {
                    fprintf(cr_csv[k], "%d,%d", CRCB_MASK(frame_buffer[i * 1920 + j]),
                            CRCB_MASK(frame_buffer[i * 1920 + j]));
                } else {
                    fprintf(cr_csv[k], "%d,%d,", CRCB_MASK(frame_buffer[i * 1920 + j]),
                            CRCB_MASK(frame_buffer[i * 1920 + j]));
                }
            }
        }
        fprintf(cr_csv[k], "\n");
        fprintf(cb_csv[k], "\n");
        fprintf(y_csv[k], "\n");
    }
    for (i = 0; i < 4; i++) {
        fclose(cr_csv[i]);
        fclose(cb_csv[i]);
        fclose(y_csv[i]);
    }

}

void display_active_map(struct detect_config *config, volatile unsigned short *output, volatile unsigned short *input) {
    int i, j;
    for (i = 0; i < 1080; i++) {
        for (j = 0; j < 1920; j++) {
            short val = input[i * 1920 + j];
            //CB
            if (j % 2) {
                if (CRCB_MASK(val) < config->cb_threshold_high && CRCB_MASK(val) > config->cb_threshold_low) {
                    if ((LUMA_MASK & val) < config->y_threshold_high && (LUMA_MASK & val) > config->y_threshold_low) {
                        output[i * 1920 + j] = 0x00F0;
                    }
                }
            }
                //CR
            else {
                if (CRCB_MASK(val) < config->cr_threshold_high && CRCB_MASK(val) > config->cr_threshold_low) {
                    if ((LUMA_MASK & val) < config->y_threshold_high && (LUMA_MASK & val) > config->y_threshold_low) {
                        output[i * 1920 + j] = 0xFFF0;
                    }
                }
            }
        }
    }
}
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

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
#define BUTTONS_BASE_ADDR 	    0x41200000
#define SWITCHES_BASE_ADDR 	    0x41210000

//Picture Values
#define VDMA_FRAME_BUFFER_INPUT       0x1ABB0000 /*0x43000000 + 0x000000A0 + 0x0000000C*/
#define MEM_INTERFACE		"/dev/mem"
#define CRCB_MASK(val) (unsigned char)((val & 0xFF00) >> 8)
#define LUMA_MASK 0x00FF

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

void display_config(struct detect_config* config);
/**
 * @brief calculates the number of surronding pixels that fall into the tolerance values set in config
 * @param frame_buffer_ptr 1920x1080 ycbcr format 4:2:2 where frame_buffer_ptr[even] = cr/Y; framebuffer_ptr[odd] = cb/Y
 * @param i center pixel of surronding pixels to be checked
 * @param j center pixel of surronding pixels to be checked
 * @param config struct that holds threshold values. Set these high and low values to detect colors falling in that range. Ex: Green cbmin 0 cbmax 128 crmin 0 crmax 50
 * @return returns the number of pixels that were within tolerance bounds, until the number of out of bounds colors exceeds num_not_green_max
 */
int calc_largest_radius(volatile unsigned short* frame_buffer_ptr, int i, int j, struct detect_config* config);

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
        }

        else if (retval == 0) {
            fprintf(stdout, "Command busy, waiting...\n");
        }
        fprintf(stdout, "retval is %d\n", retval);
    }

    if (cmd == LAUNCHER_FIRE) {
        usleep(5000000);
    }
    return;
}

int main() {
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
    volatile unsigned short* frame_buffer_ptr = (unsigned short*)mmap(NULL, 1920*1080*2, PROT_READ, MAP_SHARED, mem_fd, VDMA_FRAME_BUFFER_INPUT);
    if(frame_buffer_ptr == MAP_FAILED){
        printf("MMAP FAIL\n");
        return -1;
    }

    printf("Hello, World!\n");
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
    int i_max, j_max, radius_max, i, j , i_examine, j_examine;
    i_max = 0;
    j_max = 0;
    radius_max = 0;
    int bytes_read = 0;
    while(-1 != bytes_read) {
        //Stop Config Phase
        if(config->num_not_green_max != -1){
            int a, b, c, d;
            bytes_read = scanf("%d %d %d %d %d %d", &a, &b, &c, &d, &j_examine, &i_examine);
            if(bytes_read == -1){
                break;
            }
            config->cb_threshold_high = a;
            config->cb_threshold_low = b;
            config->cr_threshold_high = c;
            config->cr_threshold_low = d;
            bytes_read = scanf("%d %d %d %d", &a, &b, &config->offset_max, &config->num_not_green_max);
            if(bytes_read == -1){
                break;
            }
            config->y_threshold_high = a;
            config->y_threshold_low = b;
            display_config(config);
        }
        else{
            for(i = i_max - 5; i < i_max + 5; i++){
                for(j = j_max - 5; j<j_max+5; j++){
                    if(i > 0 && i < 1080 && j > 0 && j < 1920){
                        frame_buffer_ptr[i*1920+j] = 0xFF00;
                    }
                }
            }
        }
        //Run Alg on one frame
        //Assuming Green target for now
        //Assuming pointer assignment is correct
        unsigned char cb, cr, y, y1;

        int temp_radius= 0;
        radius_max = 0;
        //Every 4th Row
        for(i = 0; i< 1080; i++){
            //Every 8th Col_ish
            for(j=210; j < 1920-210; j+=2){
                cr = CRCB_MASK(frame_buffer_ptr[i*1920+j]);
                cb = CRCB_MASK(frame_buffer_ptr[i*1920+j+1]);
                y = LUMA_MASK & frame_buffer_ptr[i*1920+j];
                y1 = LUMA_MASK & frame_buffer_ptr[i*1920+j+1];
                //Pixel debug
                if(i < i_examine + 2 && i > i_examine - 2 && j > j_examine -2 && j < j_examine +2){
                    printf("CRVAL: %d\tCBVAL: %d\tYVAL: %d\tY1VAL: %d\nX: %d, Y: %d\n", cr, cb, y, y1, j_examine, i_examine);
                }
                //Green pixel
                if(cr < config->cr_threshold_high && cr > config->cr_threshold_low && cb < config->cb_threshold_high && cb > config->cb_threshold_low){
                    if(y < config->y_threshold_high && y > config->y_threshold_low){
                        temp_radius = calc_largest_radius(frame_buffer_ptr, i, j, config);
                        if( temp_radius > radius_max){
                            i_max = i;
                            j_max = j;
                            radius_max = temp_radius;
                        }
                    }
                }
            }
        }
        printf("Center is x: %d, y: %d\n", j_max, i_max);
    }
    //Jmax and Imax should be center of target try to move in direction that moves imax and jmax to center j1920/2 + i1080/2
    close(fd);
    return EXIT_SUCCESS;
}

int calc_largest_radius(volatile unsigned short* frame_buffer_ptr, int i, int j, struct detect_config* config){
    int num_green_pixels = 0;
    int num_not_green = 0;
    int x,y;
    int offset = 1;
    while (offset < config->offset_max){
        //Left and Right Columns
        for(y = i - offset; y <= i + offset; y++){
            int index1, index2;
            index1 = y*1920+j+offset;
            index2 = y*1920+j-offset;
            //Valid bounds check;
            if(index1 > 0 && index1 < 1920*1080){
                //CB
                if(index1 % 2){
                    //If outside color bounds
                    if(CRCB_MASK(frame_buffer_ptr[index1]) > config->cb_threshold_high || CRCB_MASK(frame_buffer_ptr[index1]) < config->cb_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
                //CR
                else{
                    if(CRCB_MASK(frame_buffer_ptr[index1]) > config->cr_threshold_high || CRCB_MASK(frame_buffer_ptr[index1]) < config->cr_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
            }
            if(index2 > 0 && index2 < 1920*1080){
                //CB
                if(index2 % 2){
                    //If outside color bounds
                    if(CRCB_MASK(frame_buffer_ptr[index2]) > config->cb_threshold_high || CRCB_MASK(frame_buffer_ptr[index2]) < config->cb_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
                //CR
                else{
                    if(CRCB_MASK(frame_buffer_ptr[index2]) > config->cr_threshold_high || CRCB_MASK(frame_buffer_ptr[index2]) < config->cr_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
            }
        }
        //Top and Bottom Row
        for(x = j-offset; x <= j + offset; x++){
            int index1, index2;
            index1 = (i+offset)*1920+x;
            index2 = (i-offset)*1920+x;
            //Valid bounds check;
            if(index1 > 0 && index1 < 1920*1080){
                //CB
                if(index1 % 2){
                    //If outside color bounds
                    if(CRCB_MASK(frame_buffer_ptr[index1]) > config->cb_threshold_high || CRCB_MASK(frame_buffer_ptr[index1]) < config->cb_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
                //CR
                else{
                    if(CRCB_MASK(frame_buffer_ptr[index1]) > config->cr_threshold_high || CRCB_MASK(frame_buffer_ptr[index1]) < config->cr_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index1] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index1] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
            }
            if(index2 > 0 && index2 < 1920*1080){
                //CB
                if(index2 % 2){
                    //If outside color bounds
                    if(CRCB_MASK(frame_buffer_ptr[index2]) > config->cb_threshold_high || CRCB_MASK(frame_buffer_ptr[index2]) < config->cb_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
                //CR
                else{
                    if(CRCB_MASK(frame_buffer_ptr[index2]) > config->cr_threshold_high || CRCB_MASK(frame_buffer_ptr[index2]) < config->cr_threshold_low){
                        num_not_green++;
                    }
                    //Y
                    else if((frame_buffer_ptr[index2] & LUMA_MASK) > config->y_threshold_high || (frame_buffer_ptr[index2] & LUMA_MASK) < config->y_threshold_low){
                        num_not_green++;
                    }
                    //Else count another green pixel
                    else{
                        num_green_pixels++;
                    }
                }
            }
        }
        if(num_not_green > config->num_not_green_max){
            return num_green_pixels;
        }
        offset++;
    }
    return num_green_pixels;
}

void display_config(struct detect_config* config){
    printf("CBMAX: %d\tCBMIN: %d\tCRMAX: %d\t\tCRMIN: %d\n", config->cb_threshold_high, config->cb_threshold_low, config->cr_threshold_high, config->cr_threshold_low);
    printf("YMAX: %d\tYMIN: %d\tOFF_MAX: %d\tNUM_!GREEN: %d\n", config->y_threshold_high, config->y_threshold_low, config->offset_max, config->num_not_green_max);
}
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
#define VDMA_FRAME_BUFFER       0x43000000 + 0x000000A0 + 0x0000000C
#define THRESHOLD_HIGH            0xF0
#define THRESHOLD_LOW             0x40
#define MAX_NUM_NOT_GREEN         5
#define MEM_INTERFACE		"/dev/mem"


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
    int mem_fd = open(MEM_INTERFACE, O_RDWR);
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
    volatile unsigned short* frame_buffer_ptr = (unsigned short*)mmap(NULL, 1920*1080, PROT_READ, MAP_SHARED, mem_fd, VDMA_FRAME_BUFFER);
    if(frame_buffer_ptr == MAP_FAILED){
        printf("MMAP FAIL\n");
        return -1;
    }
    //Assuming Green target for now
    //Assuming pointer assignment is correct
    unsigned short pixels[1920*1080];
    unsigned char cb, cr, y;
    int i, j;
    int i_max, j_max, radius_max;
    radius_max = 0;
    //Every 4th Row
    for(i = 0; i< 1080; i+=4){
        //Every 8th Col_ish
        for(j=0; j <1920; j+=8){
            cr = 0xFF00 & pixels[i*1920+j];
            cb = 0xFF00 & pixels[i*1920+j+1];
            //Green pixel
            if(cr < THRESHOLD_LOW && cb < THRESHOLD_LOW){
                if(calc_largest_radius(frame_buffer_ptr, i, j) > radius_max){
                    i_max = i;
                    j_max = j;
                }
            }
        }
    }
    printf("Calculated center of target %d %d", i_max ,j_max);
    //Jmax and Imax should be center of target try to move in direction that moves imax and jmax to center j1920/2 + i1080/2
    close(fd);

    return EXIT_SUCCESS;
}

int calc_largest_radius(volatile unsigned short* frame_buffer_ptr, int i, int j){
    int num_green_pixels = 0;
    int num_not_green = 0;
    int x,y;
    int offset = 1;
    while (offset < 500){
        for(y = i - offset; y <= i + offset; y++){
            //If not green
            if((frame_buffer_ptr[y*1920+j+offset] & 0xF0) > THRESHOLD_LOW){
                num_not_green++;
            }
            else if((frame_buffer_ptr[y*1920+j-offset] & 0xF0) > THRESHOLD_LOW){
                num_not_green++;
            }
            else{
                num_green_pixels++;
            }
        }
        for(y = j-offset; y <= j + offset; y++){
            //If not green
            if((frame_buffer_ptr[y+(i+offset)*1920] & 0xF0) > THRESHOLD_LOW){
                num_not_green++;
            }
            else if((frame_buffer_ptr[y+(j-offset)*1920] & 0xF0) > THRESHOLD_LOW){
                num_not_green++;
            }
            else{
                num_green_pixels++;
            }
        }
        if(num_not_green > MAX_NUM_NOT_GREEN){
            return num_green_pixels;
        }
        offset++;
    }
    return num_green_pixels;

}
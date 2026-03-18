#include "address_map.h"

#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240

volatile short int* pixel_buf_address = 0x08000000;

void otsu_algorithm();

int main(void) {
  // pointers to video-in DMA controller registers
  volatile int* video_in_dma = (int*)VIDEO_IN_BASE;         // 0xFF203060
  volatile int* edge_detect = (int*)EDGE_DETECT_CTRL_BASE;  // 0xFF203070

  // pointer to pixel buffer controller (video-out)
  volatile int* pixel_buf_ctrl = (int*)PIXEL_BUF_CTRL_BASE;  // 0xFF203020
  volatile int *KEY_ptr  = (int *)KEY_BASE;


    int edge_capture;
    // dlear edge capture register initially
    *(KEY_ptr + 3) = 0xF;

  // : det both video-in DMA and video-out to use the same buffer
  // default buffer address is 0x08000000 (FPGA on-chip memory)


  // direct video feed from camera --> vga
  *(video_in_dma + 1) = 0x08000000;  // set BackBuffer to on-chip memory


  // disable edge detection
  *edge_detect = 0x0;

  //enable the Video DMA controller (set EN bit)
  // cntrol register is at 0xFF20306C (offset 3 from VIDEO_IN_BASE)
  *(video_in_dma + 3) = 0x4;  // EN = 1
 
  // wriitng 1 to Buffer register triggers display
  *pixel_buf_ctrl = 1;

  while (1) {
        // 
        edge_capture = *(KEY_ptr + 3);  //edge capture is 0xFF20005C which is 3rd register from base


        if (edge_capture & 0x1) {  //check if KEY0 (first bit of register) was pressed and released
          *(KEY_ptr + 3) = 0x1;  //clear KEY0 edge capture bit by writing 1 to first bit
          *(video_in_dma + 3) = 0x0; // takes a pic
          otsu_algorithm();

          int count = 9000000;
          for (int i=0; i < count; i++);
          *(video_in_dma + 3) = 0x4; // going back to video mode

        }

    }


  return 0;
}


void otsu_algorithm() {


  int total = IMAGE_HEIGHT * IMAGE_WIDTH;

      short image[total];

      for (int x = 0; x < 320; x++) {
        for (int y = 0; y < 240; y++) {
          //want to transfer data from backbuffer to array
          image[y*IMAGE_WIDTH+ x] = *(pixel_buf_address + (y << 9) + (x));
        }
      }

      //colour histogram (after greyscaling)

      int colour_histogram[256];

      for (int i = 0; i < 256; i++) {
          colour_histogram[i] = 0;
      }

      for (int i = 0; i < total; i ++) {

          short pixel = image[i];

          // Extract RGB channels from RGB565
          int r = (pixel >> 11) & 0x1F;        // 5 bits → 0-31
          int g = (pixel >> 5)  & 0x3F;        // 6 bits → 0-63
          int b =  pixel        & 0x1F;        // 5 bits → 0-31

          // Scale to 8-bit (0-255)
          r = (r * 255) / 31;
          g = (g * 255) / 63;
          b = (b * 255) / 31;

          unsigned char grey = (unsigned char)((77*r + 150*g + 29*b) >> 8);

          image[i] = (short)grey;

          colour_histogram[grey]++;
    }

    long sum = 0;

    for (int i = 0; i < 256; i++) {

      //calculate weighted sum
      sum += i * colour_histogram[i];
    }

    long num_background_pixels = 0;
    long background_intensity_sum = 0;
    long max_variance = 0;
    short otsu_threshold = 0;

    //evaluate each possible threshhold value 
    //want to maximize the variance between background/foreground

    for (int t = 0; t < 256; t ++) {
          
      num_background_pixels += colour_histogram[t];
      background_intensity_sum += (long) (t* colour_histogram[t]);

      long num_foreground_pixels = total - num_background_pixels;
      
      if (num_background_pixels == 0 || num_foreground_pixels == 0) continue; //avoiding dividing by zero, continue to next t count

      long background_avg = background_intensity_sum / num_background_pixels;
      long foreground_avg = (sum - background_intensity_sum) / num_foreground_pixels;

      long mean_difference = background_avg - foreground_avg;
      long between_class_var = num_background_pixels * num_foreground_pixels * mean_difference * mean_difference;

      if (between_class_var > max_variance) {
        max_variance = between_class_var;
        otsu_threshold = t;
      }
    }

    for (int i = 0; i < IMAGE_HEIGHT * IMAGE_WIDTH; i++) {
      image[i] = (image[i] <= otsu_threshold) ? 0x0000 : 0xFFFF;
    }

    //set the video out buffer to our image array

    //rewrite processed pixels into the frame buffer
    for (int x = 0; x < 320; x++) {
        for (int y = 0; y < 240; y++) {
            //want to transfer data from backbuffer to array
            *(pixel_buf_address + (y << 9) + (x)) = image[y*IMAGE_WIDTH + x];
        }
    }

  
}

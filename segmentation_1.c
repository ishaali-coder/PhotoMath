#include "address_map.h"
#include <stdio.h>

#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240
#define MAX_SYMBOLS 10
#define NORM_SIZE 28
#define Y_START 11
#define EDGE_IGNORE 10

typedef struct {
  int x_min, x_max;  //left and right edge
  int y_min, y_max;  //top and bottom edge
} BoundingBox;

short symbols[MAX_SYMBOLS][NORM_SIZE * NORM_SIZE];

volatile short int* pixel_buf_address = (short int*)0x08000000;

int otsu_algorithm(short* image, BoundingBox* boxes);
void median_filter(short int* buffer);
int segment_symbols(short int* image, BoundingBox* boxes);
void display_cropped_symbol(short* image, BoundingBox box);
void normalize_symbol(short int* image, BoundingBox box, short* output);
void plot_pixel(int x, int y, short color);
void clear_screen();
void display_symbol(short* symbol);
void delay();

int main(void) {
  // pointers to video-in dma controller
  volatile int* video_in_dma = (int*)VIDEO_IN_BASE;         // 0xFF203060
  volatile int* edge_detect = (int*)EDGE_DETECT_CTRL_BASE;  // 0xFF203070
  // pointer to video out pixel buffer controller
  volatile int* pixel_buf_ctrl = (int*)PIXEL_BUF_CTRL_BASE;  // 0xFF203020
  volatile int* KEY_ptr = (int*)KEY_BASE;

  *(KEY_ptr + 3) = 0xF;  // clear edge capture register initially

  // direct video feed from camera ->vga
  *(video_in_dma + 1) = 0x08000000;  // set camera to write directly to VGA; buffer 0x08 default adress
  *edge_detect = 0x0;                // disable edge detection

  // enable the video dma controller (set en bit), EN = 1
  //  cntrol register is at 0xFF20306C (offset 3 from VIDEO_IN_BASE)
  *(video_in_dma + 3) = 0x4;
  *pixel_buf_ctrl = 1;  // wriitng 1 to Buffer register triggers display - enable VGA output

  while (1) {
    int edge_capture = *(KEY_ptr + 3);
    if (edge_capture & 0x1) {  // check if key0 was pressed and released
      *(KEY_ptr + 3) = 0x1;  // clearedge capture bit by writing 1 to first bit
      *(video_in_dma + 3) = 0x0;  // takes a pic (enable goes from 1--> 0)
      
      BoundingBox boxes[MAX_SYMBOLS];
      short image[IMAGE_WIDTH * IMAGE_HEIGHT];

      int num_symbols = otsu_algorithm(image, boxes);
      //now show cropped pieces
      for (int i = 0; i < num_symbols; i++) {
        normalize_symbol(image, boxes[i], symbols[i]);
        display_cropped_symbol(image, boxes[i]);
        delay();
      }

      *(video_in_dma + 3) = 0x4;  // going back to video mode
    }
  }
  return 0;
}

int otsu_algorithm(short* image, BoundingBox* boxes) {                     // returns number of symbols found
  int total = IMAGE_HEIGHT * IMAGE_WIDTH;  // number of pixels
  
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
        if (y < Y_START || x < EDGE_IGNORE || x > IMAGE_WIDTH - EDGE_IGNORE - 1 || y > IMAGE_HEIGHT-1) {
            image[y * IMAGE_WIDTH + x] = 0xFFFF;  // force band to white so it can be used as background
        } else {
            image[y * IMAGE_WIDTH + x] = *(pixel_buf_address + (y << 9) + x);
    
        }
    }
  }

  // colour histogram
  int colour_histogram[256] = {0};
  //grayscale
  for (int i = 0; i < total; i++) {
    short pixel = image[i];
    // extract rgb values  from 16bit
    int r = (pixel >> 11) & 0x1F;  // 5 bits → 0-31
    int g = (pixel >> 5) & 0x3F;   // 6 bits → 0-63
    int b = pixel & 0x1F;          // 5 bits → 0-31
    // scale to 8-bit (0-255)
    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;

    unsigned char grey = (unsigned char)((77 * r + 150 * g + 29 * b) >>8);  // use shifts instead of divides for processor ease
    image[i] = (short)grey;  // replace old pixel with new greyscaled version
    colour_histogram[grey]++;
  }
  median_filter(image);

  long sum = 0;
  for (int i = 0; i < 256; i++) {
    // calculate weighted sum
    sum += (long)i * colour_histogram[i];
  }

  long num_background_pixels = 0;
  long background_intensity_sum = 0;
  long max_variance = 0;
  short otsu_threshold = 0;

  // evaluate each possible threshhold value
  // want to maximize the variance between background/foreground
  for (int t = 0; t < 256; t++) {
    num_background_pixels += colour_histogram[t];
    background_intensity_sum += (long)(t * colour_histogram[t]);
    long num_foreground_pixels = total - num_background_pixels;  // foreground = diff between total and background

    if (num_background_pixels == 0 || num_foreground_pixels == 0) continue;  // avoiding dividing by zero, continue to next t count
    
    long background_avg = background_intensity_sum / num_background_pixels;
    long foreground_avg = (sum - background_intensity_sum) / num_foreground_pixels;
    
    long mean_difference = background_avg - foreground_avg;
    long between_class_var = num_background_pixels * num_foreground_pixels * mean_difference * mean_difference;  // otsuy alogorithm formula
    if (between_class_var > max_variance) {
      max_variance = between_class_var;
      otsu_threshold = t;
    }
  }
  //B&W image
  for (int i = 0; i < total; i++) {
  
    image[i] = (image[i] <= otsu_threshold) ? 0x0000 : 0xFFFF;
  }
  //Display black & white image
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {

      if (x == 0 || y < Y_START || y == 239 || x == 319) {
        *(pixel_buf_address + (y << 9) + x) = 0xFFFF;
      } else {
        *(pixel_buf_address + (y << 9) + x) = image[y * IMAGE_WIDTH + x];
      }
    }
  }
  delay(); 
  //segmentation
  return segment_symbols(image, boxes);
}

void median_filter(short int* buffer) {  // removes noise from binary image
  // collects the 3x3 neighborhood values, sorts them, and replaces the center
  // pixel with the median value.
  //  Temporary buffer to store filtered result
  int temp[IMAGE_WIDTH * IMAGE_HEIGHT];         // temporary buffer
  for (int y = 1; y < IMAGE_HEIGHT - 1; y++) {  // avoid borders
    for (int x = 1; x < IMAGE_WIDTH - 1; x++) {
      int values[9];  // store 9 pixels
      int k = 0;

      // Collect 3x3 neighborhood
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          values[k] = buffer[(y + dy) * IMAGE_WIDTH + (x + dx)];  // take pixel at (x+dx, y+dy) and store it in array
          k = k + 1; // get surrounding pixels
        }
      }

      // Sort 9 values (simple bubble sort)
      for (int i = 0; i < 9; i++) {
        for (int j = i + 1; j < 9; j++) {
          if (values[j] < values[i]) {
            int temp_val = values[i];
            values[i] = values[j];
            values[j] = temp_val;
          }
        }
      }
      // Take median (middle value)
      temp[y * IMAGE_WIDTH + x] = values[4];
    }
  }

  // Copy back to original buffer to replace orginal image
  for (int i = 0; i < IMAGE_WIDTH * IMAGE_HEIGHT; i++) {
    buffer[i] = (short)temp[i];
  }
}

int segment_symbols(short int* image, BoundingBox* boxes) {
  int count = 0;  // Keeps track of how many symbols we have found so far
  int in_symbol = 0;  // Flag: 0 = currently NOT inside a symbol, 1 = inside a symbol
  int gap_count = 0;
  int GAP_THRESHOLD = 8;

  // Loop through each column of the image (left to right)
  for (int x = EDGE_IGNORE; x < IMAGE_WIDTH - EDGE_IGNORE; x++) {
    int has_pixel = 0;  // Will become 1 if we find any BLACK pixel in this column
    // Scan down the column (top to bottom)
    for (int y = Y_START; y < IMAGE_HEIGHT; y++) {
      // Check if pixel is BLACK (digit = foreground)
      if (image[y * IMAGE_WIDTH + x] == 0x0000) {
        has_pixel = 1;  // This column contains part of a symbol
        break;          // No need to check further pixels in this column
      }
    }

    // CASE 1: We found a column with a pixel AND we were NOT already inside a
    // symbol
    if (has_pixel && !in_symbol) {
      boxes[count].x_min = x;  // Mark the LEFT boundary of a new symbol
      in_symbol = 1;           // We are now inside a symbol
      gap_count = 0;
    }
    // CASE 2: We found an empty column AND we WERE inside a symbol
    else if (!has_pixel && in_symbol) {
      gap_count++;
      if (gap_count >= GAP_THRESHOLD) {
        boxes[count].x_max = x - gap_count;    // Mark the RIGHT boundary of the symbol
        int y_min = IMAGE_HEIGHT;  // Initialize top boundary very large
        int y_max = Y_START;             // Initialize bottom boundary TO END OF STRIPE
        // Now find TOP and BOTTOM of this symbol
        for (int xx = boxes[count].x_min; xx <= boxes[count].x_max; xx++) {
          for (int y = Y_START; y < IMAGE_HEIGHT; y++) {
            // Check for BLACK pixels again
            if (image[y * IMAGE_WIDTH + xx] == 0x0000) {
              if (y < y_min) y_min = y;  // Update TOP boundary
              if (y > y_max) y_max = y;  // Update BOTTOM boundary
            }
          }
        }

        boxes[count].y_min = y_min;       // Save TOP boundary
        boxes[count].y_max = y_max;       // Save BOTTOM boundary
        count++;                          // Move to next symbol
        in_symbol = 0;   // We are no longer inside a symbol
        gap_count = 0;
        if (count >= MAX_SYMBOLS) break;  // Prevent overflow
      }
    }
    // CASE 3: in a symbol and found a pixel
    else if (has_pixel && in_symbol) {
        //reset the gap count
        gap_count = 0;
    }
  } 
  //handle symbol that reaches he right edge of the image 
  if (in_symbol && count < MAX_SYMBOLS) {
    boxes[count].x_max = IMAGE_WIDTH - 1;

    int y_min = IMAGE_HEIGHT;
    int y_max = 0;

    for (int xx = boxes[count].x_min; xx <= boxes[count].x_max; xx++) {
      for (int y = Y_START; y < IMAGE_HEIGHT; y++) {
        if (image[y * IMAGE_WIDTH + xx] == 0x0000) {
          if (y < y_min) y_min = y;
          if (y > y_max) y_max = y;
        }
      }
    }

    boxes[count].y_min = y_min;
    boxes[count].y_max = y_max;
    count++;
  }

  return count;  // Return number of symbols detected
}

void display_cropped_symbol(short* image, BoundingBox box) { //function takes binary image and coordinates of one detected symbol
  clear_screen();

  int symbol_width = box.x_max - box.x_min + 1;  //calculates width of symbol; +1 beacuse pixel indices are inclusive 
  int symbol_height = box.y_max - box.y_min + 1;  //height of symbol

  //Find centre of screen (160, 120)
  int screen_center_x = IMAGE_WIDTH / 2;
  int screen_center_y = IMAGE_HEIGHT / 2;

  //compute top left of symbol so its centered
  int offset_x = screen_center_x - symbol_width / 2;  //left position = centre - half width so midle aligns with screen centre
  int offset_y = screen_center_y - symbol_height / 2;  

  for (int y = box.y_min; y <= box.y_max; y++) {  //loop thorught symbols vertical and horizontal region
    for (int x = box.x_min; x <= box.x_max; x++) {
      short pixel = image[y * IMAGE_WIDTH + x];  //get pixel value from original image

      int draw_x = offset_x + (x - box.x_min);  //repositioning pixel
      int draw_y = offset_y + (y - box.y_min);

      plot_pixel(draw_x, draw_y, pixel); //draw pixel at new position
    }
  }
}

void normalize_symbol(short int* image, BoundingBox box, short* output) {
  /*Takes a detected symbol (bounding box) and rescales it to a fixed size
  (28x28). This ensures all symbols are uniform for template matching */
  
  int padding = 3;

  int x_min = box.x_min - padding;
  int x_max = box.x_max + padding;
  int y_min = box.y_min - padding;
  int y_max = box.y_max + padding;

  // clamp to image bounds
  if (x_min < 0) x_min = 0;
  if (y_min < 0) y_min = 0;
  if (x_max >= IMAGE_WIDTH) x_max = IMAGE_WIDTH - 1;
  if (y_max >= IMAGE_HEIGHT) y_max = IMAGE_HEIGHT - 1;

  int width = x_max - x_min + 1;
  int height = y_max - y_min + 1;
  int size = (width > height) ? width : height;
  //clear output (white background)
  for (int i = 0; i < NORM_SIZE * NORM_SIZE; i++) {
    output[i] = 0xFFFF;
  }
  //center offsets
  int x_offset = (size - width) / 2;
  int y_offset = (size - height) / 2;
  
  for (int y = 0; y < NORM_SIZE; y++) {    // loop over ouput image (20x20 grid)
    for (int x = 0; x < NORM_SIZE; x++) {
      int src_x = x_min + (x * size / NORM_SIZE) - x_offset;  // map normalized x -> orginal image x
      int src_y = y_min + (y * size / NORM_SIZE) - y_offset;  // map normalized y -> orginal image y
      if (src_x >= x_min && src_x <= x_max && src_y >= y_min && src_y <= y_max) {
        short pixel = image[src_y * IMAGE_WIDTH + src_x];
        // keep BLACK digit on WHITE background
        output[y * NORM_SIZE + x] = pixel;
      } else {
        output[y * NORM_SIZE + x] = 0xFFFF;
      }
    }
  }
}

void plot_pixel(int x, int y, short color) {
  volatile short* pixel_buffer = (short*)0x08000000;
  *(pixel_buffer + (y << 9) + x) = color;
}

void clear_screen() {
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      plot_pixel(x, y, 0xFFFF);  // white background
    }
  }
}

void display_symbol(short* symbol) {
  clear_screen();

  int scale = 8;  // size of each pixel block

  for (int y = 0; y < NORM_SIZE; y++) {
    for (int x = 0; x < NORM_SIZE; x++) {
      short pixel = symbol[y * NORM_SIZE + x];

      // scale each pixel
      for (int dy = 0; dy < scale; dy++) {
        for (int dx = 0; dx < scale; dx++) {
          int sx = x * scale + 80;  // center X
          int sy = y * scale + 40;  // center Y

          plot_pixel(sx + dx, sy + dy, pixel);
        }
      }
    }
  }
}

void delay() { for (volatile int i = 0; i < 20000000; i++); }

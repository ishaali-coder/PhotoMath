#include "address_map.h"
#include "templates.h"

#include <stdio.h>

//constants
#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240
#define MAX_SYMBOLS 1
#define NORM_SIZE 28
#define Y_START 11
#define EDGE_IGNORE 10
#define TOTAL 320 * 240
#define TEMPLATES_PER_DIGIT 5
#define CURRENT_SYMBOL "2"
#define SYM_START_I 8
#define SYM_END_I 9
#define SEARCH_RANGE 4


//symbol box definition
typedef struct {
  int x_min, x_max;  // left and right edge
  int y_min, y_max;  // top and bottom edge
} BoundingBox;

//globals
short symbols[MAX_SYMBOLS][NORM_SIZE * NORM_SIZE];
volatile short int* pixel_buf_address = (short int*)0x08000000;
// 7-segment encoding for digits 0-9
// Segments: bit 0=a, 1=b, 2=c, 3=d, 4=e, 5=f, 6=g (0 = ON, 1 = OFF for common anode)
const unsigned char seven_seg[10] = {
    0x3F, // 0: segments a,b,c,d,e,f ON
    0x06, // 1: segments b,c ON
    0x5B, // 2: segments a,b,d,e,g ON
    0x4F, // 3: segments a,b,c,d,g ON
    0x66, // 4: segments b,c,f,g ON
    0x6D, // 5: segments a,c,d,f,g ON
    0x7D, // 6: segments a,c,d,e,f,g ON
    0x07, // 7: segments a,b,c ON
    0x7F, // 8: all segments ON
    0x6F  // 9: segments a,b,c,d,f,g ON
};


//function declarations
int otsu_algorithm(short* image, BoundingBox* boxes);
void median_filter(short int* buffer);
int segment_symbols(short int* image, BoundingBox* boxes);
//void display_cropped_symbol(short* image, BoundingBox box);
void normalize_symbol(short int* image, BoundingBox box, short* output);
void plot_pixel(int x, int y, short color);
void clear_screen();
void display_symbol(short* symbol);
void delay();
float jaccard_similarity(short* image, const Template* template, int i);
float best_template_match(short* image, int starting_index, int final_index);
void display_int_on_hex(int value);

int main(void) {
  // pointers to video-in dma controller
  volatile int* video_in_dma = (int*)VIDEO_IN_BASE;         // 0xFF203060
  volatile int* edge_detect = (int*)EDGE_DETECT_CTRL_BASE;  // 0xFF203070
  // pointer to video out pixel buffer controller
  volatile int* pixel_buf_ctrl = (int*)PIXEL_BUF_CTRL_BASE;  // 0xFF203020
  volatile int* KEY_ptr = (int*)KEY_BASE;


  *(KEY_ptr + 3) = 0xF;  // clear edge capture register initially


  // direct video feed from camera ->vga
  *(video_in_dma + 1) = 0x08000000;  // set camera to write directly to VGA;
                                     // buffer 0x08 default adress
  *edge_detect = 0x0;                // disable edge detection


  // enable the video dma controller (set en bit), EN = 1
  //  cntrol register is at 0xFF20306C (offset 3 from VIDEO_IN_BASE)
  *(video_in_dma + 3) = 0x4;
  *pixel_buf_ctrl = 1;  // wriitng 1 to Buffer register triggers display - enable VGA output

  display_int_on_hex(0);
  

  while (1) {
    int edge_capture = *(KEY_ptr + 3);

    if (edge_capture & 0x1) {  // check if key0 was pressed and released
      *(KEY_ptr + 3) = 0x1;  // clearedge capture bit by writing 1 to first bit
      *(video_in_dma + 3) = 0x0;  // takes a pic (enable goes from 1--> 0)

      BoundingBox boxes[MAX_SYMBOLS];
      short image[TOTAL];

      int num_symbols = otsu_algorithm(image, boxes);

      short normalized[NORM_SIZE * NORM_SIZE];

       //now show cropped pieces
      for (int i = 0; i < num_symbols; i++) {
        normalize_symbol(image, boxes[i], normalized);
        display_symbol(normalized);
        delay();
      }

      while (!(*(KEY_ptr + 3) & 0x2) && !(*(KEY_ptr + 3) & 0x4));

      //if KEY1 is pressed then keep this image
      //if KEY2 is pressed go back and try again
      if (*(KEY_ptr + 3) & 0x2) {

        //find highest jacccard similarity with the image and each 1 template
        float score_1 =  best_template_match(normalized, 0, 5);
        float score_2 =  best_template_match(normalized, 5, 10);

        //printf("Score 1: %f, Score 2: %f\n", score_1, score_2);
        if (score_1 > score_2) {
            display_int_on_hex(1);
        } else if (score_2 > score_1) {
            display_int_on_hex(2);
        } else {
            display_int_on_hex(-1);
        }
        delay();    
      } 

      *(KEY_ptr + 3) = 0xF; //clear all keys 

      *(video_in_dma + 3) = 0x4;  // going back to video mode


    }
    display_int_on_hex(0);

    }
    return 0;
}

int otsu_algorithm(short* image, BoundingBox* boxes) {  // returns number of symbols found


  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      if (y < Y_START || x < EDGE_IGNORE || x > IMAGE_WIDTH - EDGE_IGNORE - 1 ||
          y >= IMAGE_HEIGHT - 1) {
        image[y * IMAGE_WIDTH + x] =
            0xFFFF;  // force band to white so it can be used as background
      } else {
        image[y * IMAGE_WIDTH + x] = *(pixel_buf_address + (y << 9) + x);
      }
    }
  }


  // colour histogram
  int colour_histogram[256] = {0};
  // grayscale
  for (int i = 0; i < TOTAL; i++) {
    short pixel = image[i];
    // extract rgb values  from 16bit
    int r = (pixel >> 11) & 0x1F;  // 5 bits → 0-31
    int g = (pixel >> 5) & 0x3F;   // 6 bits → 0-63
    int b = pixel & 0x1F;          // 5 bits → 0-31
    // scale to 8-bit (0-255)
    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;


    unsigned char grey =
        (unsigned char)((77 * r + 150 * g + 29 * b) >>
                        8);  // use shifts instead of divides for processor ease
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
    long num_foreground_pixels =
        TOTAL - num_background_pixels;  // foreground = diff between total and
                                        // background


    if (num_background_pixels == 0 || num_foreground_pixels == 0)
      continue;  // avoiding dividing by zero, continue to next t count


    long background_avg = background_intensity_sum / num_background_pixels;
    long foreground_avg =
        (sum - background_intensity_sum) / num_foreground_pixels;


    long mean_difference = background_avg - foreground_avg;
    long between_class_var = num_background_pixels * num_foreground_pixels *
                             mean_difference *
                             mean_difference;  // otsuy alogorithm formula
    if (between_class_var > max_variance) {
      max_variance = between_class_var;
      otsu_threshold = t;
    }
  }
  // B&W image
  for (int i = 0; i < TOTAL; i++) {
    image[i] = (image[i] <= otsu_threshold) ? 0x0000 : 0xFFFF;
  }
  // Display black & white image
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      if (x < EDGE_IGNORE || y < Y_START || y >= IMAGE_HEIGHT - 1 || x >= IMAGE_WIDTH - EDGE_IGNORE) {
        *(pixel_buf_address + (y << 9) + x) = 0xFFFF;
      } else {
        *(pixel_buf_address + (y << 9) + x) = image[y * IMAGE_WIDTH + x];
      }
    }
  }
  delay();
  // segmentation
  return segment_symbols(image, boxes);
  //return 0;
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
          values[k] = buffer[(y + dy) * IMAGE_WIDTH +
                             (x + dx)];  // take pixel at (x+dx, y+dy) and store
                                         // it in array
          k = k + 1;                     // get surrounding pixels
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
  int in_symbol =
      0;  // Flag: 0 = currently NOT inside a symbol, 1 = inside a symbol
  int gap_count = 0;
  int GAP_THRESHOLD = 8;


  // Loop through each column of the image (left to right)
  for (int x = EDGE_IGNORE; x < IMAGE_WIDTH - EDGE_IGNORE; x++) {
    int has_pixel =
        0;  // Will become 1 if we find any BLACK pixel in this column
    // Scan down the column (top to bottom)
    for (int y = Y_START; y < IMAGE_HEIGHT - 1; y++) {
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
        boxes[count].x_max =
            x - gap_count;         // Mark the RIGHT boundary of the symbol
        int y_min = IMAGE_HEIGHT;  // Initialize top boundary very large
        int y_max = Y_START;  // Initialize bottom boundary TO END OF STRIPE
        // Now find TOP and BOTTOM of this symbol
        for (int xx = boxes[count].x_min; xx <= boxes[count].x_max; xx++) {
          for (int y = Y_START; y < IMAGE_HEIGHT - 1; y++) {
            // Check for BLACK pixels again
            if (image[y * IMAGE_WIDTH + xx] == 0x0000) {
              if (y < y_min) y_min = y;  // Update TOP boundary
              if (y > y_max) y_max = y;  // Update BOTTOM boundary
            }
          }
        }


        boxes[count].y_min = y_min;  // Save TOP boundary
        boxes[count].y_max = y_max;  // Save BOTTOM boundary
        count++;                     // Move to next symbol
        in_symbol = 0;               // We are no longer inside a symbol
        gap_count = 0;
        if (count >= MAX_SYMBOLS) break;  // Prevent overflow
      }
    }
    // CASE 3: in a symbol and found a pixel
    else if (has_pixel && in_symbol) {
      // reset the gap count
      gap_count = 0;
    }
  }
  // handle symbol that reaches he right edge of the image
  if (in_symbol && count < MAX_SYMBOLS) {
    boxes[count].x_max = IMAGE_WIDTH - 1;


    int y_min = IMAGE_HEIGHT;
    int y_max = 0;


    for (int xx = boxes[count].x_min; xx <= boxes[count].x_max; xx++) {
      for (int y = Y_START; y < IMAGE_HEIGHT - 1; y++) {
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

void normalize_symbol(short int* image, BoundingBox box, short* output) {
    int padding = 3;

    int x_min = box.x_min - padding;
    int x_max = box.x_max + padding;
    int y_min = box.y_min - padding;
    int y_max = box.y_max + padding;

    if (x_min < 0) x_min = 0;
    if (y_min < 0) y_min = 0;
    if (x_max >= IMAGE_WIDTH)  x_max = IMAGE_WIDTH - 1;
    if (y_max >= IMAGE_HEIGHT) y_max = IMAGE_HEIGHT - 1;

    int width  = x_max - x_min + 1;
    int height = y_max - y_min + 1;
    int size   = (width > height) ? width : height;

    // clear to white
    for (int i = 0; i < NORM_SIZE * NORM_SIZE; i++)
        output[i] = 0xFFFF;

    // center the symbol in the output grid
    int x_offset = (size - width)  / 2;
    int y_offset = (size - height) / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // map (x,y) in bounding box -> output pixel position
            int out_x = ((x + x_offset) * NORM_SIZE) / size;
            int out_y = ((y + y_offset) * NORM_SIZE) / size;

            if (out_x < NORM_SIZE && out_y < NORM_SIZE) {
                output[out_y * NORM_SIZE + out_x] =
                    image[(y_min + y) * IMAGE_WIDTH + (x_min + x)];
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

  int x_start = (IMAGE_WIDTH - NORM_SIZE * scale) / 2;
  int y_start = (IMAGE_HEIGHT - NORM_SIZE * scale) / 2;



  for (int y = 0; y < NORM_SIZE; y++) {
    for (int x = 0; x < NORM_SIZE; x++) {
      short pixel = symbol[y * NORM_SIZE + x];


      // scale each pixel
      for (int dy = 0; dy < scale; dy++) {
        for (int dx = 0; dx < scale; dx++) {
          int sx = x * scale + x_start;  // center X
          int sy = y * scale + y_start;  // center Y


          plot_pixel(sx + dx, sy + dy, pixel);
        }
      }
    }
  }
}


void delay() { for (volatile int i = 0; i < 20000000; i++); }

float correlated_similarity(short* image, const Template* template, int j) {

    int max_intersection= 0;
    int best_union = 1;

     //use correlated similarity instead
  //this approach "slides" the image over the sample until it obtains the highest jaccard score
  //this is considered to be the most accurate score as it accounts for mislaignemnt

  //takes ratio of intersection over union
  //how many pixels are black in BOTH images / how many pixels are black in EITHER image

    for (int  dy = -SEARCH_RANGE; dy <= SEARCH_RANGE; dy++) {
        for (int dx = -SEARCH_RANGE; dx <= SEARCH_RANGE; dx++) {
            int current_intersection = 0;
            int current_union = 0;

            for (int y = 0; y < NORM_SIZE; y++) {
                for (int x= 0; x < NORM_SIZE; x++) {
                    int tx = x + dx;
                    int ty = y + dy;

                    int i_pixel = (image[y*NORM_SIZE + x] == 0x0000);
                    int t_pixel =0;

                    if (tx>= 0 && tx < NORM_SIZE && ty >= 0 && ty < NORM_SIZE) {
                        t_pixel = (template[j].data[ty * NORM_SIZE + tx]);
                    }

                    current_intersection += (i_pixel & t_pixel);
                    current_union += (i_pixel | t_pixel);
                }
            } 

            if (current_intersection > max_intersection) {
                max_intersection = current_intersection;
                best_union = current_union;
            }
        }
    }
    return (float)max_intersection / (float)best_union;
}


float best_template_match(short* image, int starting_index, int final_index) {
  float max_jaccard_similarity = 0.0;
//   int best_i;


  for (int i = starting_index; i < final_index; i++) {
    float similarity = correlated_similarity(image, template_arr, i);

    //jaccard similarity score ranges from 0 to 1
    //best possible score is a 1 --> images are identical
    //worst possible socre is a 0 --> no pixels in common
    if (similarity > max_jaccard_similarity) {
      max_jaccard_similarity = similarity;
    //   best_i = i;
    }
  }


  return max_jaccard_similarity;
}

//Written by claude.ai
void display_int_on_hex(int value) {
    volatile int* HEX0 = (int*)HEX3_HEX0_BASE;       // HEX0 is bits 7:0
    volatile int* HEX2 = (int*)HEX5_HEX4_BASE;       // HEX2 is at separate base

    // clamp to valid range
    if (value < 0)   value = 0;
    if (value > 239) value = 239;

    int hundreds = value / 100;         // 0 or 1 or 2
    int tens     = (value / 10) % 10;
    int ones     = value % 10;

    // HEX3_HEX0_BASE controls HEX3(bits31:24) HEX2(bits23:16) HEX1(bits15:8) HEX0(bits7:0)
    int current = *HEX0;
    current &= 0xFF000000;  // preserve HEX3, clear HEX2/HEX1/HEX0
    current |= (seven_seg[hundreds] << 16);  // HEX2
    current |= (seven_seg[tens]     <<  8);  // HEX1
    current |= (seven_seg[ones]          );  // HEX0
    *HEX0 = current;
}

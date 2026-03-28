#include <stdio.h>
// #include <stdlib.h>

#include "address_map.h"
#include "templates.h"

// constants
#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240
#define MAX_SYMBOLS 1
#define NORM_SIZE 28
#define Y_START 11
#define Y_END 225
#define EDGE_IGNORE 10
#define MIN_SYMBOL_AREA 100
#define TOTAL 320 * 240
#define TEMPLATES_PER_DIGIT 10
#define CURRENT_SYMBOL "x"
#define SYM_START_I 0
#define SYM_END_I 40
#define SEARCH_RANGE 4

// symbol box definition
typedef struct {
  int x_min, x_max;  // left and right edge
  int y_min, y_max;  // top and bottom edge
} BoundingBox;

// globals
short symbols[MAX_SYMBOLS][NORM_SIZE * NORM_SIZE];
volatile short int* pixel_buf_address = (short int*)0x08000000;
// 7-segment encoding for digits 0-9
// Segments: bit 0=a, 1=b, 2=c, 3=d, 4=e, 5=f, 6=g (0 = ON, 1 = OFF for common
// anode)
const unsigned char seven_seg[10] = {
    0x3F,  // 0: segments a,b,c,d,e,f ON
    0x06,  // 1: segments b,c ON
    0x5B,  // 2: segments a,b,d,e,g ON
    0x4F,  // 3: segments a,b,c,d,g ON
    0x66,  // 4: segments b,c,f,g ON
    0x6D,  // 5: segments a,c,d,f,g ON
    0x7D,  // 6: segments a,c,d,e,f,g ON
    0x07,  // 7: segments a,b,c ON
    0x7F,  // 8: all segments ON
    0x6F   // 9: segments a,b,c,d,f,g ON
};

typedef struct {
  const char* label;
  float max_similarity;
} Symbol;

Symbol symbol_arr[13] = {
    {"1", 0.0}, {"2", 0.0}, {"3", 0.0}, {"4", 0.0}, {"5", 0.0},
    {"6", 0.0}, {"7", 0.0}, {"8", 0.0}, {"9", 0.0}, {"0", 0.0},
    {"+", 0.0}, {"-", 0.0}, {"x", 0.0}, {"/", 0.0},
};

// function declarations
int adaptive_threshold(short* image, BoundingBox* boxes);
void median_filter(short int* buffer);
int segment_symbols(short int* image, BoundingBox* boxes);
// void display_cropped_symbol(short* image, BoundingBox box);
void normalize_symbol(short int* image, BoundingBox box, short* output);
void plot_pixel(int x, int y, short color);
void clear_screen();
void display_symbol(short* symbol);
void delay();
float correlated_similarity(short* image, const Template* template, int i);
float best_template_match(short* image, int starting_index, int final_index);
void display_int_on_hex(int value);
void display_char_on_hex(char c);
int sum_of_absolute_differences(short* image, const Template* templ, int i);
long correlated_sad(short* image, const Template* templ, int i);
long correlated_ssd(short* image, const Template* templ, int i);

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
  *pixel_buf_ctrl =
      1;  // wriitng 1 to Buffer register triggers display - enable VGA output

  display_char_on_hex('0');

  while (1) {
    int edge_capture = *(KEY_ptr + 3);

    if (edge_capture & 0x1) {  // check if key0 was pressed and released
      *(KEY_ptr + 3) = 0x1;  // clearedge capture bit by writing 1 to first bit
      *(video_in_dma + 3) = 0x0;  // takes a pic (enable goes from 1--> 0)

      BoundingBox boxes[MAX_SYMBOLS];
      short image[TOTAL];

      int num_symbols = adaptive_threshold(image, boxes);

      short normalized[NORM_SIZE * NORM_SIZE];

      // now show cropped pieces
      for (int i = 0; i < num_symbols; i++) {
        normalize_symbol(image, boxes[i], normalized);
        display_symbol(normalized);
        delay();
      }

      while (!(*(KEY_ptr + 3) & 0x2) && !(*(KEY_ptr + 3) & 0x4));

      // if KEY1 is pressed then keep this image
      // if KEY2 is pressed go back and try again
      if (*(KEY_ptr + 3) & 0x2) {
        // find highest jacccard similarity with the image and each 1 template
        int template_i = 0;
        float best_score_overall_jaccard = 0;
        const char* best_match_jaccard;

        long best_score_overall_sad = 1000000;
        const char* best_match_sad;

        long best_score_overall_ssd = 1000000;
        const char* best_match_ssd;

        for (int i = 0; i < 13; i++) {
          float max_jaccard_score = 0.0;
          int min_sad_score = 1000000;
          int min_ssd_score = 1000000;

          const char* label = symbol_arr[i].label;

          while (!strcmp(template_arr[template_i].label, label)) {
            float temp_jaccard_score =
                correlated_similarity(normalized, template_arr, template_i);
            long temp_sad_score =
                correlated_sad(normalized, template_arr, template_i);
            long temp_ssd_score =
                correlated_ssd(normalized, template_arr, template_i);

            if (temp_sad_score < min_sad_score) {
              min_sad_score = temp_sad_score;
            }

            if (temp_ssd_score < min_ssd_score) {
              min_ssd_score = temp_ssd_score;
            }

            if (temp_jaccard_score > max_jaccard_score) {
              max_jaccard_score = temp_jaccard_score;
            }

            template_i++;
          }

          if (max_jaccard_score > best_score_overall_jaccard) {
            best_score_overall_jaccard = max_jaccard_score;
            best_match_jaccard = label;
          }

          if (min_sad_score < best_score_overall_sad) {
            best_score_overall_sad = min_sad_score;
            best_match_sad = label;
          }

          if (min_ssd_score < best_score_overall_ssd) {
            best_score_overall_ssd = min_ssd_score;
            best_match_ssd = label;
          }
        }

        // float score_1 =  best_template_match(normalized, 0, 5);
        // float score_2 =  best_template_match(normalized, 5, 10);

        // printf("Score 1: %f, Score 2: %f\n", score_1, score_2);

        printf("Best Jaccard Match: %s with score %f\n", best_match_jaccard,
               best_score_overall_jaccard);
        printf("Best SAD Match: %s with score %d\n", best_match_sad,
               best_score_overall_sad);
        printf("Best SSD Match: %s with score %d\n", best_match_ssd,
               best_score_overall_ssd);

        // if (!strcmp(best_match_so_far, "1")) {
        //   display_int_on_hex(1);
        // } else if (!strcmp(best_match_so_far, "2")) {
        //   display_int_on_hex(2);
        // } else if (!strcmp(best_match_so_far, "3")) {
        //   display_int_on_hex(3);
        // } else if (!strcmp(best_match_so_far, "4")) {
        //   display_int_on_hex(4);
        // } else if (!strcmp(best_match_so_far, "5")) {
        //   display_int_on_hex(5);
        // } else if (!strcmp(best_match_so_far, "6")) {
        //   display_int_on_hex(6);
        // } else if (!strcmp(best_match_so_far, "7")) {
        //   display_int_on_hex(7);
        // } else if (!strcmp(best_match_so_far, "8")) {
        //   display_int_on_hex(8);
        // } else if (!strcmp(best_match_so_far, "9")) {
        //   display_int_on_hex(9);
        // } else if (!strcmp(best_match_so_far, "0")) {
        //   display_int_on_hex(0);
        // } else if (!strcmp(best_match_so_far, "+")) {
        //   display_char_on_hex('a');
        // } else if (!strcmp(best_match_so_far, "-")) {
        //   display_char_on_hex('S');
        // } else if (!strcmp(best_match_so_far, "x")) {
        //   display_char_on_hex('p');
        // } else if (!strcmp(best_match_so_far, "/")) {
        //   display_char_on_hex('d');
        // }

        delay();
      }

      *(KEY_ptr + 3) = 0xF;  // clear all keys

      *(video_in_dma + 3) = 0x4;  // going back to video mode
    }
    display_char_on_hex('0');
  }
  return 0;
}

void capture_template(short* image, BoundingBox box, uint8_t* out) {
  short normalized[NORM_SIZE * NORM_SIZE];
  normalize_symbol(image, box, normalized);
  for (int i = 0; i < NORM_SIZE * NORM_SIZE; i++) {
    out[i] = (normalized[i] == 0x0000) ? 0 : 1;
  }
}

/*Calculates a threshold value for every single pixel based on the pixels in its
immediate neighborhood (e.g., a 15x15 block around it). If a pixel is darker
than its local average, it's considered foreground (black digit). If a pixel is
brighter than its local average, it's considered background (white board). */
int adaptive_threshold(short* image, BoundingBox* boxes) {
  int total = IMAGE_HEIGHT * IMAGE_WIDTH;
  // Grayscale Conversion
  for (int i = 0; i < total; i++) {
    short pixel =
        *(pixel_buf_address + ((i / IMAGE_WIDTH) << 9) + (i % IMAGE_WIDTH));

    int r = (pixel >> 11) & 0x1F;
    int g = (pixel >> 5) & 0x3F;
    int b = pixel & 0x1F;

    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;

    unsigned char grey = (unsigned char)((77 * r + 150 * g + 29 * b) >> 8);
    image[i] = (short)grey;  // Current grayscale value stored in image array
  }

  // Apply filter before thresholding
  median_filter(image);

  // Create a temporary buffer to store the new binary image.
  static short binary_image[IMAGE_WIDTH * IMAGE_HEIGHT];

  // define the size of the neighborhood to look at. A 15x15 block
  // The 'constant_c' is a small subtraction to make the threshold a little more
  // strict. This helps to keep small speckles out of the background.
  int block_size = 15;
  int constant_c = 5;
  int half_block = block_size / 2;

  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      long local_sum = 0;
      int count = 0;

      // for each pixel, calculate the average grayscale value of its
      // neighborhood.
      for (int dy = -half_block; dy <= half_block; dy++) {
        for (int dx = -half_block; dx <= half_block; dx++) {
          int nx = x + dx;
          int ny = y + dy;
          // check for bounds
          if (nx >= 0 && nx < IMAGE_WIDTH && ny >= 0 && ny < IMAGE_HEIGHT) {
            local_sum += image[ny * IMAGE_WIDTH + nx];
            count++;
          }
        }
      }

      int local_avg = local_sum / count;
      int threshold =
          local_avg - constant_c;  // local threshold for this specific pixel

      // Apply the threshold for this pixel:
      // If the pixel is darker than its local average (minus the constant),
      // it's part of a digit.
      if (image[y * IMAGE_WIDTH + x] <= threshold) {
        binary_image[y * IMAGE_WIDTH + x] = 0x0000;  // Black (digit)
      } else {
        binary_image[y * IMAGE_WIDTH + x] = 0xFFFF;  // White (background)
      }
    }
  }

  // Copy the result back to image buffer.
  for (int i = 0; i < total; i++) {
    image[i] = binary_image[i];
  }

  // display binary
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      // edge forcing band logic
      if (y < Y_START || y > Y_END || x < EDGE_IGNORE ||
          x > IMAGE_WIDTH - EDGE_IGNORE - 1 || y > IMAGE_HEIGHT - 1) {
        image[y * IMAGE_WIDTH + x] = 0xFFFF;  // Force it white
        *(pixel_buf_address + (y << 9) + x) = 0xFFFF;
      } else {
        *(pixel_buf_address + (y << 9) + x) = image[y * IMAGE_WIDTH + x];
      }
    }
  }

  delay();
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

        // New component logic
        int current_width = boxes[count].x_max - boxes[count].x_min + 1;
        int current_height = boxes[count].y_max - boxes[count].y_min + 1;
        int current_area = current_width * current_height;
        if (current_area >= MIN_SYMBOL_AREA) {
          count++;  // It's a real symbol, move to the next box
        } else {
          // Not a real symbol - noise dot. Do nothing.
          //  The next object will overwrite this index (count) in the boxes
          //  array.
        }
        in_symbol = 0;  // We are no longer inside a symbol
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
  if (x_max >= IMAGE_WIDTH) x_max = IMAGE_WIDTH - 1;
  if (y_max >= IMAGE_HEIGHT) y_max = IMAGE_HEIGHT - 1;

  int width = x_max - x_min + 1;
  int height = y_max - y_min + 1;
  int size = (width > height) ? width : height;

  // clear to white
  for (int i = 0; i < NORM_SIZE * NORM_SIZE; i++) output[i] = 0xFFFF;
  // center the symbol in the output grid
  int x_offset = (size - width) / 2;
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
      plot_pixel(x, y, 0x38ab);  // white background
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

void delay() { for (volatile int i = 0; i < 9750000; i++); }

// int sum_of_absolute_differences(short* image, const Template* templ, int i) {
//   int sum = 0;

//   for (int x = 0; x < NORM_SIZE; x++) {
//     for (int y = 0; y < NORM_SIZE; y++) {
//       int i_pixel = (image[y * NORM_SIZE + x] == 0x0000) ? 1 : 0;
//       int t_pixel = templ[i].data[y * NORM_SIZE + x];

//       sum += abs(i_pixel - t_pixel);
//     }
//   }

//   return sum;
// }

long correlated_sad(short* image, const Template* tmpl, int j) {
  long min_sad = 2147483647;  // INT_MAX

  for (int dy = -SEARCH_RANGE; dy <= SEARCH_RANGE; dy++) {
    for (int dx = -SEARCH_RANGE; dx <= SEARCH_RANGE; dx++) {
      long current_sad = 0;

      for (int y = 0; y < NORM_SIZE; y++) {
        for (int x = 0; x < NORM_SIZE; x++) {
          int tx = x + dx;
          int ty = y + dy;

          int a = (image[y * NORM_SIZE + x] == 0x0000) ? 0 : 1;
          int b = 0;

          if (tx >= 0 && tx < NORM_SIZE && ty >= 0 && ty < NORM_SIZE) {
            b = tmpl[j].data[ty * NORM_SIZE + tx];
          }

          // int diff = a - b;
          current_sad += (a - b < 0) ? (b - a) : (a - b);
        }
      }

      if (current_sad < min_sad) {
        min_sad = current_sad;
      }
    }
  }

  return min_sad;  // lower = better
}

long correlated_ssd(short* image, const Template* tmpl, int j) {
  long min_ssd = 2147483647;  // INT_MAX

  for (int dy = -SEARCH_RANGE; dy <= SEARCH_RANGE; dy++) {
    for (int dx = -SEARCH_RANGE; dx <= SEARCH_RANGE; dx++) {
      long current_ssd = 0;

      for (int y = 0; y < NORM_SIZE; y++) {
        for (int x = 0; x < NORM_SIZE; x++) {
          int tx = x + dx;
          int ty = y + dy;

          int a = (image[y * NORM_SIZE + x] == 0x0000) ? 0 : 1;
          int b = 0;

          if (tx >= 0 && tx < NORM_SIZE && ty >= 0 && ty < NORM_SIZE) {
            b = tmpl[j].data[ty * NORM_SIZE + tx];
          }

          // int diff = a - b;
          current_ssd += (a - b) * (a - b);
        }
      }

      if (current_ssd < min_ssd) {
        min_ssd = current_ssd;
      }
    }
  }

  return min_ssd;  // lower = better
}

// int sum_of_squared_differences(short* image, const Template* templ, int i) {
//   int sum = 0;

//   for (int x = 0; x < NORM_SIZE; x++) {
//     for (int y = 0; y < NORM_SIZE; y++) {
//       int i_pixel = (image[y * NORM_SIZE + x] == 0x0000) ? 1 : 0;
//       int t_pixel = templ[i].data[y * NORM_SIZE + x];

//       sum += (i_pixel - t_pixel) * (i_pixel - t_pixel);
//     }
//   }

//   return sum;
// }

float correlated_similarity(short* image, const Template* template, int j) {
  int max_intersection = 0;
  int best_union = 1;

  // use correlated similarity instead
  // this approach "slides" the image over the sample until it obtains the
  // highest jaccard score this is considered to be the most accurate score as
  // it accounts for mislaignemnt

  // takes ratio of intersection over union
  // how many pixels are black in BOTH images / how many pixels are black in
  // EITHER image

  for (int dy = -SEARCH_RANGE; dy <= SEARCH_RANGE; dy++) {
    for (int dx = -SEARCH_RANGE; dx <= SEARCH_RANGE; dx++) {
      int current_intersection = 0;
      int current_union = 0;

      for (int y = 0; y < NORM_SIZE; y++) {
        for (int x = 0; x < NORM_SIZE; x++) {
          int tx = x + dx;
          int ty = y + dy;

          int i_pixel = (image[y * NORM_SIZE + x] == 0x0000);
          int t_pixel = 0;

          if (tx >= 0 && tx < NORM_SIZE && ty >= 0 && ty < NORM_SIZE) {
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

// Written by claude.ai
void display_int_on_hex(int value) {
  volatile int* HEX0 = (int*)HEX3_HEX0_BASE;  // HEX0 is bits 7:0
  volatile int* HEX2 = (int*)HEX5_HEX4_BASE;  // HEX2 is at separate base

  // clamp to valid range
  if (value < 0) value = 0;
  if (value > 239) value = 239;

  int hundreds = value / 100;  // 0 or 1 or 2
  int tens = (value / 10) % 10;
  int ones = value % 10;

  // HEX3_HEX0_BASE controls HEX3(bits31:24) HEX2(bits23:16) HEX1(bits15:8)
  // HEX0(bits7:0)
  int current = *HEX0;
  current &= 0xFF000000;  // preserve HEX3, clear HEX2/HEX1/HEX0
  current |= (seven_seg[hundreds] << 16);  // HEX2
  current |= (seven_seg[tens] << 8);       // HEX1
  current |= (seven_seg[ones]);            // HEX0
  *HEX0 = current;
}

// Written by claude.ai

// 7-segment encoding: segments = gfedcba (bit 6 to bit 0)
// 0 = segment ON, 1 = segment OFF (active low on DE1-SoC)

void display_char_on_hex(char c) {
  volatile int* HEX_ptr = (int*)HEX3_HEX0_BASE;

  int segments;

  switch (c) {
    case 'p':
      segments = 0x73;
      break;  //  _
              // |_|
              // |
    case 'S':
      segments = 0x6D;
      break;  //  _
              // |_
              //  _|
    case 'a':
      segments = 0x77;
      break;  //  _
              // |_|
              // | |
    case 'd':
      segments = 0x5E;
      break;  //
              //  _|
              // |_|
    default:
      segments = 0x00;
      break;  // all off
  }

  *HEX_ptr = segments;
}

// The 7-segment layout for reference:
//
//  _
// |_|   segments: a=top, b=top-right, c=bottom-right,
// |_|            d=bottom, e=bottom-left, f=top-left, g=middle
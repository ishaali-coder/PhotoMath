#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "address_map.h"
#include "templates.h"
#include "backgrounds.h"

// constants
#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240
#define MAX_SYMBOLS 9
#define NORM_SIZE 28
#define Y_START 11  // top band boundary
#define Y_END 225   // bottom border boundary
#define EDGE_IGNORE 10
#define TOTAL 320 * 240
#define OPERATOR_START_I 34  // current index in templ array where operators start being stored
#define SEARCH_RANGE 4
#define MIN_SYMBOL_AREA 100

// symbol box definition
typedef struct {
  int x_min, x_max;  // left and right edge
  int y_min, y_max;  // top and bottom edge
} BoundingBox;

// might be able to chnage this from a struct to an array of chars instead
typedef struct {
  const char label;      // used to dteermine what symbol
  float max_similarity;  // not used --> might not need ?
  // wait to see if i will add more properties later when feature matching is further implemented
} Symbol;

// globals
volatile short int* pixel_buf_address = (short int*)0x08000000;
Symbol symbol_arr[14] = {
    {'1', 0.0},
    {'2', 0.0},
    {'3', 0.0},
    {'4', 0.0},
    {'5', 0.0},
    {'6', 0.0},
    {'7', 0.0},
    {'8', 0.0},
    {'9', 0.0},
    {'0', 0.0},
    {'+', 0.0},
    {'-', 0.0},
    {'x', 0.0},
    {'/', 0.0},
};

// function declarations
void adaptive_threshold(short* image);
void median_filter(short int* buffer);
int segment_symbols(short int* image, BoundingBox* boxes);
void normalize_symbol(short int* image, BoundingBox box, short* output);
void plot_pixel(int x, int y, short color);
void clear_screen();
void display_symbol(short* symbol);
void delay();
float calculate_aspect_ratio_input(short* data);
float solve_equation(char* equation, int num_symbols);
long correlated_sad(short* image, const Template* tmpl, int j);
void draw_background();
void draw_calculating();
void draw_instruction();
void draw_equals(int x, int y, int width, int thickness, short color);
void display_answer(float answer);
int calculate_weighted_diff(short *img, const uint8_t *templ, int dx, int dy);

int main(void) {
  // pointers to video-in dma controller
  volatile int* video_in_dma = (int*)VIDEO_IN_BASE;  // 0xFF203060
  volatile int* edge_detect = (int*)EDGE_DETECT_CTRL_BASE;  // 0xFF203070
  volatile int* sw_addr = (int*)SW_BASE; // 0xFF200040
  // pointer to video out pixel buffer controller
  volatile int* pixel_buf_ctrl = (int*)PIXEL_BUF_CTRL_BASE; // 0xFF203020
  volatile int* KEY_ptr = (int*)KEY_BASE;

  *(KEY_ptr + 3) = 0xF;  // clear edge capture register initially
  *(video_in_dma + 3) = 0x0; //disable camera i itially so it does not overwrite background
  draw_background(); //show background image on start
  while (!(*sw_addr & 0x1)) {
    //stay here untill first switch flipped
  }

  // direct video feed from camera ->vga
  *(video_in_dma + 1) = 0x08000000;  // set camera to write directly to vga buffer 0x08 default adress
  *edge_detect = 0x0;  // disable edge detection
  // enable the video dma controller (set en bit), EN = 1
  //  cntrol register is at 0xFF20306C (offset 3 from VIDEO_IN_BASE)
  *(video_in_dma + 3) = 0x4;
  *pixel_buf_ctrl = 1;  // wriitng 1 to Buffer register triggers display - enable VGA output

  while (1) {
     //check if the switch is still ON, if off show background again
    if (*sw_addr & 0x1) {
      *(video_in_dma + 3) = 0x0; // Stop camera
      draw_instruction();
      while (*sw_addr & 0x1); // Wait for switch0 to be flipped off
    }
    else if (*sw_addr & 0x2) {
      *(KEY_ptr + 3) = 0xF;
      *(video_in_dma + 1) = 0x08000000;  
      *(video_in_dma + 3) = 0x4; // Restart camera
      if (*(KEY_ptr + 3) & 0x1) {  // check if key0 was pressed and released
        *(KEY_ptr + 3) = 0x1;       // clearedge capture bit by writing 1 to first bit
        *(video_in_dma + 3) = 0x0;  // takes a pic (enable goes from 1--> 0)

        BoundingBox boxes[MAX_SYMBOLS];
        short image[TOTAL];
        adaptive_threshold(image);
        int num_symbols = segment_symbols(image, boxes);
        printf("Numer of symbols recognized: %d/n", num_symbols);
        short normalized[NORM_SIZE * NORM_SIZE];
        char equation[MAX_SYMBOLS];
        memset(equation, 0, sizeof(equation));

        while (!(*(KEY_ptr + 3) & 0x2) && !(*(KEY_ptr + 3) & 0x4));
        // if KEY1 is pressed then keep this image
        // if KEY2 is pressed go back and try again
        if (*(KEY_ptr + 3) & 0x2) { 
          *(KEY_ptr + 3) = 0xF; //clear edge capture
          for (int x = 0; x < MAX_SYMBOLS; x ++) equation[x] = ' ';
          int sym_i = 0;

          // find lowest difference between the image and each number
          for (sym_i; sym_i < num_symbols; sym_i++) {
            normalize_symbol(image, boxes[sym_i], normalized);
            float captured_ratio = calculate_aspect_ratio_input(normalized);
            long best_score_overall = 2147483647;
            char best_char = '?';
            display_symbol(normalized);
            delay();

            // 3. Compare against ALL templates
            for (int symbol_i = 0; symbol_i < 14; symbol_i++) {
              char label = symbol_arr[symbol_i].label;
            
              // heuristic filtering
              // If the captured shape is very wide, it's likely '-' or 'x', not '1'
              if (label == '1' && captured_ratio > 0.6) continue; 
              // If the captured shape is very tall, it's likely a digit, not '-'
              if (label == '-' && ( captured_ratio < 1.5 && captured_ratio > -1)) continue;

              for (int t_idx = 0; t_idx < MAX_TEMPLATES; t_idx++) {
                if (template_arr[t_idx].label == label) {
                  long score = correlated_sad(normalized, template_arr, t_idx);
                  if (score < best_score_overall) {
                    best_score_overall = score;
                    best_char = label;
                  }
                }
              }
            }
            printf("Symbol %d Match: %c (Score: %ld, Ratio: %.2f)\n", sym_i, best_char, best_score_overall, captured_ratio);
            equation[sym_i] = best_char;
          }
          draw_calculating();
          delay();
          *(KEY_ptr + 3) = 0xF;  // clear all keys

          float final_answer = solve_equation(equation, num_symbols);
          display_answer(final_answer);

          //wait for key press before returning to camera mode
          while (!(*(KEY_ptr + 3) & 0x8)); //do nothing until KEY3 is pressed
          *(KEY_ptr + 3) = 0x8;
          *(video_in_dma + 3) = 0x4;  // going back to live video mode
        }
      }
    } else {
      *(video_in_dma + 3) = 0x0; // Stop camera
      draw_background();
      while (!(*sw_addr & 0x3)); //wait until any switch is flipped
    }
  } 
  return 0;
}

float solve_equation(char* equation, int n) {
    char op = ' ';
    int i_op = -1;

    // Find the operator position dynamically
    for (int i = 0; i < n; i++) {
        char s = equation[i];
        if (s == '+' || s == '-' || s == 'x' || s == '/') {
            op = s;
            i_op = i;
            break;
        }
    }

    if (i_op == -1) return 0.0; // No operator found

    float first_num = 0;
    float second_num = 0;

    // Build first number
    for (int i = 0; i < i_op; i++) {
        first_num = (first_num * 10) + (equation[i] - '0');
    }

    // Build second number
    for (int i = i_op + 1; i < n; i++) {
        //if (equation[i] >= '0' && equation[i] <= '9') {
            second_num = (second_num * 10) + (equation[i] - '0');
        //}
    }

    float result = 0;
    if (op == '+') result = first_num + second_num;
    else if (op == '-') result = first_num - second_num;
    else if (op == 'x') result = first_num * second_num;
    else if (op == '/') result = (second_num != 0) ? (first_num / second_num) : 0;

    printf("\nCalc: %.1f %c %.1f = %.1f\n", first_num, op, second_num, result);
    return result;
}

// int solve_equation(char* equation, int n) {
//   int eq= 0;
//   int mode = 0; //o add
//   //1 sub
//   //2 mult
//   //3 div

//   for (int i = 0; i < n; i ++ ) {
//     char sym = equation[i];
//     if (i < n/2 && n == 3) {
//       eq += (sym - '0');
//     } else if (i < n/2 && n > 3) {//numbers with more than 1 digit
//       eq = eq * (10) + (sym - '0'); //multply old number by 10 to shift to tens and add new number in ones place
//     } else if (i == n/2) {
//       if (sym == '-') mode +=1; 
//       else if (sym == 'x') mode += 2;
//       else if (sym == '/') mode += 3;
//     } else if (i > n/2) {
      
//       if (mode == 0) {eq = eq + (sym - '0');}
//       else if (mode == 1) {eq = eq - (sym - '0');}
//       else if (mode == 2) { eq = eq * (sym - '0') ;}
//       else if (mode == 3) {eq = eq / (sym - '0'); }
//     }
//   }
//   printf("\nAnswer: %d\n", eq);
//   return eq;
// }

void adaptive_threshold(short* image) {
  // int total = IMAGE_HEIGHT * IMAGE_WIDTH;
  //  Grayscale Conversion
  for (int i = 0; i < TOTAL; i++) {
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
  median_filter(image); // Apply filter before thresholding
  // Create a temporary buffer to store the new binary image.
  static short binary_image[TOTAL];

  // define the size of the neighborhood to look at - 15x15 block
  // This helps to keep small speckles out of the background.
  int block_size = 15;
  int constant_c = 5;  //subtraction to make threshold more strict
  int half_block = block_size / 2;

  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      long local_sum = 0;
      int count = 0;
      // for each pixel, calculate the average grayscale value of its neighborhood.
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
      int threshold = local_avg - constant_c;  // local threshold for this specific pixel

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
   // Copy the result back to your 'image' buffer.
  for (int i = 0; i < TOTAL; i++) {
    image[i] = binary_image[i];
  }
  //display binary image
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      // edge forcing band logic
      if (y < Y_START || y > Y_END || x < EDGE_IGNORE || x > IMAGE_WIDTH - EDGE_IGNORE - 1 || y > IMAGE_HEIGHT - 1) {
        image[y * IMAGE_WIDTH + x] = 0xFFFF;  // Force it white
        *(pixel_buf_address + (y << 9) + x) = 0xFFFF;
      } else {
        image[y * IMAGE_WIDTH + x] = binary_image[y * IMAGE_WIDTH + x];
        *(pixel_buf_address + (y << 9) + x) = image[y * IMAGE_WIDTH + x];
      }
    }
  }
  delay();
  //return segment_symbols(image, boxes); //segement cleaner image
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
          values[k] = buffer[(y + dy) * IMAGE_WIDTH + (x + dx)];  // take pixel at (x+dx, y+dy) and store in array
          k = k + 1;   // get surrounding pixels
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
        boxes[count].x_max = x - gap_count;   // Mark the RIGHT boundary of the symbol
        int y_min = IMAGE_HEIGHT;  // Initialize top boundary very large
        int y_max = Y_START;       // Initialize bottom boundary TO END OF STRIPE
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

        boxes[count].y_min = y_min;       // Save TOP boundary
        boxes[count].y_max = y_max;       // Save BOTTOM boundary
        int current_width = boxes[count].x_max - boxes[count].x_min + 1;
        int current_height = boxes[count].y_max - boxes[count].y_min + 1;
        int current_area = current_width * current_height;
        if (current_area >= MIN_SYMBOL_AREA) {
          count++; //It's a real symbol, move to the next box
        } else {
        //Not a realy symbol - likely a noise dot. Do nothing.
        //the next object will overwrite this index (count) in the boxes array.
        }
        in_symbol = 0;   // We are no longer inside a symbol
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

void display_cropped_symbol(short* image, BoundingBox box) {  // function takes binary image and coordinates of one detected symbol
  clear_screen();

  int symbol_width = box.x_max - box.x_min +1;                                          // calculates width of symbol; +1 beacuse pixel indices are inclusive
  int symbol_height = box.y_max - box.y_min + 1;  // height of symbol

  // Find centre of screen (160, 120)
  int screen_center_x = IMAGE_WIDTH / 2;
  int screen_center_y = IMAGE_HEIGHT / 2;

  // compute top left of symbol so its centered
  int offset_x = screen_center_x - symbol_width / 2;  // left position = centre - half width so midle aligns with screen centre
  int offset_y = screen_center_y - symbol_height / 2;

  for (int y = box.y_min; y <= box.y_max; y++) {  // loop thorught symbols vertical and horizontal region
    for (int x = box.x_min; x <= box.x_max; x++) {
      short pixel = image[y * IMAGE_WIDTH + x];  // get pixel value from original image
      int draw_x = offset_x + (x - box.x_min);  // repositioning pixel
      int draw_y = offset_y + (y - box.y_min);

      plot_pixel(draw_x, draw_y, pixel);  // draw pixel at new position
    }
  }
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
        output[out_y * NORM_SIZE + out_x] = image[(y_min + y) * IMAGE_WIDTH + (x_min + x)];
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
      plot_pixel(x, y, 0x8000);  // change this colour
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

float calculate_aspect_ratio_input(short* data) {
  int x_min = NORM_SIZE;
  int y_min = NORM_SIZE;
  int x_max = 0;
  int y_max = 0;

  // figure out bounding box of normalized template

  for (int x = 0; x < NORM_SIZE; x++) {
    for (int y = 0; y < NORM_SIZE; y++) {
      // if we find a black pixel in the templtae
      if (data[y * NORM_SIZE + x] == 0x0000) {
        // check if it occurs earlier than our old first
        if (x < x_min) {
          x_min = x;
        }
        // or later than our previously found last
        if (x > x_max) {
          x_max = x;
        }

        // similarly for y
        if (y < y_min) {
          y_min = y;
        }
        if (y > y_max) {
          y_max = y;
        }
      }
    }
  }

  if (y_max == y_min || x_max < x_min || y_max < y_min) return -1.0;  // avoid infinite aspect ratios

  return ((float)(x_max - x_min) / (float)(y_max - y_min));
}

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
          int b = tmpl[j].data[ty * NORM_SIZE + tx];

          if (tx >= 0 && tx < NORM_SIZE && ty >= 0 && ty < NORM_SIZE) {
            b = tmpl[j].data[ty * NORM_SIZE + tx];
          }

          // Use a squared difference or absolute difference
          current_sad += (a != b) ? 1 : 0;
        }
      }

      if (current_sad < min_sad) {
        min_sad = current_sad;
      }
    }
  }

  return min_sad;  // lower = better
}

void draw_background() {
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      plot_pixel(x, y, background_pixels[y * IMAGE_WIDTH + x]);
    }
  }
}

void draw_calculating() {
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      plot_pixel(x, y, calculating[y * IMAGE_WIDTH + x]);
    }
  }
}

void draw_instruction() {
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      plot_pixel(x, y, instructions[y * IMAGE_WIDTH + x]);
    }
  }
}

void draw_equals(int x, int y, int width, int thickness, short color) {
  // Top line
  for (int i = 0; i < width; i++) {
    for (int t = 0; t < thickness; t++) {
      plot_pixel(x + i, y + t, color);
    }
  }

  // Bottom line
  for (int i = 0; i < width; i++) {
    for (int t = 0; t < thickness; t++) {
      plot_pixel(x + i, y + 2 * thickness + 6 + t, color);
    }
  }
}

void display_answer(float answer) {
    clear_screen();  //clear CGA
    char str[20];
    sprintf(str, "%.1f", answer); // supports decimals like 3.14

    int len = strlen(str);
    int scale = 4;  //how big each pixel becomes when drawn, each pixel is 4x4 block
    int digit_size = NORM_SIZE * scale; //size of rach digit

    int spacing = scale * 2;  // consistent spacing between characters
    int gap = digit_size / 2;

    int total_width = 0;

    for (int i = 0; i < len; i++) { //loop through each character
      if (str[i] == '.') {
        total_width += scale * 2;  // small width for decimal
      } else {
        total_width += digit_size; //normal digtis take full width
      }
       if (i != len - 1) total_width += spacing;
    }

    int eq_width = digit_size;
    int eq_height = digit_size / 6;

    total_width += eq_width + gap;

    int x_start = (IMAGE_WIDTH - total_width) / 2; //center vertically
    int y_start = (IMAGE_HEIGHT - digit_size) / 2; //center horizontally

    // To draw the equal sign
    int eq_x = x_start;  //= start at left side of centered block
    int eq_y = y_start + digit_size / 2 - eq_height;

    draw_equals(eq_x, eq_y, eq_width, eq_height, 0xFFFF);

    int current_x = x_start + eq_width + gap;

    for (int i = 0; i < len; i++) { //draw eacha character
      char c = str[i];
      //Handle decimal point
      if (c == '.') {
        int dot_size = scale;

        for (int dy = 0; dy < dot_size; dy++) {
         for (int dx = 0; dx < dot_size; dx++) {
           plot_pixel(current_x + dx, y_start + digit_size - (2 * scale) + dy, 0xFFFF);
         }
        }
        current_x += (scale * 2) + spacing; //move to next character
        continue;
      }

      int template_idx = -1;
      for (int t = 0; t < MAX_TEMPLATES; t++) {
        if (template_arr[t].label == c) { //match digit/character
          template_idx = t;
          break;
        }
      }

        if (template_idx != -1) {
            int min_x = NORM_SIZE, max_x = 0;
            for (int y = 0; y < NORM_SIZE; y++) {
                for (int x = 0; x < NORM_SIZE; x++) {
                    if (template_arr[template_idx].data[y * NORM_SIZE + x] == 0) {
                        if (x < min_x) min_x = x;
                        if (x > max_x) max_x = x;
                    }
                }
            }

            int char_width = (max_x - min_x + 1);

            // --- DRAW CHARACTER ---
            for (int y = 0; y < NORM_SIZE; y++) {
                for (int x = min_x; x <= max_x; x++) {

                    short color =
                        (template_arr[template_idx].data[y * NORM_SIZE + x] == 0)
                        ? 0x0000 : 0xFFFF;

                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {

                            plot_pixel(
                                current_x + ((x - min_x) * scale) + dx,
                                y_start + (y * scale) + dy,
                                color
                            );
                        }
                    }
                }
            }

            // move forward using ACTUAL width (fixes spacing issues)
            current_x += (char_width * scale) + spacing;
        }
    }
}

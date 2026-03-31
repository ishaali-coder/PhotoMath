#include <stdio.h>

#include "address_map.h"
#include "templates.h"

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
// float calculate_aspect_ratio_templ(Template* template, int i);
void solve_equation(char* equation, int num_symbols);
long correlated_sad(short* image, const Template* tmpl, int j);

int main(void) {
  // pointers to video-in dma controller
  volatile int* video_in_dma = (int*)VIDEO_IN_BASE;
  volatile int* edge_detect = (int*)EDGE_DETECT_CTRL_BASE;

  // pointer to video out pixel buffer controller
  volatile int* pixel_buf_ctrl = (int*)PIXEL_BUF_CTRL_BASE;
  volatile int* KEY_ptr = (int*)KEY_BASE;

  *(KEY_ptr + 3) = 0xF;  // clear edge capture register initially

  // direct video feed from camera ->vga
  *(video_in_dma + 1) = 0x08000000;  // set camera to write directly to vga buffer 0x08 default adress

  *edge_detect = 0x0;  // disable edge detection

  // enable the video dma controller (set en bit), EN = 1
  //  cntrol register is at 0xFF20306C (offset 3 from VIDEO_IN_BASE)
  *(video_in_dma + 3) = 0x4;
  *pixel_buf_ctrl = 1;  // wriitng 1 to Buffer register triggers display - enable VGA output

  while (1) {
    int edge_capture = *(KEY_ptr + 3);

    if (edge_capture & 0x1) {  // check if key0 was pressed and released

      *(KEY_ptr + 3) = 0x1;       // clearedge capture bit by writing 1 to first bit
      *(video_in_dma + 3) = 0x0;  // takes a pic (enable goes from 1--> 0)

      BoundingBox boxes[MAX_SYMBOLS];
      short image[TOTAL];

      adaptive_threshold(image);
      int num_symbols = segment_symbols(image, boxes);
      printf("%d", num_symbols);
      short normalized[NORM_SIZE * NORM_SIZE];

      // now show cropped pieces
    //   for (int i = 0; i < num_symbols; i++) {
    //     normalize_symbol(image, boxes[i], normalized);
    //     display_symbol(normalized);
    //     delay();
    //   }

      // wait til key press to confirm

      while (!(*(KEY_ptr + 3) & 0x2) && !(*(KEY_ptr + 3) & 0x4));

      // if KEY1 is pressed then keep this image
      // if KEY2 is pressed go back and try again

      if (*(KEY_ptr + 3) & 0x2) {

        char equation[MAX_SYMBOLS];

        for (int x = 0; x < MAX_SYMBOLS; x ++) {
          equation[x] = ' ';

        }


        // find lowest difference between the image and each number
        for (int sym_i = 0; sym_i < num_symbols; sym_i++) {
          
          normalize_symbol(image, boxes[sym_i], normalized);
            display_symbol(normalized);
            delay();

          int template_i;                    // used to loop throough the array of presaved
                                             // templates
          int symbol_i_start, symbol_i_max;  // used to loop through all possible symbols

          if (sym_i == num_symbols / 2) {  //  --> assuming operator is always in the middle

            template_i = OPERATOR_START_I;
            symbol_i_start = 10;
            symbol_i_max = 14;

          } else {  // if we're not in the middle then we're looking at a digit
            // digits are first in both the symbol and template arrays
            template_i = 0;
            symbol_i_start = 0;
            symbol_i_max = 10;
          }

          long best_score_overall_sad = 1000000;
          char best_match_sad;
          //float ratio;

          // for each possible symbol in the symbol array
          // caluclate the difference between the input image and each template
          // we have saved for that digit use the max score out of all templates
          // for a symbol when comparing to other symbols this makes sure each
          // symbol "gets a starting chance"

          for (int symbol_i = symbol_i_start; symbol_i < symbol_i_max; symbol_i++) {
            int min_sad_score = 1000000;  // xtart with a high number as we want
                                          // to minimize difference

            const char label = symbol_arr[symbol_i].label;
            // label of the symbol we're currently looking at

            while (template_arr[template_i].label == label) {
              long temp_sad_score = correlated_sad(normalized, template_arr, template_i);

              if (temp_sad_score < min_sad_score) {
                // store if we have found a new minimum ifference fr thsi symbol
                min_sad_score = temp_sad_score;
              }
              template_i++;
            }

            // at the end of checking all templates for this symbol compare to
            // the best match we have found overall if this score is a
            // mincompared to the min score of all other templates its our new
            // best match
            if (min_sad_score < best_score_overall_sad) {
              best_score_overall_sad = min_sad_score;
              best_match_sad = label;
              //ratio = template_arr[template_i].aspect_ratio / calculate_aspect_ratio_input(normalized);  // only calculate aspect ratio for our current best match
            }

            // if (ratio < 0.5 || ratio > 0.2) {  // if aspect ratio is very different then we know its not a good match even if the sad score is low
            //   printf("unsure\n");              // set to unknown symbol
            // }

            // now test to see if we get unsure when the algirthm gets the wrong output
            //--> see if aspect ratio is helpful for catching cases just sad cant determine accurately by itself
            // tune bounds
          }
           printf("Best SAD Match: %c with score %ld\n", best_match_sad,
                   best_score_overall_sad);

            equation[sym_i] = best_match_sad;

            delay();
        }

        *(KEY_ptr + 3) = 0xF;  // clear all keys

        solve_equation(equation, num_symbols);

        *(video_in_dma + 3) = 0x4;  // going back to video mode
      }
    }
  }
  return 0;
}
float solve_equation(char* equation, int n) {

  char op;

  Operator operators[4];
  
  int i_op = 0; //look for operator index and figure out what operation we're trying to 
  // for (int i = 0; i < n && i_op < 4; i++) {

  //   char sym = equation[i];

  //   if ((sym == '+' || sym == '-' || sym == 'x' || sym == '/')) {
  //     operators[i_op].label = sym;
  //     operators[i_op].index = i;
  //     operators[i_op].priority = (sym == '+' || sym == '-') ? 1 : 2; // assign priority based on operator type
      
  //     i_op++;
  //   }



  // }

  

  float eq = 0.0;
  float second_num = 0.0;
  int num_digits_first = i_op;
  int num_digits_second = n - i_op - 1;

  for (int i = 0; i < n && i_op == 1; i ++ ) {

    char sym = equation[i];

    //if we're at the first digit and first digit is ones

    if (i < i_op && num_digits_first == 1) { 
      eq += (sym - '0'); //if this is the first number in the eq just add it in
      //now eq = first_dig

    } else if (i < i_op && num_digits_first > 1) {//first number has more than 1 digit
      eq = eq * (10) + (sym - '0'); //multply old number by 10 to shift to tens and add new number in ones place

    } else if (i > i_op && num_digits_second == 1) {
      second_num = (sym - '0'); //second num = equation[n-1]
 
    } else if (i > i_op && num_digits_second > 1) {
      second_num = second_num * 10 + (sym - '0'); //build second number  
    } 

    //only finish evaluatiing the equation when the second number has been fully reconstructed
    if (i == n-1) {
        if (op == '+') eq += second_num; //addition mode
        else if (op == '-') eq -= second_num; //subtraction mode
        else if (op == 'x') eq *= second_num ; //multiplication mode
        else if (op == '/') {
           //division mode
          if (second_num == 0) {
            printf("\nError: Division by zero\n");
            
          } else {
            eq = (float) eq / (float) second_num;
          }
        }
    }
  }

  printf("\nAnswer: %.1f\n", eq);
  return eq;
}

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

  // Apply filter before thresholding
  median_filter(image);

  // Create a temporary buffer to store the new binary image.
  static short binary_image[TOTAL];

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

  // Copy binary image to image
  // AND send image tp buffer
  for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      // edge forcing band logic
      if (y < Y_START || y > Y_END || x < EDGE_IGNORE ||
          x > IMAGE_WIDTH - EDGE_IGNORE - 1 || y > IMAGE_HEIGHT - 1) {
        image[y * IMAGE_WIDTH + x] = 0xFFFF;  // Force it white
        *(pixel_buf_address + (y << 9) + x) = 0xFFFF;
      } else {
        image[y * IMAGE_WIDTH + x] = binary_image[y * IMAGE_WIDTH + x];
        *(pixel_buf_address + (y << 9) + x) = binary_image[y * IMAGE_WIDTH + x];
      }
    }
  }
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

void display_cropped_symbol(
    short* image, BoundingBox box) {  // function takes binary image and
                                      // coordinates of one detected symbol
  clear_screen();

  int symbol_width =
      box.x_max - box.x_min +
      1;                                          // calculates width of symbol; +1 beacuse pixel indices are inclusive
  int symbol_height = box.y_max - box.y_min + 1;  // height of symbol

  // Find centre of screen (160, 120)
  int screen_center_x = IMAGE_WIDTH / 2;
  int screen_center_y = IMAGE_HEIGHT / 2;

  // compute top left of symbol so its centered
  int offset_x = screen_center_x -
                 symbol_width / 2;  // left position = centre - half width so
                                    // midle aligns with screen centre
  int offset_y = screen_center_y - symbol_height / 2;

  for (int y = box.y_min; y <= box.y_max;
       y++) {  // loop thorught symbols vertical and horizontal region
    for (int x = box.x_min; x <= box.x_max; x++) {
      short pixel =
          image[y * IMAGE_WIDTH + x];  // get pixel value from original image

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

// float calculate_aspect_ratio_templ(Template* template, int i) {
//   int x_min = NORM_SIZE;
//   int y_min = NORM_SIZE;
//   int x_max = 0;
//   int y_max = 0;

//   // figure out bounding box of normalized template

//   for (int x = 0; x < NORM_SIZE; x++) {
//     for (int y = 0; y < NORM_SIZE; y++) {

//       //if we find a black pixel in the templtae
//       if (template[i].data[y * NORM_SIZE + x] == 0) {

//         //check if it occurs earlier than our old first
//         if (x < x_min) {
//           x_min = x;
//         }
//         //or later than our previously found last
//         if (x > x_max) {
//           x_max = x;
//         }

//         //similarly for y
//         if (y < y_min) {
//           y_min = y;
//         }
//         if (y > y_max) {
//           y_max = y;
//         }
//       }
//     }
//   }

//   if (y_max == y_min || x_max < x_min || y_max < y_min) return -1.0; //avoid infinite aspect ratios

//   return ((float)(x_max - x_min) / (float)(y_max - y_min));
// }

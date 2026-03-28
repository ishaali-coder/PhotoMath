//header file wiht the templates for digits and operators
#ifndef TEMPLATES
#define TEMPLATES

#include <string.h>
#include <stdint.h>

//5 samples per digit/operator (13) --> need 5 x 13 = 60 templates
#define MAX_TEMPLATES 60
#define TEMPLATE_BYTES (28 * 28)

//template data type is used for each sample
//holds info on what operator / digit it is
typedef struct {
    const char label;
    float aspect_ratio;
    uint8_t data[TEMPLATE_BYTES];
} Template;

extern Template template_arr[MAX_TEMPLATES];

#endif

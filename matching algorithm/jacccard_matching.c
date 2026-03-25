float jaccard_similarity(short* image, const Template* template);
int best_template_match(short* image);

float jaccard_similarity(short* image, const Template* template) {
  float union_pixels = 0.0;
  float intersection = 0.0;

  //takes ratio of union over intersection
  //how many pixels are black in BOTH images / how many pixels are black in EITHER image
  for (int i = 0; i < TOTAL; i++) {
    short i_pixel = (image[i] == 0x0000);
    short t_pixel = (template.data[i] == 0x0000);


    union_pixels += i_pixel | t_pixel;
    intersection += i_pixel & t_pixel;
  }


  return (union_pixels / intersection);
}


int best_template_match(short* image) {
  float max_jaccard_similarity = 0.0;
  int best_i;


  for (int i = 0; i < NUM_TEMPLATES; i++) {
    float similarity = jaccard_similarity(image, template_arr[i]);

    //jaccard similarity score ranges from 0 to 1
    //best possible score is a 1 --> images are identical
    //worst possible socre is a 0 --> no pixels in common
    if (similarity > max_jaccard_similarity) {
      max_jaccard_similarity = similarity;
      best_i = i;
    }
  }


  return best_i;
}

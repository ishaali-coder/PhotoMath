
#include <stdio.h>

void solve_equation(char* equation, int n);
int main() {

    char equation[9];

        // for (int x = 0; x < MAX_SYMBOLS; x ++) {
        //   equation[x] = ' ';

        // }


    equation[0] = '1';
    equation[1] = '1';
    equation[2] = '+';
    equation[3] = '1';

    solve_equation(equation, 4);

    equation[0] = '4';
    equation[1] = '1';
    equation[2] = 'x';
    equation[3] = '3';

        solve_equation(equation, 4);

    




    return 0;
}


void solve_equation(char* equation, int n) {

  //for (int n = 0; n < MAX_SYMBOLS && (equation[n] != ' '); n++); 

  int eq= 0;
  int mode = 0; //o add
  //1 sub
  //2 mult
  //3 div

  /* Test case to debug" 4Best SAD Match: 1 with score 58
Best SAD Match: 1 with score 49
Best SAD Match: + with score 135
Best SAD Match: 2 with score 101

Answer: 17*/

  for (int i = 0; i < n; i ++ ) {

    char sym = equation[i];

    if (i < n/2 && n == 3) {
      eq += (sym - '0');


    } else if (i < n/2 && n > 3) {//numbers with more than 1 digit
      eq = eq * (10) + (sym - '0'); //multply old number by 10 to shift to tens and add new number in ones place


    } else if (i == n/2) {

      if (sym == '-') mode +=1; 
      else if (sym == 'x') mode += 2;
      else if (sym == '/') mode += 3;



    } else if (i > n/2) {
      
      if (mode == 0) {eq = eq + (sym - '0');}
      else if (mode == 1) {eq = eq - (sym - '0');}
      else if (mode == 2) { eq = eq * (sym - '0') ;}
      else if (mode == 3) {eq = eq / (sym - '0'); }


 
    }

  }

  printf("\nAnswer: %d\n", eq);

}
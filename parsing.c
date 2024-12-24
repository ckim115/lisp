#include "mpc.h" // For parsing (-lm)
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <string.h>

static char buffer[2048];

/* Fake readline function */
char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

/* Fake add_history function */
void add_history(char* unused) {}

/* otherwise include the editline headers */
#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

/* Struct for handling errors */
typedef struct {
  int type;
  long num;
  int err;
} lval;

/* Enum for possible lval types */
enum { LVAL_NUM, LVAL_ERR };

/* Enum for possibl error types */
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

/* Creates a new number type lval */
lval lval_num(long x) {
  lval v;
  v.type = LVAL_NUM;
  v.num = x;
  return v;
}

/* Creates a new error type lval */
lval lval_err(long x) {
  lval v;
  v.type = LVAL_ERR;
  v.err = x;
  return v;
}

/* Print an lval */
void lval_print(lval v) {
  switch (v.type) {
    /* In the case the type is a number print its, then break */
    case LVAL_NUM: printf("%li", v.num); break;
    
    /* In the case the type is an error */
    case LVAL_ERR:
      /* Check what kind of error and print */
      if (v.err == LERR_DIV_ZERO) {
        printf("Error: Division By Zero!");
      }
      if (v.err == LERR_BAD_OP) {
        printf("Error: Invalid Operator!");
      }
      if (v.err == LERR_BAD_NUM) {
        printf("Error: Invalid Number!");
      }
    break;
  }
}

/* Print lval with newline */
void lval_println(lval v) {
  lval_print(v);
  putchar('\n');
}

/* Use operator string to determine which operation to perform */
lval eval_op(lval x, char* op, lval y) {
  
  /* If either value is an error, return it */
  if (x.type == LVAL_ERR) { return x; }
  if (y.type == LVAL_ERR) { return y; };
  
  /* Otherwise do the math */
  if (strcmp(op, "+") == 0) { return lval_num(x.num + y.num); }
  if (strcmp(op, "-") == 0) { return lval_num(x.num - y.num); }
  if (strcmp(op, "*") == 0) { return lval_num(x.num * y.num); }
  if (strcmp(op, "/") == 0) { 
    /* If second operand is zero return error */
    return y.num == 0
      ? lval_err(LERR_DIV_ZERO)
      : lval_num(x.num / y.num); 
  }
  
  return lval_err(LERR_BAD_OP);
}

/* Evaluate the parse tree */
lval eval(mpc_ast_t* t) {
  
  /* If tagged as number, return directly */
  if (strstr(t->tag, "number")) {
    /* Check if some error in conversion */
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    /* ERANGE checks if out of bounds of range */
    return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
  }
  
  /* The first operator is always '(', the second is always an operator */
  char* op = t->children[1]->contents;
  
  /* Store third child (num/expression) in x */
  lval x = eval(t->children[2]);
  
  /* Iterate the remaining children and combine */
  int i = 3;
  while (strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }
  
  return x;
}

int main(int argc, char** argv) {
  /* Create some parsers */
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");
  
  /* Define with following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
    "								\
      number	: /-?[0-9]+/ ;					\
      operator	: '+' | '-' | '*' | '/' ;			\
      expr	: <number> | '(' <operator> <expr>+ ')' ;	\
      lispy	: /^/ <operator> <expr>+ /$/ ;			\
    ",
    Number, Operator, Expr, Lispy);
  
  /* Print Version and Exit Information */
  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");
  
  /* In a never ending loop */
  while (1) {
    
    /* Output prompt and get input */
    char* input = readline("lispy> ");
    
    /* Add input to history */
    add_history(input);
    
    /* Attempt to parse user input */
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      /* On success print the AST */
      lval result = eval(r.output);
      lval_println(result);
      mpc_ast_delete(r.output);
    } else {
      /* Otherwise print error */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    
    /* Free retrieved input */
    free(input);
  }
  
  /* Undefine and Delete the Parser */
  mpc_cleanup(4, Number, Operator, Expr, Lispy);
  
  return 0;
} 

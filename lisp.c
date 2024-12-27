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

/* Enum for possible lval types */
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

/* Struct for handling errors */
typedef struct lval {
  int type;
  long num;
  /* Error and Symbol types have some string data; will need to free */
  char* err;
  char* sym;
  /* Count and Pointer to a list of lval* */
  int count;
  struct lval** cell;
} lval;

/* Construct a pointer to a new Number lval */
lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

/*Construct a pointer to a new Error lval */
lval* lval_err(char* m) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

/*Construct a pointer to a new Symbol lval */
lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

/*Construct a pointer to a new empty Sexpr lval */
lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/*Construct a pointer to a new empty Qexpr lval */
lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* Deleting (freeing) an lval */
void lval_del(lval* v) {
  
  switch (v->type) {
    /* Do nothing for number type */
    case LVAL_NUM: break;
    /* For Err of Sym free the string data */
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    
    /* If Qexpr/Sexpr then delete all elements inside cell */
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      for (int i = 0; i < v->count; i++) {
        lval_del(v->cell[i]);
      }
      /* Also free the memory allocated to cell itself */
      free(v->cell);
    break;
  }
  
  /* Free memory allocated to lval struct */
  free(v);
}

/* Add to the expression in the Sexpr expression list */
lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

/* Extract a single element from an Sexpr */
lval* lval_pop(lval* v, int i) {
  /* Find the item at i */
  lval* x = v->cell[i];
  
  /* Shift memory after the item at i over the top */
  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));
  /* Decrease count of items in list */
  v->count--;
  
  /* Reallocate the memory used */
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

/* Functions like lval_pop but deletes list that element was extracted from */
lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

void lval_print(lval* v);

/* Loops over all the sub-expressions of an expression and prints these 
   individually separated by spaces, in the same way they are input. */
void lval_expr_print(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    /* Print value contained within */
    lval_print(v->cell[i]);
    
    /* Don't print trailing space if last element */
    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

/* Print an lval */
void lval_print(lval* v) {
  switch (v->type) {
    /* In the case the type is a number print its, then break */
    case LVAL_NUM:	printf("%li", v->num); break;
    /* In the case the type is an error */
    case LVAL_ERR:	printf("Error: %s", v->err); break;
    case LVAL_SYM:	printf("%s", v->sym); break;
    case LVAL_SEXPR:	lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR:	lval_expr_print(v, '{', '}'); break;
  }
}

/* Print lval with newline */
void lval_println(lval* v) {
  lval_print(v);
  putchar('\n');
}

/* Function performing calculator commands */
lval* builtin_op(lval* a, char* op) {
  
  /* Ensure all arguments are numbers */
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }
  
  /* Pop first element */
  lval* x = lval_pop(a, 0);
  
  /* If no arguments and sub then perform unary negation */
  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }
  
  /* While there are still elements remaining */
  while (a->count > 0) {
    
    /* Pop the next element */
    lval* y = lval_pop(a, 0);
    
    /* Perform operations */
    if (strcmp(op, "+") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0) { x->num *= y->num; }
    if (strcmp(op, "/") == 0) { 
      /* If second operand is zero return error */
      if (y->num == 0) {
        lval_del(x);
        lval_del(y);
        x = lval_err("Division By Zero!"); 
        break;
      }
      x->num /= y->num;
    }
    /* Delete element now finished with */
    lval_del(y);
  }
  /* Delete input expression and return result */
  lval_del(a);
  return x;
}

lval* lval_eval(lval*v);

lval* builtin(lval* v, char* func);

/* Evaluate the Sexpr */ 
lval* lval_eval_sexpr(lval* v) {
  
  /* Evaluate Children */
  for(int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
  }
  
  /* Error Checking */
  for (int i = 0; i < v->count; i++) {
    if(v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }
  
  /* Empty Expression */
  if (v->count == 0) { return v; }
  
  /* Single Expression */
  if(v->count == 1) { return lval_take(v, 0); }
  
  /* Ensure first element is a Symbol */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression does not start with symbol!");
  }
  
  /* Call builtin with operator */
  lval* result = builtin(v, f->sym);
  lval_del(f);
  return result;
}

/* Evaluate lval */
lval* lval_eval(lval* v) {
  /* Evaluate Sexpr */
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
  /* All other lval types remain the same */
  return v;
}

/* Read node tagged as number */
lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

/* Read a node */
lval* lval_read(mpc_ast_t* t) {
  
  /* If Symbol or Number return conversion to that type */
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }
  
  /* If root (>) or sexpr then create empty list */
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0)  { x = lval_sexpr(); }
  if (strcmp(t->tag, "sexpr"))  { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }
  
  /* Fill this list with any valid expression contained within */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  
  return x;
}

/* Builtin functions for handling qexprs */
/* Macro for handling error conditions in builtin functions 
     if condition not met, return error */
#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }

/* Function that pops the first item of a list and removes the list */
lval* builtin_head(lval* a) {
  /* Must pass exactly 1 arguement, which is a qexpr */
  LASSERT(a, a->count == 1,
    "Function 'head' passed too many arguements!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'head' passed incorrect type!");
  LASSERT(a, a->cell[0]->count != 0,
    "Function 'head' passed {}");
    
  lval* v = lval_take(a, 0);
  /* Keep popping only 1 item remaining in v (head) */
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

/* Function that pops and delete the first item of a list, returning hte list */
lval* builtin_tail(lval* a) {
  LASSERT(a, a->count == 1,
    "Function 'head' passed too many arguements!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'head' passed incorrect type!");
  LASSERT(a, a->cell[0]->count != 0,
    "Function 'head' passed {}");
  
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

/* Function that converts a sexpr to a qexpr */
lval* builtin_list(lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

/* Function that converts a qexpr to a sexpr and evaluates */
lval* builtin_eval(lval* a) {
  LASSERT(a, a->count == 1,
    "Function 'eval' passed too many arguements!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'eval' passed incorrect type!");
    
  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(x);
}

/* Helper function to join qexprs in builtin_join. Qexprs can contain multiple
     sexprs so lval_add cannot be used directly */
lval* lval_join(lval* x, lval* y) {
  /* For each cell in 'y' add it to 'x' */
  while (y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }
  
  /* Delete empty 'y' and return 'x' */
  lval_del(y);
  return x;
}

/* Function that joins multiple qexprs into one */
lval* builtin_join(lval* a) {
  /* Check if all qexprs */
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
      "Function 'join' passed incorrect type!");
  }
  
  lval* x = lval_pop(a, 0);
  
  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }
  
  lval_del(a);
  return x;
}

/* Function that calls correct builtin function depending on what symbols are 
     encountered */
lval* builtin(lval* a, char* func) {
  if (strcmp("list", func) == 0) { return builtin_list(a); }
  if (strcmp("head", func) == 0) { return builtin_head(a); }
  if (strcmp("tail", func) == 0) { return builtin_tail(a); }
  if (strcmp("join", func) == 0) { return builtin_join(a); }
  if (strcmp("eval", func) == 0) { return builtin_eval(a); }
  if (strstr("+-/*", func)) { return builtin_op(a, func); }
  printf("%s\n", func);
  lval_del(a);
  return lval_err("Unknown Function!");
}

int main(int argc, char** argv) {
  /* Create some parsers */
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");
  
  /* Define with following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
    "								\
      number	: /-?[0-9]+/ ;					\
      symbol	: \"list\" | \"head\" | \"tail\"		\
      		| \"join\" | \"eval\" | '+' | '-' | '*' | '/' ;	\
      sexpr	: '(' <expr>* ')' ;				\
      qexpr	: '{' <expr>* '}' ;				\
      expr   : <number> | <symbol> | <sexpr> | <qexpr> ;	\
      lispy	: /^/ <expr>* /$/ ;				\
    ",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  /* Print Version and Exit Information */
  puts("Lispy Version 0.0.0.0.5");
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
      lval* x = lval_eval(lval_read(r.output));
      lval_println(x);
      lval_del(x);
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
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  return 0;
} 

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

/* Forward Declaration */
/* structure for storing different lisp value types */
struct lval;
/* structure which stores the name and value of everything named in our program */
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;	

/* Lisp Value */
/* Enum for possible lval types */
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUN };

/* New function pointer type declaration lbuiltin.
    To get an lval*, we dereference lbuiltin and call with lenv* and lval* */
typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
  int type;
  
  long num;
  /* Error and Symbol types have some string data; will need to free */
  char* err;
  char* sym;
  /* if type LVAL_FUN, holding function */
  lbuiltin fun; 
  
  /* Count and Pointer to a list of lval* */
  int count;
  lval** cell;
};

struct lenv {
  int count;
  char** syms;
  lval** vals;
};

/* Construct a pointer to a new Number lval */
lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

/*Construct a pointer to a new Error lval */
lval* lval_err(char* m, ...) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  
  /* Create a va_list and initialize with va_start */
  va_list va;
  va_start(va, m);
  
  /* Allocate 512 bytes of space */
  v->err = malloc(512);
  
  /* printf the error string with a maximum of 511 bytes */
  vsnprintf(v->err, 511, m, va);
  
  /* Reallocate to number of bytes actually needed*/
  v->err = realloc(v->err, strlen(v->err) + 1);
  
  /* Cleanup va list */
  va_end(va);
  
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

/* Construct a pointer to a new empty Qexpr lval */
lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* Construct a pointer to a new function lval */
lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->fun = func;
  return v;
}

/* Copy and return an lval */
lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;
  
  switch (v->type) {
    case LVAL_NUM: x->num = v->num; break;
    case LVAL_FUN: x->fun = v->fun; break;
    case LVAL_ERR:
      x->err = malloc(strlen(v->err)+1);
      strcpy(x->err, v->err);
      break;
    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym)+1);
      strcpy(x->sym, v->sym);
      break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for (int i = 0; i < x->count; i++) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
      break;
  }
  
  return x;
}

/* Deleting (freeing) an lval */
void lval_del(lval* v) {
  
  switch (v->type) {
    /* Do nothing for number type or function type */
    case LVAL_NUM: break;
    case LVAL_FUN: break;
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

/* Initializes a new environment */
lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

char* ltype_name(int t);

/* Deletes an environment */
void lenv_del(lenv* e) {
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

lval* lenv_get(lenv* e, lval* k) {
  /* Iterate over all items of the environment */
  for (int i = 0; i < e->count; i++) {
    /* Check if symbols match and return if so */
    if (strcmp(e->syms[i], k->sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }
  
  /* No symbol found */
  return lval_err("Unbound Symbol '%s'", k->sym);
}

/* Replace an existing value or put a new value into the environment */
void lenv_put(lenv* e, lval* k, lval* v) {
  /* Iterate over all items of the environment */
  for (int i = 0; i < e->count; i++) {
    /* If symbols matches replace lval */
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }
  
  /* Symbol not found; allocate space and add to lists */
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);
  
  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym)+1);
  strcpy(e->syms[e->count-1], k->sym);
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
    case LVAL_FUN:	printf("<function>"); break;
  }
}

/* Print lval with newline */
void lval_println(lval* v) {
  lval_print(v);
  putchar('\n');
}

/* Function performing calculator commands */
lval* builtin_op(lenv* e, lval* a, char* op) {
  /* Ensure all arguments are numbers */
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Function '%s' passed incorrect type for argument 1. "
      "Expected %s.", 
      op, ltype_name(LVAL_NUM));
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
        x = lval_err("Division By Zero."); 
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

lval* lval_eval(lenv* e, lval*v);

lval* builtin(lenv* e, lval* v, char* func);

/* Evaluate the Sexpr */ 
lval* lval_eval_sexpr(lenv* e, lval* v) {
  /* Evaluate Children */
  for(int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }
  
  /* Error Checking */
  for (int i = 0; i < v->count; i++) {
    if(v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }
  
  /* Empty/Single Expression */
  if (v->count == 0) { return v; }
  if(v->count == 1) { return lval_take(v, 0); }
  
  /* Ensure first element is a function after evaluation */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval_del(f);
    lval_del(v);
    return lval_err("First element is not a function. Got %s, expected %s",
      ltype_name(f->type), ltype_name(LVAL_FUN));
  }
  
  /* If so call function to get result */
  lval* result = f->fun(e, v);
  lval_del(f);
  return result;
}

/* Evaluate lval */
lval* lval_eval(lenv* e, lval* v) {
  if (v->type == LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }
  
  /* Evaluate Sexpr */
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
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
#define LASSERT(args, cond, m, ...) \
  if (!(cond)) { \
    lval* err = lval_err(m, ##__VA_ARGS__); \
    lval_del(args); \
    return err; \
    }

/* String names for lval types */
char* ltype_name(int t) {
  switch(t) {
    case LVAL_FUN: return "Function";
    case LVAL_NUM: return "Number";
    case LVAL_ERR: return "Error";
    case LVAL_SYM: return "Symbol";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    default: return "Unknown";
  }
}

/* Function that pops the first item of a list and removes the list */
lval* builtin_head(lenv* e, lval* a) {
  /* Must pass exactly 1 arguement, which is a qexpr */
  LASSERT(a, a->count == 1,
    "Function 'head' passed too many arguements. "
    "Got %i, expected %i.",
    a->count, 1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'head' passed incorrect type. "
    "Got %s, expected %s.",
    ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0,
    "Function 'head' passed {}");
    
  lval* v = lval_take(a, 0);
  /* Keep popping only 1 item remaining in v (head) */
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

/* Function that pops and delete the first item of a list, returning hte list */
lval* builtin_tail(lenv* e, lval* a) {
  LASSERT(a, a->count == 1,
    "Function 'head' passed too many arguements. "
    "Got %i, expected %i.",
    a->count, 1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'head' passed incorrect type. "
    "Got %s, expected %s.",
    ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0,
    "Function 'head' passed {}");
  
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

/* Function that converts a sexpr to a qexpr */
lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

/* Function that converts a qexpr to a sexpr and evaluates */
lval* builtin_eval(lenv* e, lval* a) {
  LASSERT(a, a->count == 1,
    "Function 'eval' passed too many arguements!"
    "Got %i, expected %i",
    a->count, 1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'eval' passed incorrect type!"
    "Got %s, expected %s",
    ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    
  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
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
lval* builtin_join(lenv* e, lval* a) {
  /* Check if all qexprs */
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
      "Function 'join' passed incorrect type. "
      "Got %s, expected %s",
      ltype_name(a->cell[i]->type), ltype_name(LVAL_QEXPR));
  }
  
  lval* x = lval_pop(a, 0);
  
  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }
  
  lval_del(a);
  return x;
}

/* Builtin math operations */
lval* builtin_add(lenv* e, lval* a) {
  return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
  return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
  return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
  return builtin_op(e, a, "/");
}

/* Create a symbol lval and function lval with the given name */
void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);
}

/* Allow user to define own function */
lval* builtin_def(lenv* e, lval* a) {
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'def' passed incorrect type. "
    "Got %s, expected %s",
    ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  
  
  /* First argument is symbol list (ex 'x' in {x} 20) */
  lval* syms = a->cell[0];
  
  /* Ensure all elements of first list are symbols */
  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM,
      "Function 'def' cannot define non-symbol. "
      "Got %s, expected %s.",
      ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
  }
  
  /* Check correct number of symbols and values (ensure its not {x} 10 20, etc) */
  LASSERT(a, syms->count == a->count-1,
    "Function 'def' cannot define incorrect number of values to symbols. "
    "Number of symbols was %i, number of values was %i",
    syms->count, a->count-1);

  /* Assign copies of values to symbols */
  for (int i = 0; i < syms->count; i++) {
    lenv_put(e, syms->cell[i], a->cell[i+1]);
  }
  
  lval_del(a);
  return lval_sexpr(); /* empty () if done correctly */
}

/* Add builtin functions to environment */
void lenv_add_builtins(lenv* e) {
  /* Variable Functions */
  lenv_add_builtin(e, "def", builtin_def);
  
  /* List Functions */
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  
  /* Mathematical Functions */
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
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
    "							\
      number	: /-?[0-9]+/ ;				\
      symbol	: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;	\
      sexpr	: '(' <expr>* ')' ;			\
      qexpr	: '{' <expr>* '}' ;			\
      expr   : <number> | <symbol> | <sexpr> | <qexpr> ;\
      lispy	: /^/ <expr>* /$/ ;			\
    ",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  /* Print Version and Exit Information */
  puts("Lispy Version 0.0.0.0.5");
  puts("Press Ctrl+c to Exit\n");
  
  /* Create new environment and add builtin functions */
  lenv* e = lenv_new();
  lenv_add_builtins(e);
  
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
      lval* x = lval_eval(e, lval_read(r.output));
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
  
  /* Delete environment */
  lenv_del(e);
  
  /* Undefine and Delete the Parser */
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  return 0;
} 

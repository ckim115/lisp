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
/* parser pointers */
mpc_parser_t* Number;
mpc_parser_t* Symbol;
mpc_parser_t* Sexpr;
mpc_parser_t* Qexpr;
mpc_parser_t* Expr;
mpc_parser_t* Lispy;
mpc_parser_t* String;
mpc_parser_t* Comment;
/* structure for storing different lisp value types */
struct lval;
/* structure which stores the name and value of everything named in our program */
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;	

/* Lisp Value */
/* Enum for possible lval types */
enum { LVAL_NUM, LVAL_DOUBLE, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUN, LVAL_STR };

/* New function pointer type declaration lbuiltin.
    To get an lval*, we dereference lbuiltin and call with lenv* and lval* */
typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
  int type;
  
  double num;
  /* Error Symbol and String types have some string data; will need to free */
  char* err;
  char* sym;
  char* str;
  
  /* If type LVAL_FUN, holding function. If user-defined, NULL*/
  lbuiltin builtin; 
  lenv* env;
  /* Formal arguements and function body if user-defined function */
  lval* formals;
  lval* body;
  
  /* Count and Pointer to a list of lval* */
  int count;
  lval** cell;
};

struct lenv {
  lenv* par;
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

/* Construct a pointer to a new Double lval */
lval* lval_double(double x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_DOUBLE;
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

/* Construct a pointer to a new empty Sexpr lval */
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
  v->builtin = func;
  return v;
}

/* Construct a pointer to a new String lval */
lval* lval_str(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->str = malloc(strlen(s) + 1);
  strcpy(v->str, s);
  return v;
}

lenv* lenv_new(void);

/* Construct a pointer to a new user-defined function lval */
lval* lval_lambda(lval* formals, lval* body) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->builtin = NULL;
  
  /* Each function has its own environment */
  v->env = lenv_new();
  v->formals = formals;
  v->body = body;
  return v;
}

lenv* lenv_copy(lenv* e);

/* Copy and return an lval */
lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;
  
  switch (v->type) {
    case LVAL_NUM: 
    case LVAL_DOUBLE: x->num = v->num; break;
    case LVAL_FUN: 
      if(v->builtin) {
        x->builtin = v->builtin; 
      } else {
        x->builtin = NULL;
        x->env = lenv_copy(v->env);
        x->formals = lval_copy(v->formals);
        x->body = lval_copy(v->body);
      }
      break;
    case LVAL_ERR:
      x->err = malloc(strlen(v->err)+1);
      strcpy(x->err, v->err);
      break;
    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym)+1);
      strcpy(x->sym, v->sym);
      break;
    case LVAL_STR:
      x->str = malloc(strlen(v->str)+1);
      strcpy(x->str, v->str);
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

/* Copy and return an environment */
lenv* lenv_copy(lenv* e) {
  lenv* n = malloc(sizeof(lenv));
  n->par = e->par;
  n->count = e->count;
  n->syms = malloc(sizeof(char*) * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);
  for (int i = 0; i < e->count; i++) {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  return n;
}

void lenv_del(lenv* e);

/* Deleting (freeing) an lval */
void lval_del(lval* v) {
  
  switch (v->type) {
    /* Do nothing for number, double, or function type */
    case LVAL_NUM: 
    case LVAL_DOUBLE: break;
    case LVAL_FUN: 
      /* If it is user-defined */
      if(!v->builtin) {
        lenv_del(v->env);
        lval_del(v->formals);
        lval_del(v->body);
      }
      break;
    /* For Err of Sym free the string data */
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    case LVAL_STR: free(v->str); break;
    
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
  e->par = NULL;
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

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
  
  /* Check parent for symbol */
  if (e->par) {
    return lenv_get(e->par, k);
  } else {
    return lval_err("Unbound Symbol '%s'", k->sym);
  }
}

/* Replace an existing value or put a new value into the local environment */
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

/* Define variables in global environment */
void lenv_def(lenv* e, lval* k, lval* v) {
  /* Iterate till e has no parent */
  while (e->par) {
    e = e->par;
  }
  lenv_put(e, k, v);
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

/* Used for printing lvals of the String value as a user might input it,
     taking user escape symbols into account (/n) */
void lval_print_str(lval* v) {
  /* Copy string and pass through escaped function */
  char* escaped = malloc(strlen(v->str)+1);
  strcpy(escaped, v->str);
  
  escaped = mpcf_escape(escaped);
  printf("\"%s\"", escaped);
  free(escaped);
}

/* Print an lval */
void lval_print(lval* v) {
  switch (v->type) {
    /* In the case the type is a number or double print is, then break */
    case LVAL_NUM:	printf("%li", (long) v->num); break;
    case LVAL_DOUBLE:	printf("%f", (double) v->num); break;
    /* In the case the type is an error */
    case LVAL_ERR:	printf("Error: %s", v->err); break;
    case LVAL_SYM:	printf("%s", v->sym); break;
    case LVAL_STR:	lval_print_str(v); break;
    case LVAL_SEXPR:	lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR:	lval_expr_print(v, '{', '}'); break;
    case LVAL_FUN:
      if (v->builtin) {
        printf("<function>"); 
      } else {
        printf("(\\ ");
        lval_print(v->formals);
        putchar(' ');
        lval_print(v->body);
        putchar(')');
      }
      break;
  }
}

/* Print lval with newline */
void lval_println(lval* v) {
  lval_print(v);
  putchar('\n');
}

lval* lval_eval(lenv* e, lval*v);

char* ltype_name(int t);

lval* builtin(lenv* e, lval* v, char* func);
lval* lval_call(lenv* e, lval* f, lval* a);

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
    lval* err = lval_err(
      "S-Expression starts with incorrect type. "
      "Got %s, expected %s",
      ltype_name(f->type), ltype_name(LVAL_FUN));
    lval_del(f);
    lval_del(v);
    return err;
  }
  
  /* If so call function to get result */
  lval* result = lval_call(e, f, v);
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

/* Read node tagged as number. Check if double or integer */
lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  if (strchr(t->contents, '.')) {
    double d = strtod(t->contents, NULL);
    return errno != ERANGE ? lval_double(d) : lval_err("invalid number");
  }
  long i = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(i) : lval_err("invalid number");
}

/* Deals with reading user input strings as they are in an escape format */
lval* lval_read_str(mpc_ast_t* t) {
  /* Cut off final quote char */
  t->contents[strlen(t->contents)-1] = '\0';
  /* Copy in string without first quote char */
  char* unescaped = malloc(strlen(t->contents+1)+1);
  strcpy(unescaped, t->contents+1);
  
  unescaped = mpcf_unescape(unescaped);
  lval* str = lval_str(unescaped);
  
  free(unescaped);
  return str;
}

/* Read a node */
lval* lval_read(mpc_ast_t* t) {
  /* If Symbol String or Number return conversion to that type */
  if (strstr(t->tag, "string")) { return lval_read_str(t); }
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
    if (strstr(t->children[i]->tag, "comment")) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  return x;
}

lval* builtin_eval(lenv* e, lval* a);
lval* builtin_list(lenv* e, lval* a);

/* Runs when expression is called and function is evaluated.
     If the number of arguements is less than the formals, return a
     partially evaluated function */
lval* lval_call(lenv* e, lval* f, lval* a) {
  /* If a builtin fxn, just call it */
  if(f->builtin) { 
    return f->builtin(e, a); 
  }
  
  int given = a->count;
  int total = f->formals->count;
  
  /* While arguements still remain to be processed */
  while (a->count) {
    /* If no more formals to bind to */
    if (f->formals->count == 0) {
      lval_del(a);
      return lval_err(
        "Function passed too many arguments. "
        "Got %i, expected %i.", given, total);
    }
    
    /* First symbol arguement pair */
    lval* sym = lval_pop(f->formals, 0);
    /* Special case to deal with '&' from {x & xs} */
    if (strcmp(sym->sym, "&") == 0) {
      /* Ensure '&' is followed by another symbol (list to store x+ variables on */
      if (f->formals->count != 1) {
        lval_del(a);
        return lval_err("Function format invalid. "
          "Symbol '&' not followed by single symbol.");
      }
      
      /* Nest formal should be bound to remaining arguments */
      lval* nsym = lval_pop(f->formals, 0);
      lenv_put(f->env, nsym, builtin_list(e, a));
      lval_del(sym); lval_del(nsym);
      break;
    }
    
    lval* val = lval_pop(a, 0);
    lenv_put(f->env, sym, val);
    
    lval_del(sym);
    lval_del(val);
  }
  
  lval_del(a);
  
  /* If '&' remains in formal list bind to empty list 
    Case where no variable arguments were supplied */
  if(f->formals->count > 0 && 
    strcmp(f->formals->cell[0]->sym, "&") == 0) {
    
    /* Check that & is not passed invalidly */
    if (f->formals->count != 2) {
      return lval_err("Function formal invalid. "
        "Symbol '&' not followed by sigle symbol.");
    }
    
    /* Remove '&' symbol */
    lval_del(lval_pop(f->formals, 0));
    
    /* Pop next symbol and create empty list */
    lval* sym = lval_pop(f->formals, 0);
    lval* val = lval_qexpr();
    
    /* Bind to environment then delete */
    lenv_put(f->env, sym, val);
    lval_del(sym); lval_del(val);
  }
  
  /* If all formals have been bound */
  if (f->formals->count == 0) {
    f->env->par = e;
    /* Return with body in new sexpr */
    return builtin_eval(
      f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
  } else {
    /* Return partially evaluated function */
    return lval_copy(f);
  }
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

#define LASSERT_NUM(f, args, num) \
  if (args->count != num) { \
    lval* err = lval_err("Function '%s' did not pass %li arguements." \
      " Got %i, expected %i.", f, num, args->count, num); \
    lval_del(args); \
    return err; \
    }

#define LASSERT_TYPE(f, args, i, t) \
  if (a->cell[i]->type != t) { \
    lval* err = lval_err("Function '%s' passed incorrect type. " \
      "Got %s, expected %s.", \
      f, ltype_name(a->cell[i]->type), ltype_name(t)); \
    lval_del(args); \
    return err; \
    }
    
/* String names for lval types */
char* ltype_name(int t) {
  switch(t) {
    case LVAL_FUN: return "Function";
    case LVAL_NUM: return "Number";
    case LVAL_DOUBLE: return "Double";
    case LVAL_ERR: return "Error";
    case LVAL_SYM: return "Symbol";
    case LVAL_STR: return "String";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    default: return "Unknown";
  }
}

/* Function performing calculator commands */
lval* builtin_op(lenv* e, lval* a, char* op) {
  /* Ensure all arguments are numbers */
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM && a->cell[i]->type != LVAL_DOUBLE) {
      lval_del(a);
      return lval_err("Function '%s' passed incorrect type for argument 1. "
      "Expected %s or %s.", 
      op, ltype_name(LVAL_NUM), ltype_name(LVAL_DOUBLE));
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
    if (x->type == LVAL_NUM && y->type == LVAL_DOUBLE) x->type = LVAL_DOUBLE;
    
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
    if (strcmp(op, "%") == 0) {
      if (x->type == LVAL_DOUBLE || y->type == LVAL_DOUBLE) {
        x = lval_err("% with doubles.");
        break;
      }
      x->num = (long) x->num % (long) y->num;
    }
    /* Delete element now finished with */
    lval_del(y);
  }
  /* Delete input expression and return result */
  lval_del(a);
  return x;
}

/* Function that pops the first item of a list and removes the list */
lval* builtin_head(lenv* e, lval* a) {
  /* Must pass exactly 1 arguement, which is a qexpr */
  LASSERT_NUM("head", a, 1);
  LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
  LASSERT(a, a->cell[0]->count != 0,
    "Function 'head' passed {}");
    
  lval* v = lval_take(a, 0);
  /* Keep popping only 1 item remaining in v (head) */
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

/* Function that pops and delete the first item of a list, returning hte list */
lval* builtin_tail(lenv* e, lval* a) {
  LASSERT_NUM("tail", a, 1);
  LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
  LASSERT(a, a->cell[0]->count != 0,
    "Function 'tail' passed {}");
  
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
  LASSERT_NUM("eval", a, 1);
  LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);
    
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
    LASSERT_TYPE("join", a, i, LVAL_QEXPR);
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

lval* builtin_mod(lenv* e, lval* a) {
  return builtin_op(e, a, "%");
}

/* Create a symbol lval and function lval with the given name */
void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);
}

lval* builtin_var(lenv* e, lval* a, char* func) {
  LASSERT_TYPE(func, a, 0, LVAL_QEXPR);
  
  
  /* First argument is symbol list (ex 'x' in {x} 20) */
  lval* syms = a->cell[0];
  /* Ensure all elements of first list are symbols */
  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM,
      "Function '%s' cannot define non-symbol. "
      "Got %s, expected %s.",
      func, ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
  }
  
  /* Check correct number of symbols and values (ensure its not {x} 10 20, etc) */
  LASSERT(a, syms->count == a->count-1,
    "Function '%s' cannot define incorrect number of values to symbols. "
    "Number of symbols was %i, number of values was %i",
    func, syms->count, a->count-1);

  /* Assign copies of values to symbols */
  for (int i = 0; i < syms->count; i++) {
    /* If 'def' define in globally. If 'put' define in local */
    if (strcmp(func, "def") == 0) {
      lenv_def(e, syms->cell[i], a->cell[i+1]);
    }
    
    if (strcmp(func, "=") == 0) {
      lenv_put(e, syms->cell[i], a->cell[i+1]);
    }
  }
  
  lval_del(a);
  return lval_sexpr();
}

/* Allow user to define own function */
lval* builtin_def(lenv* e, lval* a) {
  return builtin_var(e, a, "def");
}

/* Define a local variable */
lval* builtin_put(lenv* e, lval* a) {
  return builtin_var(e, a, "=");
}

/* Lambda function builtin */
lval* builtin_lambda(lenv* e, lval* a) {
  /* Check that there are 2 Q-Expression arguements */
  LASSERT_NUM("\\", a, 2);
  LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);
  
  /* Check that first Q-Expression (the formals) only contains Symbols */
  for (int i = 0; i < a->cell[0]->count; i++) {
    LASSERT(a, a->cell[0]->cell[i]->type == LVAL_SYM,
      "Cannot define non-symbol. "
      "Got %s, expected %s.",
      ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
  }
  
  /* Pass the two arguements to lval_lambda */
  lval* formals = lval_pop(a, 0);
  lval* body = lval_pop(a, 0);
  
  return lval_lambda(formals, body);
}

lval* builtin_ord(lenv* e, lval* a, char* op) {
  /* Check if only comparing 2 values */
  LASSERT_NUM(op, a, 2);
      
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_NUM || a->cell[i]->type == LVAL_DOUBLE,
      "Function '%s' passed incorrect type. "
      "Got %s, expected %s or %s",
      op, ltype_name(a->cell[i]->type), ltype_name(LVAL_NUM), ltype_name(LVAL_DOUBLE));
  }
  
  int r;
  if (strcmp(op, ">") == 0) {
    r = (a->cell[0]->num > a->cell[1]->num);
  }
  if (strcmp(op, "<") == 0) {
    r = (a->cell[0]->num > a->cell[1]->num);
  }
  if (strcmp(op, ">=") == 0) {
    r = (a->cell[0]->num > a->cell[1]->num);
  }
  if (strcmp(op, "<=") == 0) {
    r = (a->cell[0]->num > a->cell[1]->num);
  }
  lval_del(a);
  return lval_num(r);
}

lval* builtin_gt(lenv* e, lval* a) {
  return builtin_ord(e, a, ">");
}

lval* builtin_lt(lenv* e, lval* a) {
  return builtin_ord(e, a, "<");
}

lval* builtin_ge(lenv* e, lval* a) {
  return builtin_ord(e, a, ">=");
}

lval* builtin_le(lenv* e, lval* a) {
  return builtin_ord(e, a, "<=");
}

int lval_eq(lval* x, lval* y) {
  /* Different types/values/string values always inequal, BUT doubles/numbers equal*/
  if (x->type != LVAL_NUM && x->type != LVAL_DOUBLE) {
    if(y->type != LVAL_NUM && y->type != LVAL_DOUBLE) {
      if (x->type != y->type) { return 0; }
    }
  }
  /* Compare based on type */
  switch (x->type) {
    case LVAL_NUM:
    case LVAL_DOUBLE: return (x->num == y->num);
    
    case LVAL_ERR: return (strcmp(x->err, y->err) == 0);
    case LVAL_SYM: return (strcmp(x->sym, y->sym) == 0);
    case LVAL_STR: return (strcmp(x->str, y->str) == 0);
    
    case LVAL_FUN: 
      if (x->builtin || y->builtin) {
        return x->builtin == y->builtin;
      } else {
        return lval_eq(x->formals, y->formals)
          && lval_eq(x->body, y->body);
      }
    
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      if (x->count != y->count) { return 0; }
      for (int i = 0; i < x->count; i++) {
        if (!lval_eq(x->cell[i], y->cell[i])) { return 0; }
      }
      return 1;
    break;
  }
  
  return 0;
}

lval* builtin_cmp(lenv* e, lval* a, char* op) {
  /* Check if only comparing 2 arguements */
  LASSERT_NUM(op, a, 2);
      
  int r;
  if (strcmp(op, "==") == 0) {
    r = lval_eq(a->cell[0], a->cell[1]);
  }
  if (strcmp(op, "!=") == 0) {
    r = !lval_eq(a->cell[0], a->cell[1]);
  }
  
  return lval_num(r);
}

lval* builtin_eq(lenv* e, lval* a) {
  return builtin_cmp(e, a, "==");
}

lval* builtin_ne(lenv* e, lval* a) {
  return builtin_cmp(e, a, "!=");
}

lval* builtin_if(lenv* e, lval* a) {
  /* Check if only comparing 3 arguements (1 number, 2 qexprs) */
  LASSERT_NUM("if", a, 3);
  LASSERT(a, a->cell[0]->type == LVAL_NUM || a->cell[0]->type == LVAL_DOUBLE,
    "Function 'if' passed incorrect type. "
    "Got %s, expected %s or %s",
    ltype_name(a->cell[0]->type), ltype_name(LVAL_NUM), ltype_name(LVAL_DOUBLE));
  for (int i = 1; i < a->count; i++) {
    LASSERT_TYPE("if", a, i, LVAL_QEXPR);
  }
  
  /* Mark both expressions as evaluable qexpr -> sexpr */
  lval* x;
  a->cell[1]->type = LVAL_SEXPR;
  a->cell[2]->type = LVAL_SEXPR;
  
  /* if 'if' condition is true, evaluate first expression, else evaluate second */
  if (a->cell[0]->num) {
    x = lval_eval(e, lval_pop(a, 1));
  } else {
    x = lval_eval(e, lval_pop(a, 2));
  }
  
  lval_del(a);
  return x;
}

/* Prints data from running programs */
lval* builtin_print(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    lval_print(a->cell[i]);
    putchar(' ');
  }
  
  putchar('\n');
  lval_del(a);
  
  return lval_sexpr();
}

/* Prints errors */
lval* builtin_error(lenv* e, lval* a) {
  /* Check if it passes in a single string arguement */
  LASSERT_NUM("error", a, 1);
  LASSERT_TYPE("error", a, 0, LVAL_STR);
  
  lval* err = lval_err(a->cell[0]->str);
  
  lval_del(a);
  return err;
}

/* Loads in a file */
lval* builtin_load(lenv* e, lval* a) {
  /* Check if it passes in a single string arguement */
  LASSERT_NUM("load", a, 1);
  LASSERT_TYPE("load", a, 0, LVAL_STR);
  
  /* Parse file given by string name; gives us an abstract syntax tree */
  mpc_result_t r;
  if (mpc_parse_contents(a->cell[0]->str, Lispy, &r)) {
    /* Read contents; there are multiple expressions that can be evaluated separatedly */
    lval* expr = lval_read(r.output);
    mpc_ast_delete(r.output);
    
    /* Evaluate each expression */
    while (expr->count) {
      lval* x = lval_eval(e, lval_pop(expr, 0));
      if (x->type == LVAL_ERR) { lval_println(x); }
      lval_del(x);
    }
    
    lval_del(expr);
    lval_del(a);
    
    return lval_sexpr(); // Empty list
    
  } else {
    /* Get parse error as string */
    char* err_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);
    
    lval* err = lval_err("Could not load Library %s", err_msg);
    free(err_msg);
    lval_del(a);
    
    return err;
  }
}

/* Add builtin functions to environment */
void lenv_add_builtins(lenv* e) {
  /* String Functions */
  lenv_add_builtin(e, "load", builtin_load);
  lenv_add_builtin(e, "error", builtin_error);
  lenv_add_builtin(e, "print", builtin_print);
  
  /* Comparison Functions */
  lenv_add_builtin(e, "if", builtin_if);
  lenv_add_builtin(e, "==", builtin_eq);
  lenv_add_builtin(e, "!=", builtin_ne);
  lenv_add_builtin(e, ">",  builtin_gt);
  lenv_add_builtin(e, "<",  builtin_lt);
  lenv_add_builtin(e, ">=", builtin_ge);
  lenv_add_builtin(e, "<=", builtin_le);

  /* Variable Functions */
  lenv_add_builtin(e, "def", builtin_def);
  lenv_add_builtin(e, "=", builtin_put);
  lenv_add_builtin(e, "\\", builtin_lambda);
  
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
  lenv_add_builtin(e, "%", builtin_mod);
}

int main(int argc, char** argv) {
  /* Create some parsers */
  Number = mpc_new("number");
  Symbol = mpc_new("symbol");
  Sexpr = mpc_new("sexpr");
  Qexpr = mpc_new("qexpr");
  Expr = mpc_new("expr");
  Lispy = mpc_new("lispy");
  String = mpc_new("string");
  Comment = mpc_new("comment");
  
  /* Define with following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
    "							\
      number	: /-?[0-9]+(\\.[0-9]*)?/ ;		\
      symbol	: /[a-zA-Z0-9_+\\-%*\\/\\\\=<>!&]+/ ;	\
      string	: /\"(\\\\.|[^\"])*\"/ ;		\
      comment	: /;[^\\r\\n]*/ ;			\
      sexpr	: '(' <expr>* ')' ;			\
      qexpr	: '{' <expr>* '}' ;			\
      expr  	: <number> | <symbol> | <string> 	\
      		| <comment> | <sexpr> | <qexpr> ;	\
      lispy	: /^/ <expr>* /$/ ;			\
    ",
    Number, Symbol, String, Comment, Sexpr, Qexpr, Expr, Lispy);
  
  /* Create new environment and add builtin functions */
  lenv* e = lenv_new();
  lenv_add_builtins(e);
  
  if (argc == 1) {
  
    /* Print Version and Exit Information */
    puts("Lispy Version 0.0.0.0.5");
    puts("Press Ctrl+c to Exit\n");
      
    /* Load stdlib file */
    puts("Loading in stdlib...");
    lval* stdlib = lval_add(lval_sexpr(), lval_str("stdlib.lspy"));
    lval* s = builtin_load(e, stdlib);
    if (s->type == LVAL_ERR) { lval_println(s); }
    lval_del(s);
    puts("stdlib loaded in\n");
  
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
  }
  
  /* If supplied with list of files */
  if (argc >= 2) {
    /* loop over each supplied filename */
    for (int i = 1; i < argc; i++) {
      /* Argument list with single argument (filename) */
      lval* args = lval_add(lval_sexpr(), lval_str(argv[i]));
      
      /* Pass to builtin load to get result */
      lval* x = builtin_load(e, args);
      /* If result is error, print */
      if (x->type == LVAL_ERR) { lval_println(x); }
      lval_del(x);
    }
  }
    
  /* Delete environment */
  lenv_del(e);
  
  /* Undefine and Delete the Parser */
  mpc_cleanup(8, Number, Symbol, String, Comment, Sexpr, Qexpr, Expr, Lispy);
  
  return 0;
} 

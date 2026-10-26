#include "m4_calculator.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { const char *text; unsigned pos, depth; bool degrees, error; double answer; } calc_t;
static double expression(calc_t *p);
static void spaces(calc_t *p) { while (p->text[p->pos] == ' ') ++p->pos; }
static double primary(calc_t *p) {
    spaces(p);
    if (++p->depth > 24) { p->error = true; --p->depth; return 0; }
    double value = 0;
    char c = p->text[p->pos];
    if (c == '(') {
        ++p->pos; value = expression(p); spaces(p);
        if (p->text[p->pos] == ')') ++p->pos; else p->error = true;
    } else if (isdigit((unsigned char)c) || c == '.') {
        char *end;
        value = strtod(p->text + p->pos, &end);
        if (end == p->text + p->pos) p->error = true;
        p->pos = (unsigned)(end - p->text);
    } else {
        char name[12] = {0}; unsigned n = 0;
        while (isalpha((unsigned char)p->text[p->pos]) && n + 1 < sizeof(name)) name[n++] = p->text[p->pos++];
        if (!strcmp(name, "pi")) value = 3.14159265358979323846;
        else if (!strcmp(name, "e")) value = 2.71828182845904523536;
        else if (!strcmp(name, "Ans")) value = p->answer;
        else {
            spaces(p);
            if (!n || p->text[p->pos] != '(') { p->error = true; --p->depth; return 0; }
            ++p->pos; double x = expression(p); spaces(p);
            if (p->text[p->pos] == ')') ++p->pos; else p->error = true;
            double angle = p->degrees ? x * 3.14159265358979323846 / 180.0 : x;
            if (!strcmp(name, "sin")) value = sin(angle);
            else if (!strcmp(name, "cos")) value = cos(angle);
            else if (!strcmp(name, "tan")) { if (fabs(cos(angle)) < 1e-14) p->error = true; else value = tan(angle); }
            else if (!strcmp(name, "asin")) value = asin(x);
            else if (!strcmp(name, "acos")) value = acos(x);
            else if (!strcmp(name, "atan")) value = atan(x);
            else if (!strcmp(name, "sqrt")) value = sqrt(x);
            else if (!strcmp(name, "ln")) value = log(x);
            else if (!strcmp(name, "log")) value = log10(x);
            else if (!strcmp(name, "abs")) value = fabs(x);
            else if (!strcmp(name, "exp")) value = exp(x);
            else p->error = true;
            if (p->degrees && (!strcmp(name, "asin") || !strcmp(name, "acos") || !strcmp(name, "atan"))) value *= 180.0 / 3.14159265358979323846;
        }
    }
    spaces(p);
    while (p->text[p->pos] == '!' || p->text[p->pos] == '%') {
        if (p->text[p->pos++] == '%') value /= 100.0;
        else if (value < 0 || value > 170 || floor(value) != value) p->error = true;
        else { double product = 1; for (unsigned i = 2; i <= (unsigned)value; ++i) product *= i; value = product; }
    }
    --p->depth;
    if (!isfinite(value)) p->error = true;
    return value;
}
static double unary(calc_t *p);
static double power(calc_t *p) {
    double value = primary(p); spaces(p);
    if (p->text[p->pos] == '^') {
        ++p->pos;
        if (++p->depth > 24) { p->error = true; --p->depth; return 0; }
        value = pow(value, unary(p)); --p->depth;
    }
    if (!isfinite(value)) p->error = true;
    return value;
}
static double unary(calc_t *p) {
    spaces(p);
    if (p->text[p->pos] == '+' || p->text[p->pos] == '-') {
        bool negative = p->text[p->pos++] == '-';
        /* Input length is bounded, and nested unary operators share the depth budget. */
        if (++p->depth > 24) { p->error = true; --p->depth; return 0; }
        double value = unary(p); --p->depth; return negative ? -value : value;
    }
    return power(p);
}
static double term(calc_t *p) {
    double value = unary(p); spaces(p);
    while (p->text[p->pos] == '*' || p->text[p->pos] == '/') {
        char op = p->text[p->pos++]; double right = unary(p);
        if (op == '/' && right == 0) { p->error = true; return 0; }
        value = op == '*' ? value * right : value / right; spaces(p);
    }
    return value;
}
static double expression(calc_t *p) {
    double value = term(p); spaces(p);
    while (p->text[p->pos] == '+' || p->text[p->pos] == '-') {
        char op = p->text[p->pos++]; double right = term(p);
        value = op == '+' ? value + right : value - right; spaces(p);
    }
    return value;
}
bool m4_calculator_eval(const char *input, bool degrees, double answer, double *result) {
    if (!input || !result || !input[0] || strlen(input) > 192) return false;
    calc_t parser = {.text = input, .degrees = degrees, .answer = answer};
    double value = expression(&parser); spaces(&parser);
    if (parser.error || parser.text[parser.pos] || !isfinite(value)) return false;
    *result = value; return true;
}

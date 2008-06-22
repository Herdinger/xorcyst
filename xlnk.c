/*
 * $Id: xlnk.c,v 1.20 2007/11/11 22:35:22 khansen Exp $
 * $Log: xlnk.c,v $
 * Revision 1.20  2007/11/11 22:35:22  khansen
 * compile on mac
 *
 * Revision 1.19  2007/08/12 19:01:11  khansen
 * xorcyst 1.5.0
 *
 * Revision 1.18  2007/08/07 22:43:01  khansen
 * version
 *
 * Revision 1.17  2007/07/22 13:33:26  khansen
 * convert tabs to whitespaces
 *
 * Revision 1.16  2005/01/09 11:19:41  kenth
 * xorcyst 1.4.5
 * prints code/data addresses of public symbols when --verbose
 *
 * Revision 1.15  2005/01/05 09:33:37  kenth
 * xorcyst 1.4.4
 * fixed RAM allocator bug
 * print RAM statistics when --verbose
 *
 * Revision 1.14  2005/01/05 01:52:19  kenth
 * xorcyst 1.4.3
 *
 * Revision 1.13  2005/01/04 21:35:58  kenth
 * return error code from main() when error count > 0
 *
 * Revision 1.12  2004/12/29 21:43:55  kenth
 * xorcyst 1.4.2
 *
 * Revision 1.11  2004/12/27 06:42:05  kenth
 * fixed bug in alloc_ram()
 *
 * Revision 1.10  2004/12/25 02:23:28  kenth
 * xorcyst 1.4.1
 *
 * Revision 1.9  2004/12/19 19:58:54  kenth
 * xorcyst 1.4.0
 *
 * Revision 1.8  2004/12/18 19:10:39  kenth
 * relocation improved (I hope)
 *
 * Revision 1.7  2004/12/18 17:02:00  kenth
 * CMD_LINE*, CMD_FILE handling, error location printing
 * CMD_DSW, CMD_DSD gone
 *
 * Revision 1.6  2004/12/16 13:20:41  kenth
 * xorcyst 1.3.5
 *
 * Revision 1.5  2004/12/14 01:50:21  kenth
 * xorcyst 1.3.0
 *
 * Revision 1.4  2004/12/11 02:06:18  kenth
 * xorcyst 1.2.0
 *
 * Revision 1.3  2004/12/06 04:53:18  kenth
 * xorcyst 1.1.0
 *
 * Revision 1.2  2004/06/30 23:38:22  kenth
 * replaced argp with something else
 *
 * Revision 1.1  2004/06/30 07:56:04  kenth
 * Initial revision
 *
 * Revision 1.1  2004/06/30 07:42:03  kenth
 * Initial revision
 *
 */

/**
 *    (C) 2004 Kent Hansen
 *
 *    The XORcyst is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    The XORcyst is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with The XORcyst; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * This is the 6502 linker program. It takes one or more object files generated
 * by the 6502 assembler program, and then (rough list)
 * - maps all data labels to physical 6502 RAM
 * - relocates code to 6502 address space
 * - resolves external references
 * - writes everything to a final binary
 *
 * The input to the linker is a script file which describes the layout and
 * contents of the final binary.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "getopt.h"
#include "objdef.h"
#include "opcode.h"
#include "script.h"
#include "unit.h"
#include "hashtab.h"

#define SAFE_FREE(m) if ((m) != NULL) { free(m); m = NULL; }

/**
 * Parses a string to an integer.
 * @param s String
 * @return Integer
 */
static int str_to_int(char *s)
{
    if (s[0] == '$') {
        return strtol(&s[1], NULL, 16);
    }
    else if (s[0] == '%') {
        return strtol(&s[1], NULL, 2);
    }
    return strtol(s, NULL, 0);
}

/*--------------------------------------------------------------------------*/
/* Argument parsing stuff. */

static char program_version[] = "xlnk 1.5.0";

struct tag_arguments {
    char *input_file;
    int silent;
    int verbose;
};

typedef struct tag_arguments arguments;

/* Argument variables set by arg parser. */
static arguments program_args;

/* Long options for getopt_long(). */
static struct option long_options[] = {
  { "quiet",    no_argument, 0, 'q' },
  { "silent",   no_argument, 0, 's' },
  { "verbose",  no_argument, 0, 'v' },
  { "help", no_argument, 0, 0 },
  { "usage",    no_argument, 0, 0 },
  { "version",  no_argument, 0, 'V' },
  { 0 }
};

/* Prints usage message and exits. */
static void usage()
{
    printf("\
Usage: xlnk [-qsvV] [--quiet] [--silent] [--verbose] [--help] [--usage]\n\
            [--version] FILE\n\
");
    exit(0);
}

/* Prints help message and exits. */
static void help()
{
    printf("\
Usage: xlnk [OPTION...] FILE\n\
The XORcyst Linker -- it creates quite a stir\n\
\n\
  -q, -s, --quiet, --silent  Don't produce any output\n\
  -v, --verbose              Produce verbose output\n\
      --help                 Give this help list\n\
      --usage                Give a short usage message\n\
  -V, --version              Print program version\n\
\n\
Report bugs to <dev@null>.\n\
");
    exit(0);
}

/* Prints version and exits. */
static void version()
{
    printf("%s\n", program_version);
    exit(0);
}

/* Parses program arguments. */
static void
parse_arguments (int argc, char **argv)
{
    int key;
    /* getopt_long stores the option index here. */
    int index = 0;

    /* Set default values. */
    program_args.silent = 0;
    program_args.verbose = 0;
    program_args.input_file = NULL;

    /* Parse options. */
    while ((key = getopt_long(argc, argv, "qsvV", long_options, &index)) != -1) {
        switch (key) {
            case 'q': case 's':
            program_args.silent = 1;
            break;

            case 'v':
            program_args.verbose = 1;
            break;

            case 0:
            /* Use index to differentiate between options */
            if (strcmp(long_options[index].name, "usage") == 0) {
                usage();
            }
            else if (strcmp(long_options[index].name, "help") == 0) {
                help();
            }
            break;

            case 'V':
            version();
            break;

            case '?':
            /* Error message has been printed by getopt_long */
            exit(1);
            break;

            default:
            /* Forgot to handle a short option, most likely */
            exit(1);
            break;
        }
    }

    /* Must be one additional argument, which is the input file. */
    if (argc-1 != optind) {
        printf("Usage: xlnk [OPTION...] FILE\nTry `xlnk --help' or `xlnk --usage' for more information.\n");
        exit(1);
    }
    else {
        program_args.input_file = argv[optind];
    }
}

/*--------------------------------------------------------------------------*/
/* Data structures. */

/* Describes a local label in the unit. */
struct tag_local
{
    char *name; /* NULL if not exported */
    int resolved;   /* 0 initially, set to 1 when phys_addr has been assigned */
    int virt_addr;
    int phys_addr;
    int ref_count;
    int size;
    struct tag_xunit *owner;
    unsigned short align;
    unsigned char flags;
};

typedef struct tag_local local;

/* Describes an array of local labels. */
struct tag_local_array
{
    local *entries;
    int size;
};

typedef struct tag_local_array local_array;

/**
 * eXtended unit, has extra info built from basic unit ++
 */
struct tag_xunit
{
    unit _unit_;    /* NB!!! "Superclass", must be first field for casting to work */
    local_array data_locals;
    local_array code_locals;
    int bank_id;
    int data_size;
    int code_origin;
    int code_size;
    int loaded;
};

typedef struct tag_xunit xunit;

/**
 * Describes a 6502 RAM block available for allocation.
 */
struct tag_avail_block
{
    int start;  /* Start address in 6502 space */
    int end;    /* End address in 6502 space */
    struct tag_avail_block *next;
};

typedef struct tag_avail_block avail_block;

/** */
struct tag_calc_address_args
{
    xunit *xu;
    int index;
};

typedef struct tag_calc_address_args calc_address_args;

/** */
struct tag_write_binary_args
{
    xunit *xu;
    FILE *fp;
};

typedef struct tag_write_binary_args write_binary_args;

/*--------------------------------------------------------------------------*/

/** Array containing the units to link. */
static xunit *units;
/* Number of units in above array. */
static int unit_count;

/** Holds the current memory address. */
static int pc;

/** Hash tables used to lookup symbols. */
static hashtab *label_hash;
static hashtab *constant_hash;
static hashtab *unit_hash;

/** Number of errors and warnings during linking */
static int err_count;
static int warn_count;

static int suppress;

/* Head of the list of available 6502 RAM blocks (for data allocation). */
static avail_block *first_block = NULL;

static int total_ram = 0;

/* Bank info */
static int bank_offset;
static int bank_size;
static int bank_origin;
static int bank_id;

/* Debug info */
static unsigned char *unit_file = NULL; /* length byte followed by chars */
static int unit_line = -1;

/*--------------------------------------------------------------------------*/

/**
 * If the object file contains FILE and LINE bytecodes (assembled with
 * --debug switch), unit_file and unit_line will contain the current
 * source location. In that case, this function prints the location.
 */
static void maybe_print_location()
{
    char *str;
    int len;
    if (unit_file != NULL) {
        /* Print source location */
        len = unit_file[0] + 1;
        str = (char *)malloc(len + 1);
        strncpy(str, (char *)&unit_file[1], len);
        str[len] = '\0';
        fprintf(stderr, "%s:%d: ", str, unit_line);
        free(str);
    }
}

/**
 * If the object doesn't contain FILE and LINE bytecodes,
 * unit_file will be <code>NULL</code>. In that case, this
 * function prints a tip about reassembling with --debug switch.
 */
static void maybe_print_debug_tip()
{
    if (unit_file == NULL) {
        fprintf(stderr, "\treassemble with --debug switch to obtain source location\n");
    }
}

/**
 * Issues an error.
 * @param fmt format string for printf
 */
static void err(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (!suppress) {
        /* Print error message */
        fprintf(stderr, "error: ");
        maybe_print_location();
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        maybe_print_debug_tip();
        /* Increase total error count */
        err_count++;
    }
    va_end(ap);
}

/**
 * Issues a warning.
 * @param fmt format string for printf
 */
static void warn(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (!suppress) {
        /* Print warning message */
        fprintf(stderr, "warning: ");
        maybe_print_location();
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        maybe_print_debug_tip();
        /* Increase total warning count */
        warn_count++;
    }
    va_end(ap);
}

/**
 * Prints a message if --verbose switch was given.
 * @param fmt format string for printf
 */
static void verbose(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (!suppress && program_args.verbose) {
        vfprintf(stdout, fmt, ap);
        fprintf(stdout, "\n");
    }
    va_end(ap);
}

/*--------------------------------------------------------------------------*/
/* Functions to manage 6502 RAM blocks. */
/* The RAM allocator maintains a list of these blocks that are used to
   map the contents of the units' data segments to memory.
*/

/**
 * Calculates number of bytes of 6502 RAM left for allocation.
 */
static int ram_left()
{
    int sum;
    avail_block *b;
    for (sum = 0, b = first_block; b != NULL; b = b->next) {
        sum += b->end - b->start;
    }
    return sum;
}

/**
 * Adds a block of 6502 memory to the list of available memory regions.
 * When adding multiple blocks they should be added in prioritized order.
 * @param start Start address of the block
 * @param end End address of the block (non-inclusive!)
 */
static void add_ram_block(int start, int end)
{
    avail_block *b;
    /* Allocate a block struct */
    avail_block *new_block = (avail_block *)malloc( sizeof(avail_block) );
    if (new_block != NULL) {
        /* Set the fields */
        new_block->start = start;
        new_block->end = end;
        new_block->next = NULL;
        /* Add it to list */
        if (first_block == NULL) {
            /* Start the list */
            first_block = new_block;
        }
        else {
            /* Add to end */
            for (b = first_block; b->next != NULL; b = b->next) ;
            b->next = new_block;
        }
        verbose("  added RAM block: %X-%X", new_block->start, new_block->end);
    }
}

/**
 * Allocates a chunk of 6502 RAM to a local.
 * @param l Local
 * @return 0 if there isn't enough RAM to satisfy the request (fail), 1 otherwise (success)
 */
static int alloc_ram(local *l)
{
    int left;
    int pad;
    /* Try the available blocks in order. */
    /* Use the first one that's sufficient. */
    avail_block *b;
    avail_block *n;
    avail_block *p = NULL;
    for (b = first_block; b != NULL; p = b, b = b->next) {
        /* Check if zero page block required */
        if (l->flags & LABEL_FLAG_ZEROPAGE) {
            if (b->start >= 0x100) {
                continue;   /* This block is no good */
            }
        }
        /* Calculate the # of bytes left in this block */
        left = b->end - b->start;
        /* See if it's enough */
        if (left < l->size) {
            continue;   /* Not enough, sorry */
        }
        /* Check if alignment required */
        if (l->flags & LABEL_FLAG_ALIGN) {
            pad = b->start & ((1 << l->align) - 1);
            if (pad != 0) {
                /* This block doesn't match the alignment */
                /* Break it into two blocks if possible */
                pad = (1 << l->align) - pad;
                pad = (left < pad) ? left : pad;
                if (pad < left) {
                    n = (avail_block *)malloc(sizeof(avail_block));
                    n->start = b->start;
                    n->end = n->start + pad;
                    b->start += pad;
                    n->next = b;
                    if (b == first_block) {
                        first_block = n;    /* New head */
                    }
                    b = n;
                }
                continue;
            }
        }
        /* Pick this one. */
        l->phys_addr = b->start;
        /* Decrease block size by moving start address ahead */
        b->start += l->size;
        /* If there's no more space left in this block, discard it */
        if (left == l->size) {
            /* Remove from linked list */
            if (p == NULL) {
                /* Set successor block as new head */
                first_block = b->next;
            }
            else {
                /* Squeeze out */
                p->next = b->next;
            }
            /* Free associated memory */
            SAFE_FREE(b);
        }
        /* Return with success */
        return 1;
    }
    /* Couldn't find a block large enough, return with failure */
    return 0;
}

/**
 * Frees up memory associated with list of RAM blocks.
 */
static void finalize_ram_blocks()
{
    avail_block *b;
    avail_block *t;
    for (b = first_block; b != NULL; b = t) {
        t = b->next;
        SAFE_FREE(b);
    }
}

/*--------------------------------------------------------------------------*/
/* Functions to get big-endian values from byte buffer. */

/* Gets single byte from buffer and increments index. */
static unsigned char get_1(unsigned char *b, int *i)
{
    return b[(*i)++];
}
/* Gets big-endian short from buffer and increments index. */
static unsigned short get_2(unsigned char *b, int *i)
{
    unsigned short result = get_1(b, i) << 8;
    result |= get_1(b, i);
    return result;
}
/* Gets big-endian 24-bit integer from buffer and increments index. */
static unsigned int get_3(unsigned char *b, int *i)
{
    unsigned int result = get_2(b, i) << 8;
    result |= get_1(b, i);
    return result;
}
/* Gets big-endian int from buffer and increments index. */
/*static unsigned int get_4(unsigned char *b, int *i)
{
    unsigned int result = get_2(b, i) << 16;
    result |= get_2(b, i);
    return result;
}
*/
/*--------------------------------------------------------------------------*/

/**
 * Calculates the storage occupied by a CMD_LABEL bytecode's arguments.
 */
static int label_cmd_args_size(unsigned char *bytes)
{
    int size = 1;   /* Smallest possible: flag byte */
    int flags = bytes[0];
    if (flags & LABEL_FLAG_EXPORT) { size += bytes[1] + 1 + 1; }    /* Length byte + string */
    if (flags & LABEL_FLAG_ALIGN) { size += 1; }            /* Alignment */
    if (flags & LABEL_FLAG_ADDR) { size += 2; }         /* Address */
    return size;
}

/** Signature for procedure to process a bytecode */
typedef void (*bytecodeproc)(unsigned char *, void *);

/**
 * Walks an array of bytecodes, calling corresponding bytecode handlers
 * along the way.
 * @param bytes Array of bytecodes, terminated by CMD_END
 * @param handlers Array of bytecode handlers (entries can be NULL)
 * @param arg Argument passed to bytecode handler, can be anything
 */
static void bytecode_walk(unsigned char *bytes, bytecodeproc *handlers, void *arg)
{
    int i;
    unsigned char cmd;
    unit_file = NULL;
    unit_line = -1;
    if (bytes == NULL) { return; }
    i = 0;
    do {
        /* Get a command */
        cmd = get_1(bytes, &i);

        /* Check if debug command */
        if (cmd < CMD_END) {
            switch (cmd) {
                case CMD_FILE:
                unit_file = &bytes[i];
                i += get_1(bytes, &i) + 1;  /* Skip count and array of bytes */
                break;
                case CMD_LINE8:  unit_line = get_1(bytes, &i);  break;
                case CMD_LINE16: unit_line = get_2(bytes, &i);  break;
                case CMD_LINE24: unit_line = get_3(bytes, &i);  break;
                case CMD_LINE_INC: unit_line++; break;
            }
            continue;
        }

        /* Call bytecode handler if one is present */
        if (handlers[cmd-CMD_END] != NULL) {
            handlers[cmd-CMD_END](&bytes[i-1], arg);
        }
        /* Skip any bytecode arguments */
        switch (cmd) {
            case CMD_END:   break;
            case CMD_BIN8:  i += get_1(bytes, &i) + 1;  break;  /* Skip count and array of bytes */
            case CMD_BIN16: i += get_2(bytes, &i) + 1;  break;  /* Skip count and array of bytes */
            case CMD_LABEL: i += label_cmd_args_size(&bytes[i]); break; /* Skip flag byte and possibly name and alignment */
            case CMD_INSTR: i += 3; break;  /* Skip 6502 opcode and 16-bit expr id */
            case CMD_DB:    i += 2; break;  /* Skip 16-bit expr id */
            case CMD_DW:    i += 2; break;  /* Skip 16-bit expr id */
            case CMD_DD:    i += 2; break;  /* Skip 16-bit expr id */
            case CMD_DSI8:  i += 1; break;  /* Skip 8-bit count */
            case CMD_DSI16: i += 2; break;  /* Skip 16-bit count */
            case CMD_DSB:   i += 2; break;  /* Skip 16-bit expr id */

            default:
            /* Invalid opcode */
            err("invalid bytecode");
            break;
        }
    } while (cmd != CMD_END);
}

/*--------------------------------------------------------------------------*/
/* Functions for expression evaluation. */

/**
 * Finalizes a constant.
 * @param c Constant to finalize
 */
static void finalize_constant(constant *c)
{
    if (c->type == STRING_CONSTANT) {
        SAFE_FREE(c->string);
    }
}

/**
 * Gets string representation of an operator (OP_*, see objdef.h).
 * @param op Operator
 * @return String representation of operator
 */
static const char *operator_to_string(int op)
{
    switch (op) {
        case OP_PLUS:   return "+";
        case OP_MINUS:  return "-";
        case OP_MUL:    return "*";
        case OP_DIV:    return "/";
        case OP_MOD:    return "%";
        case OP_SHL:    return "<<";
        case OP_SHR:    return ">>";
        case OP_AND:    return "&";
        case OP_OR: return "|";
        case OP_XOR:    return "^";
        case OP_EQ: return "==";
        case OP_NE: return "!=";
        case OP_LT: return "<";
        case OP_GT: return ">";
        case OP_LE: return "<=";
        case OP_GE: return ">=";
        case OP_NOT:    return "!";
        case OP_NEG:    return "~";
        case OP_LO: return "<";
        case OP_HI: return ">";
        case OP_UMINUS: return "-";
        case OP_BANK:   return "^";
    }
    return "";
}

/**
 * Evaluates an expression recursively.
 * The result will either be a integer or string literal, indicating successful
 * evaluation; or an invalid type indicating that a symbol could not be translated
 * to a constant (in other words, it could not be resolved). In this case,
 * result->string contains the name of the symbol which couldn't be evaluated.
 * @param u The unit where the expression is contained
 * @param e The expression to evaluate
 * @param result Pointer to resulting value
 */
static void eval_recursive(xunit *u, expression *e, constant *result)
{
    char *s;
    local *l;
    constant *c;
    constant lhs_result, rhs_result;
    switch (e->type) {
        case OPERATOR_EXPRESSION:
        switch (e->op_expr.operator) {
            /* Binary operators */
            case OP_PLUS:
            case OP_MINUS:
            case OP_MUL:
            case OP_DIV:
            case OP_MOD:
            case OP_SHL:
            case OP_SHR:
            case OP_AND:
            case OP_OR:
            case OP_XOR:
            case OP_EQ:
            case OP_NE:
            case OP_LT:
            case OP_GT:
            case OP_LE:
            case OP_GE:
            /* Evaluate both sides */
            eval_recursive(u, e->op_expr.lhs, &lhs_result);
            eval_recursive(u, e->op_expr.rhs, &rhs_result);
            /* If either side is unresolved, then result is unresolved. */
            if ((lhs_result.type == -1) || (rhs_result.type == -1)) {
                result->type = -1;
            }
            /* If both sides are integer, then result is integer. */
            else if ((lhs_result.type == INTEGER_CONSTANT) &&
            (rhs_result.type == INTEGER_CONSTANT)) {
                result->type = INTEGER_CONSTANT;
                /* Perform the proper operation to obtain result. */
                switch (e->op_expr.operator) {
                    case OP_PLUS:   result->integer = lhs_result.integer + rhs_result.integer;  break;
                    case OP_MINUS:  result->integer = lhs_result.integer - rhs_result.integer;  break;
                    case OP_MUL:    result->integer = lhs_result.integer * rhs_result.integer;  break;
                    case OP_DIV:    result->integer = lhs_result.integer / rhs_result.integer;  break;
                    case OP_MOD:    result->integer = lhs_result.integer % rhs_result.integer;  break;
                    case OP_SHL:    result->integer = lhs_result.integer << rhs_result.integer; break;
                    case OP_SHR:    result->integer = lhs_result.integer >> rhs_result.integer; break;
                    case OP_AND:    result->integer = lhs_result.integer & rhs_result.integer;  break;
                    case OP_OR: result->integer = lhs_result.integer | rhs_result.integer;  break;
                    case OP_XOR:    result->integer = lhs_result.integer ^ rhs_result.integer;  break;
                    case OP_EQ: result->integer = lhs_result.integer == rhs_result.integer; break;
                    case OP_NE: result->integer = lhs_result.integer != rhs_result.integer; break;
                    case OP_LT: result->integer = lhs_result.integer < rhs_result.integer;  break;
                    case OP_GT: result->integer = lhs_result.integer > rhs_result.integer;  break;
                    case OP_LE: result->integer = lhs_result.integer <= rhs_result.integer; break;
                    case OP_GE: result->integer = lhs_result.integer >= rhs_result.integer; break;
                }
            }
            /* If both sides are string... */
            else if ((lhs_result.type == STRING_CONSTANT) &&
            (rhs_result.type == STRING_CONSTANT)) {
                switch (e->op_expr.operator) {
                    case OP_PLUS:
                    /* Concatenate */
                    result->string = (char *)malloc(strlen(lhs_result.string)+strlen(rhs_result.string)+1);
                    if (result->string != NULL) {
                        strcpy(result->string, lhs_result.string);
                        strcat(result->string, rhs_result.string);
                        result->type = STRING_CONSTANT;
                    }
                    break;

                    /* String comparison: using strcmp() */
                    case OP_EQ: result->integer = strcmp(lhs_result.string, rhs_result.string) == 0;    break;
                    case OP_NE: result->integer = strcmp(lhs_result.string, rhs_result.string) != 0;    break;
                    case OP_LT: result->integer = strcmp(lhs_result.string, rhs_result.string) < 0; break;
                    case OP_GT: result->integer = strcmp(lhs_result.string, rhs_result.string) > 0; break;
                    case OP_LE: result->integer = strcmp(lhs_result.string, rhs_result.string) <= 0;    break;
                    case OP_GE: result->integer = strcmp(lhs_result.string, rhs_result.string) >= 0;    break;

                    default:
                    /* Not defined operator for string operation... */
                    break;
                }
            }
            else {
                /* Error, operands are incompatible */
                result->type = -1;
                err("incompatible operands to `%s' in expression", operator_to_string(e->op_expr.operator) );
            }
            /* Discard the operands */
            finalize_constant(&lhs_result);
            finalize_constant(&rhs_result);
            break;  /* Binary operator */

            /* Unary operators */
            case OP_NOT:
            case OP_NEG:
            case OP_LO:
            case OP_HI:
            case OP_UMINUS:
            /* Evaluate the single operand */
            eval_recursive(u, e->op_expr.lhs, &lhs_result);
            /* If operand is unresolved then result is unresolved. */
            if (lhs_result.type == -1) {
                result->type = -1;
            }
            /* If operand is integer then result is integer. */
            else if (lhs_result.type == INTEGER_CONSTANT) {
                result->type = INTEGER_CONSTANT;
                /* Perform the proper operation to obtain result. */
                switch (e->op_expr.operator) {
                    case OP_NOT:    result->integer = !lhs_result.integer;      break;
                    case OP_NEG:    result->integer = ~lhs_result.integer;      break;
                    case OP_LO: result->integer = lhs_result.integer & 0xFF;    break;
                    case OP_HI: result->integer = (lhs_result.integer >> 8) & 0xFF; break;
                    case OP_UMINUS: result->integer = -lhs_result.integer;      break;
                }
            }
            else {
                /* Error, invalid operand */
                err("incompatible operand to `%s' in expression", operator_to_string(e->op_expr.operator) );
                result->type = -1;
            }
            /* Discard the operand */
            finalize_constant(&lhs_result);
            break;  /* Unary operator */

            case OP_BANK:
            switch (e->op_expr.lhs->type) {
                case LOCAL_EXPRESSION:
                /* Simple, it must be in the same (current) bank */
                result->integer = bank_id;
                result->type = INTEGER_CONSTANT;
                break;

                case EXTERNAL_EXPRESSION:
                /* Get the name of the external */
                s = u->_unit_.externals[e->op_expr.lhs->extrn_id].name;
                /* Look it up */
                if ((l = (local *)hashtab_get(label_hash, s)) != NULL) {
                    /* It's a label */
                    result->integer = l->owner->bank_id;
                    result->type = INTEGER_CONSTANT;
                }
                else if ((c = (constant *)hashtab_get(constant_hash, s)) != NULL) {
                    /* It's a constant */
                    result->integer = ((xunit *)c->unit)->bank_id;
                    result->type = INTEGER_CONSTANT;
                }
                else {
                    result->type = -1;
                }
                break;

                default:
                result->type = -1;
                break;
            }
            break;
        }
        break;

        case INTEGER_EXPRESSION:
        /* Copy value to result */
        result->type = INTEGER_CONSTANT;
        result->integer = e->integer;
        break;

        case STRING_EXPRESSION:
        /* Copy value to result */
        result->string = (char *)malloc(strlen(e->string) + 1);
        if (result->string != NULL) {
            strcpy(result->string, e->string);
            result->type = STRING_CONSTANT;
        }
        break;

        case LOCAL_EXPRESSION:
        if (e->local_id >= u->data_locals.size) {
            /* It's a code local */
            l = &u->code_locals.entries[e->local_id - u->data_locals.size];
        }
        else {
            /* It's a data local */
            l = &u->data_locals.entries[e->local_id];
        }
        /* Test if it's resolved */
        if (l->resolved) {
            /* Copy address to result */
            result->type = INTEGER_CONSTANT;
            result->integer = l->phys_addr;
        }
        else {
            /* Not resolved (yet, at least) */
            result->type = -1;
        }
        break;

        case EXTERNAL_EXPRESSION:
        /* Get the name of the external */
        s = u->_unit_.externals[e->extrn_id].name;
        /* Look it up */
        if ((l = (local *)hashtab_get(label_hash, s)) != NULL) {
            /* It's a label */
            /* Test if it's resolved */
            if (l->resolved) {
                /* Copy address to result */
                result->type = INTEGER_CONSTANT;
                result->integer = l->phys_addr;
            }
            else {
                /* Not resolved (yet) */
                result->type = -1;
            }
        }
        else if ((c = (constant *)hashtab_get(constant_hash, s)) != NULL) {
            /* It's a constant */
            /* Copy value to result */
            switch (c->type) {
                case INTEGER_CONSTANT:
                result->type = INTEGER_CONSTANT;
                result->integer = c->integer;
                break;

                case STRING_CONSTANT:
                result->string = (char *)malloc(strlen(c->string) + 1);
                if (result->string != NULL) {
                    strcpy(result->string, c->string);
                    result->type = STRING_CONSTANT;
                }
                break;
            }
        }
        else {
            /* Error */
            result->type = -1;
            err("unknown symbol `%s' referenced from %s", s, u->_unit_.name);
        }
        break;

        case PC_EXPRESSION:
        /* Copy current PC to result */
        result->type = INTEGER_CONSTANT;
        result->integer = pc;
        break;
    }
}

/**
 * Evaluates an expression.
 * @param u The unit where the expression is contained
 * @param exid The unique ID of the expression
 * @param result Where to store the result of the evaluation
 */
static void eval_expression(xunit *u, int exid, constant *result)
{
    /* Get the expression with id exid */
    expression *exp = u->_unit_.expressions[exid];
    /* Evaluate recursively */
    eval_recursive(u, exp, result);
}

/*--------------------------------------------------------------------------*/
/* Functions for incrementing PC, with error handling for wraparound. */

/**
 * Increases PC by amount.
 * Issues error if the PC wraps around.
 */
static void inc_pc(int amount, void *arg)
{
    calc_address_args *aargs;
    /* Check for 16-bit overflow */
    if ((pc <= 0x10000) && ((pc+amount) > 0x10000)) {
        aargs = (calc_address_args *)arg;
        err("PC went beyond 64K when linking `%s'", aargs->xu->_unit_.name);
    }
    /* Add! */
    pc += amount;
}

/**
 * Increases PC by 8-bit value immediately following bytecode command.
 */
static void inc_pc_count8(unsigned char *b, void *arg)
{
    int i = 1;
    inc_pc( get_1(b, &i) + 1, arg );
}

/**
 * Increases PC by 16-bit value immediately following bytecode command.
 */
static void inc_pc_count16(unsigned char *b, void *arg)
{
    int i = 1;
    inc_pc( get_2(b, &i) + 1, arg );
}

/**
 * Increases PC by 1.
 */
static void inc_pc_1(unsigned char *b, void *arg)
{
    inc_pc( 1, arg );
}

/**
 * Increases PC by 2.
 */
static void inc_pc_2(unsigned char *b, void *arg)
{
    inc_pc( 2, arg );
}

/**
 * Increases PC by 4.
 */
static void inc_pc_4(unsigned char *b, void *arg)
{
    inc_pc( 4, arg );
}

/**
 * Increases PC according to size of define data command.
 */
static void inc_pc_dsb(unsigned char *b, void *arg)
{
    constant c;
    int exid;
    calc_address_args *args = (calc_address_args *)arg;
    int i = 1;
    /* Get expression ID */
    exid = get_2(b, &i);
    /* Evaluate expression */
    eval_expression(args->xu, exid, &c);
    /* Handle the result */
    if (c.type == INTEGER_CONSTANT) {
        /* An array of bytes will be located here */
        /* Advance PC appropriately */
        inc_pc( c.integer, arg );
    }
    else if (c.type == STRING_CONSTANT) {
        /* Error, doesn't make sense here */
        err("unexpected string operand (`%s') to storage directive", c.string);
    }
    else {
        /* Error, unresolved */
        //err("unresolved symbol");
    }

    finalize_constant(&c);
}

/**
 * Increments PC according to the length of this instruction.
 */
static void inc_pc_instr(unsigned char *b, void *arg)
{
    constant c;
    unsigned char op, t;
    int exid;
    calc_address_args *args = (calc_address_args *)arg;
    /* Get opcode */
    int i = 1;
    op = get_1(b, &i);
    /* Get expression ID */
    exid = get_2(b, &i);
    /* Evaluate it */
    eval_expression(args->xu, exid, &c);
    /* Handle the result */
    if (c.type == INTEGER_CONSTANT) {
        /* See if it can be reduced to ZP instruction */
        if ((c.integer < 0x100) &&
        ((t = opcode_zp_equiv(op)) != 0xFF)) {
            /* replace op by ZP-version */
            op = t;
            b[1] = t;
        }
    }
    else if (c.type == STRING_CONSTANT) {
        /* Error, string operand doesn't make sense here */
        err("invalid instruction operand (string)");
    }
    else {
        /* Address not available yet (forward reference). */
        //err("unresolved symbol");
    }
    /* Advance PC */
    inc_pc( opcode_length(op), arg );
}

/*--------------------------------------------------------------------------*/
/* Functions for writing pure 6502 binary from bytecodes. */

/**
 * Writes an array of bytes.
 */
static void write_bin8(unsigned char *b, void *arg)
{
    int count;
    int i;
    write_binary_args *args = (write_binary_args *)arg;
    /* Get 8-bit count */
    i = 1;
    count = get_1(b, &i) + 1;
    /* Write data */
    fwrite(&b[i], 1, count, args->fp);
    /* Advance PC */
    inc_pc( count, arg );
}

/**
 * Writes an array of bytes.
 */
static void write_bin16(unsigned char *b, void *arg)
{
    int count;
    int i;
    write_binary_args *args = (write_binary_args *)arg;
    /* Get 16-bit count */
    i = 1;
    count = get_2(b, &i) + 1;
    /* Write data */
    fwrite(&b[i], 1, count, args->fp);
    /* Advance PC */
    inc_pc( count, arg );
}

/**
 * Writes an instruction.
 */
static void write_instr(unsigned char *b, void *arg)
{
    constant c;
    unsigned char op;
    int i;
    int exid;
    write_binary_args *args = (write_binary_args *)arg;
    /* Get opcode */
    i = 1;
    op = get_1(b, &i);
    /* Get expression ID */
    exid = get_2(b, &i);
    /* Evaluate expression */
    eval_expression(args->xu, exid, &c);

    /* Write the opcode */
    fputc(op, args->fp);

    if (opcode_length(op) == 2) {
        /* Operand must fit in 1 byte */
        /* Check if it's a relative jump */
        switch (op) {
            case 0x10:
            case 0x30:
            case 0x50:
            case 0x70:
            case 0x90:
            case 0xB0:
            case 0xD0:
            case 0xF0:
            /* Calculate difference between target and address of next instruction */
            c.integer = c.integer - (pc + 2);
            /* Make sure jump is in range */
            if ( (c.integer < -128) || (c.integer > 127) ) {
                err("branch out of range");
            }
            /* Make it a byte value */
            c.integer &= 0xFF;
            break;
        }
        if (c.integer >= 0x100) {
            err("instruction operand doesn't fit in 1 byte");
        }
        else {
            /* Write it */
            fputc(c.integer, args->fp);
        }
    }
    else {
        /* Operand must fit in 2 bytes */
        if (c.integer >= 0x10000) {
            err("instruction operand doesn't fit in 2 bytes");
        }
        else {
            /* Write it, low byte first */
            fputc(c.integer, args->fp);
            fputc(c.integer >> 8, args->fp);
        }
    }
    /* Advance PC */
    inc_pc( opcode_length(op), arg );
}

/**
 * Writes a byte, word or dword.
 */
static void write_dx(unsigned char *b, void *arg)
{
    constant c;
    int i;
    int exid;
    write_binary_args *args = (write_binary_args *)arg;
    /* Get expression ID */
    i = 1;
    exid = get_2(b, &i);
    /* Evaluate expression */
    eval_expression(args->xu, exid, &c);

    if (c.type == INTEGER_CONSTANT) {
        /* Write low byte */
        fputc(c.integer, args->fp);
        /* If 2+ bytes, write high ones */
        switch (b[0]) {
            case CMD_DB:
            if (c.integer > 0xFF) {
                warn("`.DB' operand $%X out of range; truncated", c.integer);
            }
            break;

            case CMD_DW:
            fputc(c.integer >> 8, args->fp);
            if (c.integer > 0xFFFF) {
                warn("`.DW' operand $%X out of range; truncated", c.integer);
            }
            break;

            case CMD_DD:
            fputc(c.integer >> 8, args->fp);
            fputc(c.integer >> 16, args->fp);
            fputc(c.integer >> 24, args->fp);
            break;
        }
        /* Advance PC */
        switch (b[0]) {
            case CMD_DB:    inc_pc( 1, arg );   break;
            case CMD_DW:    inc_pc( 2, arg );   break;
            case CMD_DD:    inc_pc( 4, arg );   break;
        }
    }
    else if (c.type == STRING_CONSTANT) {
        /* Write sequence of characters */
        for (i=0; i<strlen(c.string); i++) {
            /* Write low byte */
            fputc(c.string[i], args->fp);
            /* If 2+ bytes, write high ones */
            switch (b[0]) {
                case CMD_DW:
                fputc(0, args->fp);
                break;

                case CMD_DD:
                fputc(0, args->fp);
                fputc(0, args->fp);
                fputc(0, args->fp);
                break;
            }
            /* Advance PC */
            switch (b[0]) {
                case CMD_DB:    inc_pc( 1, arg );   break;
                case CMD_DW:    inc_pc( 2, arg );   break;
                case CMD_DD:    inc_pc( 4, arg );   break;
            }
        }
    }

    finalize_constant(&c);
}

/**
 * Writes a series of zeroes.
 */
static void write_dsi8(unsigned char *b, void *arg)
{
    int count;
    int i;
    write_binary_args *args = (write_binary_args *)arg;
    /* Get 8-bit count */
    i = 1;
    count = get_1(b, &i) + 1;
    /* Write zeroes */
    for (i=0; i<count; i++) {
        fputc(0, args->fp);
    }
    /* Advance PC */
    inc_pc( count, arg );
}

/**
 * Writes a series of zeroes.
 */
static void write_dsi16(unsigned char *b, void *arg)
{
    int count;
    int i;
    write_binary_args *args = (write_binary_args *)arg;
    /* Get 16-bit count */
    i = 1;
    count = get_2(b, &i) + 1;
    /* Write zeroes */
    for (i=0; i<count; i++) {
        fputc(0, args->fp);
    }
    /* Advance PC */
    inc_pc( count, arg );
}

/**
 * Writes a series of zeroes.
 */
static void write_dsb(unsigned char *b, void *arg)
{
    constant c;
    int i;
    int exid;
    write_binary_args *args = (write_binary_args *)arg;
    /* Get expression ID */
    i = 1;
    exid = get_2(b, &i);
    /* Evaluate expression */
    eval_expression(args->xu, exid, &c);
    if (c.integer < 0) {
        err("negative count");
    }
    else if (c.integer > 0) {
        /* Write zeroes */
        for (i=0; i<c.integer; i++) {
            fputc(0, args->fp);
        }
        /* Advance PC */
        inc_pc( c.integer, arg );
    }
}

/**
 * Writes a code segment as fully native 6502 code.
 * @param fp File handle
 * @param u Unit whose code to write
 */
static void write_as_binary(FILE *fp, xunit *u)
{
    write_binary_args args;
    /* Table of callback functions for our purpose. */
    bytecodeproc handlers[] =
    {
        NULL,       /* CMD_END */
        write_bin8, /* CMD_BIN8 */
        write_bin16,    /* CMD_BIN16 */
        NULL,       /* CMD_LABEL */
        write_instr,    /* CMD_INSTR */
        write_dx,   /* CMD_DB */
        write_dx,   /* CMD_DW */
        write_dx,   /* CMD_DD */
        write_dsi8, /* CMD_DSI8 */
        write_dsi16,    /* CMD_DSI16 */
        write_dsb   /* CMD_DSB */
    };
    /* Fill in args */
    args.xu = u;
    args.fp = fp;
    /* Reset PC */
    pc = u->code_origin;
    /* Do the walk */
    bytecode_walk(u->_unit_.codeseg.bytes, handlers, (void *)&args);
}

#define XLNK_NO_DEBUG
#ifndef XLNK_NO_DEBUG

/*--------------------------------------------------------------------------*/
/* Functions for debugging bytecodes. */

/**
 * Gets string representation of bytecode command.
 * @param cmd CMD_*
 * @return String representation ("CMD_*")
 */
static const char *bytecode_to_string(unsigned char cmd)
{
    switch (cmd) {
        case CMD_FILE:  return "CMD_FILE";
        case CMD_LINE8: return "CMD_LINE8";
        case CMD_LINE16:return "CMD_LINE16";
        case CMD_LINE24:return "CMD_LINE24";
        case CMD_LINE_INC:  return "CMD_LINE_INC";
        case CMD_END:   return "CMD_END";
        case CMD_BIN8:  return "CMD_BIN8";
        case CMD_BIN16: return "CMD_BIN16";
        case CMD_LABEL: return "CMD_LABEL";
        case CMD_INSTR: return "CMD_INSTR";
        case CMD_DB:    return "CMD_DB";
        case CMD_DW:    return "CMD_DW";
        case CMD_DD:    return "CMD_DD";
        case CMD_DSI8:  return "CMD_DSI8";
        case CMD_DSI16: return "CMD_DSI16";
        case CMD_DSB:   return "CMD_DSB";
    }
    return "bytecode_to_string: invalid bytecode";
}

/**
 * Print a bytecode.
 * @param b Bytecodes
 * @param arg Not used
 */
static void print_it(unsigned char *b, void *arg)
{
    printf("%s\n", bytecode_to_string(b[0]) );
}

/**
 * Prints bytecodes.
 * @param bytes Bytecodes
 */
static void print_bytecodes(unsigned char *bytes)
{
    bytecodeproc handlers[] =
    {
        print_it,print_it,print_it,print_it,print_it,
        print_it,print_it,print_it,print_it,print_it,
        print_it,print_it,print_it
    };
    bytecode_walk(bytes, handlers, NULL);
}

/**
 * Prints a unit.
 * @param u Unit
 */
static void print_unit(unit *u)
{
    print_bytecodes(u->dataseg.bytes);
    print_bytecodes(u->codeseg.bytes);
}

#endif /* !XLNK_NO_DEBUG */

/*--------------------------------------------------------------------------*/
/* Functions for managing arrays of unit locals. */

/**
 * Creates array of locals.
 * @param size Number of locals
 * @param la Local array
 */
static void create_local_array(int size, local_array *la)
{
    la->size = size;
    /* Allocate space for entries */
    if (size > 0) {
        la->entries = (local *)malloc(sizeof(local) * size);
    }
    else {
        la->entries = NULL;
    }
}

/**
 * Finalizes array of locals.
 */
static void finalize_local_array(local_array *la)
{
    int i;
    /* Free entry attributes */
    for (i=0; i<la->size; i++) {
        SAFE_FREE(la->entries[i].name);
    }
    /* Free array itself */
    SAFE_FREE(la->entries);
}

/*--------------------------------------------------------------------------*/
/* Functions for counting and registering locals in a unit. */
/* In bytecode expressions, locals are referred to by their index.
   In order to not have to go through the bytecodes every time to
   find a label definition, the following functions build an array
   of structures that can be indexed by the local ID to obtain its
   information.
*/

/**
 * Counts this local.
 */
static void count_one_local(unsigned char *b, void *arg)
{
    /* Argument points to the counter */
    int *count = (int *)arg;
    /* Increment count */
    (*count)++;
}

/**
 * Counts the number of locals (labels) in an array of bytecodes.
 * @param b Bytecodes, terminated by CMD_END
 * @return Number of locals counted
 */
static int count_locals(unsigned char *b)
{
    int count;
    /* Table of callback functions for our purpose. */
    bytecodeproc handlers[] =
    {
        NULL,   /* CMD_END */
        NULL,   /* CMD_BIN8 */
        NULL,   /* CMD_BIN16 */
        count_one_local,    /* CMD_LABEL */
        NULL,   /* CMD_INSTR */
        NULL,   /* CMD_DB */
        NULL,   /* CMD_DW */
        NULL,   /* CMD_DD */
        NULL,   /* CMD_DSI8 */
        NULL,   /* CMD_DSI16 */
        NULL    /* CMD_DSB */
    };
    /* Reset count */
    count = 0;
    /* Count the locals now */
    bytecode_walk(b, handlers, (void *)&count);
    /* Return the number of locals counted */
    return count;
}

static xunit *reg_unit = NULL;

/**
 * Puts this local into array of locals for current unit.
 */
static void register_one_local(unsigned char *b, void *arg)
{
    int len;
    int i= 1;
    /* Argument points to a pointer which points to the local struct to fill in */
    local **lpptr = (local **)arg;
    local *lptr = *lpptr;
    /* Initialize some fields */
    lptr->resolved = 0;
    lptr->ref_count = 0;
    lptr->name = NULL;
    lptr->align = 1;
    lptr->owner = reg_unit;
    /* Get flag byte */
    lptr->flags = get_1(b, &i);
    /* Test export flag */
    if (lptr->flags & LABEL_FLAG_EXPORT) {
        /* Get the length of the name */
        len = get_1(b, &i) + 1;
        /* Allocate space for name */
        lptr->name = (char *)malloc( len + 1 );
        if (lptr->name != NULL) {
            /* Copy name from bytecodes */
            memcpy(lptr->name, &b[i], len);
            /* Zero-terminate string */
            lptr->name[len] = '\0';
        }
        i += len;
    }
    /* Test align flag */
    if (lptr->flags & LABEL_FLAG_ALIGN) {
        lptr->align = get_1(b, &i);
    }
    /* Test address flag */
    if (lptr->flags & LABEL_FLAG_ADDR) {
        lptr->phys_addr = get_2(b, &i);
        lptr->resolved = 1;
    }
    /* Point to next local in array */
    *lpptr += 1;
}

/**
 * Puts all locals found in the array of bytecodes into array.
 * @param b Bytecodes, terminated by CMD_END
 * @param la Pointer to array to receive locals
 * @param xu Owner unit
 */
static void register_locals(unsigned char *b, local_array *la, xunit *xu)
{
    local *lptr;
    local **lpptr;
    /* Table of callback functions for our purpose. */
    bytecodeproc handlers[] =
    {
        NULL,   /* CMD_END */
        NULL,   /* CMD_BIN8 */
        NULL,   /* CMD_BIN16 */
        register_one_local, /* CMD_LABEL */
        NULL,   /* CMD_INSTR */
        NULL,   /* CMD_DB */
        NULL,   /* CMD_DW */
        NULL,   /* CMD_DD */
        NULL,   /* CMD_DSI8 */
        NULL,   /* CMD_DSI16 */
        NULL    /* CMD_DSB */
    };
    /* Create array of locals */
    create_local_array(count_locals(b), la);
    /* Prepare args */
    lptr = la->entries;
    lpptr = &lptr;
    reg_unit = xu;
    /* Go! */
    bytecode_walk(b, handlers, (void *)lpptr);
}

/*--------------------------------------------------------------------------*/
/* Functions for entering exported symbols into proper hash table. */

/**
 * Enters an exported symbol into a hash table.
 * @param tab Hash table to enter it into
 * @param key Key
 * @param data Data
 * @param u Owner unit
 */
static void enter_exported_symbol(hashtab *tab, void *key, void *data, unit *u)
{
    /* Make sure symbol doesn't already exist */
    if ((hashtab_get(label_hash, key) != NULL) ||
    (hashtab_get(constant_hash, key) != NULL) ) {
        /* Error, duplicate symbol */
        err("duplicate symbol `%s' exported from unit `%s'", (char *)key, u->name);
    }
    else {
        /* Enter it */
        hashtab_put(tab, key, data);
    }
}

/**
 * Enters all constants in a unit into the proper hash table.
 * @param u Unit whose constants to enter
 */
static void enter_exported_constants(unit *u)
{
    int i;
    constant *c;
    /* Go through all constants in unit */
    for (i=0; i<u->const_count; i++) {
        c = &u->constants[i];
        enter_exported_symbol(constant_hash, (void *)c->name, (void *)c, u);
    }
}

/**
 * Enters locals which should be globally visible into the proper hash table.
 * @param la Array of locals
 * @param u Owner unit
 */
static void enter_exported_locals(local_array *la, unit *u)
{
    int i;
    local *l;
    /* Go through all locals */
    for (i=0; i<la->size; i++) {
        l = &la->entries[i];
        /* If it has a name, it is exported */
        if (l->name != NULL) {
            enter_exported_symbol(label_hash, (void *)l->name, (void *)l, u);
        }
    }
}

/*--------------------------------------------------------------------------*/
/* Functions for calculating addresses of data labels in a unit. */

/**
 * Sets the virtual address of this local to current PC value.
 */
static void set_data_address(unsigned char *b, void *arg)
{
    calc_address_args *args = (calc_address_args *)arg;
    /* Get the label */
    local *l = &args->xu->data_locals.entries[args->index];
    if (!l->resolved) {
        /* Set the virtual address */
        l->virt_addr = pc;
    }
    /* Increase label index */
    args->index++;
}

/**
 * Calculates addresses of labels in a data segment relative to 0.
 * Only a small set of bytecode commands are allowed in a data segment:
 * - label (which we want to assign a virtual address)
 * - storage (constant or variable)
 */
static void calc_data_addresses(xunit *u)
{
    calc_address_args args;
    /* Table of callback functions for our purpose. */
    bytecodeproc handlers[] =
    {
        NULL,       /* CMD_END */
        NULL,       /* CMD_BIN8 */
        NULL,       /* CMD_BIN16 */
        set_data_address,   /* CMD_LABEL */
        NULL,       /* CMD_INSTR */
        NULL,       /* CMD_DB */
        NULL,       /* CMD_DW */
        NULL,       /* CMD_DD */
        inc_pc_count8,  /* CMD_DSI8 */
        inc_pc_count16, /* CMD_DSI16 */
        inc_pc_dsb  /* CMD_DSB */
    };
    /* Fill in args */
    args.xu = u;
    args.index = 0;
    /* Reset PC */
    pc = 0;
    /* Map away! */
    bytecode_walk(u->_unit_.dataseg.bytes, handlers, (void *)&args);
    /* Store the end address, which is the total size of data */
    u->data_size = pc;
}

/*--------------------------------------------------------------------------*/

/* Constructs 32-bit sort key for local. */
#define SORT_KEY(l) (unsigned long)((((l)->flags & LABEL_FLAG_ZEROPAGE) << 30) | ((l)->align << 24) | (0x10000-(l)->size))

/**
 * Array is sorted from high to low value.
 */
static int label_partition(local **a, int p, int r)
{
    int x;
    int i;
    int j;
    x = SORT_KEY(a[r]);
    i = p - 1;
    local *temp;
    for (j=p; j<r; j++) {
        if (SORT_KEY(a[j]) >= x) {
            i = i + 1;
            temp = a[i];
            a[i] = a[j];
            a[j] = temp;
        }
    }
    temp = a[i+1];
    a[i+1] = a[r];
    a[r] = temp;
    return i + 1;
}

/**
 * Quicksort implementation used to sort array of pointers to locals.
 */
static void label_qsort(local **a, int p, int r)
{
    int q;
    if (p < r) {
        q = label_partition(a, p, r);
        label_qsort(a, p, q-1);
        label_qsort(a, q+1, r);
    }
}

/**
 * Maps all data labels to 6502 RAM locations.
 * This is a very important function. It takes all the data labels from all
 * the loaded units and attempts to assign them unique physical addresses.
 * The list of target RAM blocks given in the linker script is the premise.
 */
static void map_data_to_ram()
{
    int i, j, k;
    local_array *la;
    local **total_order;
    local *l;
    int count;
    int size;
    /* Calculate total number of labels to map */
    count = 0;
    for (i=0; i<unit_count; i++) {
        count += units[i].data_locals.size;
    }
    /* Put pointers to all data labels in one big array */
    total_order = (local **)malloc( count * sizeof(local *) );
    for (i=0, k=0; i<unit_count; i++) {
        la = &units[i].data_locals;
        for (j=0; j<la->size; j++) {
            /* Use virtual addresses to calculate size from this label to next */
            if (j == la->size-1) {
                size = units[i].data_size;
            }
            else {
                size = la->entries[j+1].virt_addr;
            }
            la->entries[j].size = size - la->entries[j].virt_addr;
            /* Put pointer in array */
            total_order[k++] = &la->entries[j];
        }
    }
    /* Sort them */
    label_qsort(total_order, 0, count-1);
    /* Map them */
    for (i=0; i<count; i++) {
        l = total_order[i];
        /* Try to allocate it */
        if (alloc_ram(l) == 1) {
            /* Good, label mapped successfully */
            l->resolved = 1;
            if (l->name != NULL) {
                verbose("  %.4X-%.4X %s", l->phys_addr, l->phys_addr + l->size-1, l->name);
            }
        }
        else {
            /* Error, couldn't allocate */
            err("out of 6502 RAM while allocating unit `%s'", l->owner->_unit_.name);
            return;
        }
    }
    free(total_order);
}

/*--------------------------------------------------------------------------*/
/* Functions for calculating offsets of code labels in a unit. */

/**
 * Sets the address of this code label to current PC.
 */
static void set_code_address(unsigned char *b, void *arg)
{
    calc_address_args *args = (calc_address_args *)arg;
    /* Get the label */
    local *l = &args->xu->code_locals.entries[args->index];
    if (!l->resolved) {
        /* Set the physical address to current PC */
        l->phys_addr = pc;
        l->resolved = 1;
        if ((l->name != NULL) && program_args.verbose) {
            printf("  %.4X %s\n", l->phys_addr, l->name);
        }
    }
    /* Increase label index */
    args->index++;
}

/**
 * Calculates addresses of code labels in a segment.
 * NOTE: Only the virtual addresses (relative to 0) are calculated.
 * The labels then need to be relocated to obtain the physical address (see below).
 * @param u Unit
 */
static void calc_code_addresses(xunit *u)
{
    calc_address_args args;
    /* Table of callback functions for our purpose. */
    bytecodeproc handlers[] =
    {
        NULL,       /* CMD_END */
        inc_pc_count8,  /* CMD_BIN8 */
        inc_pc_count16, /* CMD_BIN16 */
        set_code_address,   /* CMD_LABEL */
        inc_pc_instr,   /* CMD_INSTR */
        inc_pc_1,   /* CMD_DB -- TODO, error if string */
        inc_pc_2,   /* CMD_DW */
        inc_pc_4,   /* CMD_DD */
        inc_pc_count8,  /* CMD_DSI8 */
        inc_pc_count16, /* CMD_DSI16 */
        inc_pc_dsb  /* CMD_DSB */
    };
    /* Fill in args */
    args.xu = u;
    args.index = 0;
    /* Do the walk */
    bytecode_walk(u->_unit_.codeseg.bytes, handlers, (void *)&args);
    /* Store the total size of code */
    u->code_size = pc - u->code_origin;
}

/*--------------------------------------------------------------------------*/

/**
 * Issues a script error.
 */
static void scripterr(script *s, script_command *c, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    /* Print error message */
    fprintf(stderr, "error: %s:%d: `%s': ", s->name, c->line, script_command_type_to_string(c->type) );
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");

    va_end(ap);

    /* Increase error count */
    err_count++;
}

#define require_arg(s, c, a, d) { \
    d = script_get_command_arg(c, a); \
    if (d == NULL) { \
        scripterr(s, c, "missing argument `%s'", a); \
        return; \
    } \
}

#define require_arg_in_range(s, c, a, v, l, h) { \
    if (((v) < (l)) || ((v) > (h))) { \
        scripterr(s, c, "value of argument `%s' is out of range", a); \
        return; \
    } \
}

/*--------------------------------------------------------------------------*/
/* Functions for registering RAM blocks in script. */

/**
 * Registers one RAM block based on 'ram' script command.
 * @param s Linker script
 * @param c Command of type RAM_COMMAND
 * @param arg Not used
 */
static void register_one_ram_block(script *s, script_command *c, void *arg)
{
    int start;
    int end;
    char *start_str;
    char *end_str;
    /* Get arguments */
    require_arg(s, c, "start", start_str);
    require_arg(s, c, "end", end_str);
    /* Convert to integers */
    start = str_to_int(start_str);
    end = str_to_int(end_str);
    /* Check that they are sane */
    require_arg_in_range(s, c, "start", start, 0x0000, 0xFFFF);
    require_arg_in_range(s, c, "end", end, 0x0000, 0xFFFF);
    if (end <= start) {
        scripterr(s, c, "`end' is smaller than `start'");
    }
    /* Add block */
    add_ram_block(start, end);
}

/**
 * Registers RAM blocks based on 'ram' commands in a script.
 * @param sc Linker script
 */
static void register_ram_blocks(script *sc)
{
    /* Table of mappings for our purpose */
    static script_commandprocmap map[] = {
        { RAM_COMMAND, register_one_ram_block },
        { BAD_COMMAND, NULL }
    };
    /* Do the walk */
    script_walk(sc, map, NULL);
    /* Calculate total RAM size */
    total_ram = ram_left();
}

/*--------------------------------------------------------------------------*/
/* Functions for loading and initial processing of units in script. */

/**
 * Registers (parses etc.) one unit based on 'link' script command.
 * @param s Linker script
 * @param c Command of type LINK_COMMAND
 * @param arg Pointer to unit index
 */
static void register_one_unit(script *s, script_command *c, void *arg)
{
    char *file;
    int *i;
    xunit *xu;
    /* Get unit filename */
    require_arg(s, c, "file", file);
    /* arg is pointer to unit index */
    i = (int *)arg;
    /* Get pointer to xunit to fill in */
    xu = &units[*i];
    /* Read basic unit from file */
    if (unit_read(file, &xu->_unit_) == 0) {
        /* Something bad happened when trying to read unit */
        scripterr(s, c, "failed to load unit `%s'", file);
        xu->loaded = 0;
        return;
    }
    xu->loaded = 1;
    verbose("  unit `%s' loaded", file);
    /* Register locals for both segments */
    verbose("    registering local symbols...");
    register_locals(xu->_unit_.dataseg.bytes, &xu->data_locals, xu);
    register_locals(xu->_unit_.codeseg.bytes, &xu->code_locals, xu);
    /* Enter exported symbols into hash tables */
    verbose("    registering public symbols...");
    enter_exported_constants(&xu->_unit_);
    enter_exported_locals(&xu->data_locals, &xu->_unit_);
    enter_exported_locals(&xu->code_locals, &xu->_unit_);
    /* Put unit in hash table */
    hashtab_put(unit_hash, file, xu);
    /* Increment unit index */
    (*i)++;
}

/**
 * Registers units based on 'link' commands in script.
 * @param sc Linker script
 */
static void register_units(script *sc)
{
    /* Table of mappings for our purpose */
    static script_commandprocmap map[] = {
        { LINK_COMMAND, register_one_unit },
        { BAD_COMMAND, NULL }
    };
    int i = 0;
    /* Do the walk */
    script_walk(sc, map, (void *)&i);
}

/*--------------------------------------------------------------------------*/
/* Functions for composing a binary file based on a sequential list of
script commands. */

/**
 * Sets the output file according to 'output' script command.
 * @param s Linker script
 * @param c Command of type OUTPUT_COMMAND
 * @param arg Pointer to file handle
 */
static void set_output(script *s, script_command *c, void *arg)
{
    char *file;
    FILE **fpp;
    /* Get the name of new output file */
    require_arg(s, c, "file", file);
    /* Arg is pointer to file handle pointer */
    fpp = (FILE **)arg;
    /* Close current file */
    if (*fpp != NULL) {
        fclose(*fpp);
    }
    /* Attempt to open new file */
    *fpp = fopen(file, "wb");
    if (*fpp == NULL) {
        scripterr(s, c, "could not open `%s' for writing", file);
    }
    else {
        verbose("  output goes to `%s'", file);
    }
}

/**
 * Copies a file to output according to 'copy' script command.
 * @param s Linker script
 * @param c Command of type COPY_COMMAND
 * @param arg Pointer to file handle
 */
static void copy_to_output(script *s, script_command *c, void *arg)
{
    char *file;
    FILE **fpp;
    FILE *cf;
    unsigned char k;
    /* Arg is pointer to file handle pointer */
    fpp = (FILE **)arg;
    /* Make sure there is a file to write to */
    if (*fpp == NULL) {
        scripterr(s, c, "no output open");
    }
    else {
        /* Get the name of file to copy */
        require_arg(s, c, "file", file);
        /* Attempt to open the file to copy */
        cf = fopen(file, "rb");
        if (cf == NULL) {
            scripterr(s, c, "could not open `%s' for reading", file);
        }
        else {
            verbose("  copying `%s' to output at position %ld...", file, ftell(*fpp) );
            /* Copy it to output, byte for byte */
            for (k = fgetc(cf); !feof(cf); k = fgetc(cf) ) {
                fputc(k, *fpp);
            }
            /* Advance offset */
            bank_offset += ftell(cf);
            pc += ftell(cf);
            /* Close the copied file */
            fclose(cf);
            /* Check if exceeded bank size */
            if (bank_offset > bank_size) {
                scripterr(s, c, "bank size (%d) exceeded by %d bytes", bank_size, bank_offset - bank_size);
            }
        }
    }
}

/**
 * Starts a new bank according to 'bank' script command.
 * @param s Linker script
 * @param c Command of type BANK_COMMAND
 * @param arg Pointer to file handle
 */
static void start_bank(script *s, script_command *c, void *arg)
{
    char *size_str;
    char *origin_str;
    /* See if size specified */
    size_str = script_get_command_arg(c, "size");
    if (size_str != NULL) {
        /* Set new bank size */
        bank_size = str_to_int(size_str);
        /* Sanity check */
        if (bank_size <= 0) {
            scripterr(s, c, "invalid size");
        }
    }
    else {
        /* Use bank size of previous bank if there was one */
        /* Otherwise issue error */
        if (bank_size == 0x7FFFFFFF) {
            scripterr(s, c, "no bank size set");
        }
    }
    /* See if origin specified */
    origin_str = script_get_command_arg(c, "origin");
    if (origin_str != NULL) {
        /* Set new bank origin */
        bank_origin = str_to_int(origin_str);
        /* Sanity check */
        require_arg_in_range(s, c, "origin", bank_origin, 0x0000, 0xFFFF);
    }
    else {
        /* Use old bank origin */
    }
    bank_id++;
    /* Reset bank offset and PC */
    bank_offset = 0;
    pc = bank_origin;
}

/**
 * Writes unit according to 'link' script command.
 * @param s Linker script
 * @param c Command of type LINK_COMMAND
 * @param arg Pointer to file handle
 */
static void write_unit(script *s, script_command *c, void *arg)
{
    FILE **fpp;
    xunit *xu;
    char *file;
    /* Arg is pointer to file handle pointer */
    fpp = (FILE **)arg;
    /* Make sure there is a file to write to */
    if (*fpp == NULL) {
        scripterr(s, c, "no output open");
    }
    else {
        /* Get the name of the unit */
        require_arg(s, c, "file", file);
        /* Look it up */
        xu = (xunit *)hashtab_get(unit_hash, file);
        /* Write it */
        verbose("  appending unit `%s' to output at position %ld...", file, ftell(*fpp));
        write_as_binary(*fpp, xu);
        /* Advance offset */
        bank_offset += xu->code_size;
        /* Check if exceeded bank size */
        if (bank_offset > bank_size) {
            scripterr(s, c, "bank size (%d) exceeded by %d bytes", bank_size, bank_offset - bank_size);
        }
    }
}

/**
 * Pads output file according to 'pad' script command.
 * @param s Linker script
 * @param c Command of type PAD_COMMAND
 * @param arg Pointer to file handle
 */
static void write_pad(script *s, script_command *c, void *arg)
{
    FILE **fpp;
    int i;
    int count;
    int offset;
    int origin;
    char *offset_str;
    char *origin_str;
    char *size_str;
    /* Arg is pointer to file handle pointer */
    fpp = (FILE **)arg;
    /* Make sure there is a file to write to */
    if (*fpp == NULL) {
        scripterr(s, c, "no output open");
    }
    else {
        if ((offset_str = script_get_command_arg(c, "offset")) != NULL) {
            offset = str_to_int(offset_str);
            /* Calculate number of zeroes to write */
            count = offset - bank_offset;
        }
        else if ((origin_str = script_get_command_arg(c, "origin")) != NULL) {
            origin = str_to_int(origin_str);
            /* Calculate number of zeroes to write */
            count = origin - pc;
        }
        else if ((size_str = script_get_command_arg(c, "size")) != NULL) {
            count = str_to_int(size_str);
        }
        else {
            scripterr(s, c, "missing argument");
            count = 0;
        }
        /* Sanity check */
        if (count < 0) {
            scripterr(s, c, "cannot pad backwards");
            count = 0;
        }
        else if (count > 0) {
            verbose("  padding %d bytes...", count);
        }
        /* Write zeroes */
        for (i=0; i<count; i++) {
            fputc(0, *fpp);
        }
        /* Advance offset */
        bank_offset += count;
        pc += count;
        /* Check if exceeded bank size */
        if (bank_offset > bank_size) {
            scripterr(s, c, "bank size (%d) exceeded by %d bytes", bank_size, bank_offset - bank_size);
        }
    }
}

/**
 * Pads to end of bank in file if bank size not reached.
 * @param s Linker script
 * @param c Command of type BANK_COMMAND
 * @param fp File handle
 */
static void maybe_pad_bank(script *s, script_command *c, FILE *fp)
{
    int i;
    if ( (bank_size != 0x7FFFFFFF) && (bank_offset < bank_size) ) {
        /* Make sure there is a file to write to */
        if (fp == NULL) {
            scripterr(s, c, "no output open");
        }
        else {
            /* Pad until bank size */
            for (i=bank_offset; i<bank_size; i++) {
                fputc(0, fp);
            }
        }
    }
}

/**
 * Finishes old bank in output and starts new bank.
 * @param s Linker script
 * @param c Command of type BANK_COMMAND
 * @param arg Pointer to file handle
 */
static void write_bank(script *s, script_command *c, void *arg)
{
    FILE **fpp;
    /* Arg is pointer to file handle pointer */
    fpp = (FILE **)arg;
    /* Pad bank if necessary */
    maybe_pad_bank(s, c, *fpp);
    /* Start new bank */
    start_bank(s, c, arg);
}

/**
 * Generates the final binary output from the linker.
 * @param sc Linker script
 */
static void generate_output(script *sc)
{
    FILE *fp = NULL;
    /* Table of mappings for our purpose */
    static script_commandprocmap map[] = {
        { OUTPUT_COMMAND, set_output },
        { COPY_COMMAND, copy_to_output },
        { BANK_COMMAND, write_bank },
        { LINK_COMMAND, write_unit },
        { PAD_COMMAND, write_pad },
        { BAD_COMMAND, NULL }
    };
    /* Reset offsets */
    bank_size = 0x7FFFFFFF;
    bank_offset = 0;
    bank_origin = 0;
    bank_id = -1;
    pc = 0;
    /* Do the walk */
    script_walk(sc, map, (void *)&fp);
    /* Pad last bank if necessary */
    maybe_pad_bank(sc, sc->first_command, fp);
}

/*--------------------------------------------------------------------------*/

/**
 * Increases bank offset and PC according to size of the file specified by
 * 'copy' script command.
 * @param s Linker script
 * @param c Command of type COPY_COMMAND
 * @param arg Not used
 */
static void inc_offset_copy(script *s, script_command *c, void *arg)
{
    char *file;
    FILE *fp;
    /* Get the name of the file */
    require_arg(s, c, "file", file);
    /* Attempt to it */
    fp = fopen(file, "rb");
    if (fp == NULL) {
        scripterr(s, c, "could not open `%s' for reading", file);
    }
    else {
        /* Seek to end */
        fseek(fp, 0, SEEK_END);
        /* Advance offset */
        bank_offset += ftell(fp);
        pc += ftell(fp);
        /* Close the file */
        fclose(fp);
        /* Check if exceeded bank size */
        if (bank_offset > bank_size) {
            scripterr(s, c, "bank size (%d) exceeded by %d bytes", bank_size, bank_offset - bank_size);
        }
    }
}

/**
 * Sets the origin of a unit and relocates its code to this location.
 * @param s Linker script
 * @param c Command of type LINK_COMMAND
 * @param arg Not used
 */
static void set_unit_origin(script *s, script_command *c, void *arg)
{
    xunit *xu;
    char *file;
    char *origin_str;
    int origin;
    /* Get the unit filename */
    require_arg(s, c, "file", file);
    /* Look it up */
    xu = (xunit *)hashtab_get(unit_hash, file);
    /* Check if origin specified */
    origin_str = script_get_command_arg(c, "origin");
    if (origin_str != NULL) {
        origin = str_to_int(origin_str);
        require_arg_in_range(s, c, "origin", origin, 0x0000, 0xFFFF);
        xu->code_origin = origin;
        pc = origin;
    }
    else {
        /* No origin specified. Set to PC. */
        xu->code_origin = pc;
    }
    xu->bank_id = bank_id;
    /* Now we can calculate the physical code addresses of the unit. */
    calc_code_addresses(xu);
    /* Print info if verbose mode */
    verbose("  unit `%s' relocated to %.4X", xu->_unit_.name, xu->code_origin);
    /* Increase bank offset */
    bank_offset += xu->code_size;
}

/**
 * Increases bank offset and PC according to 'pad' script command.
 * @param s Linker script
 * @param c Command of type PAD_COMMAND
 * @param arg Not used
 */
static void inc_offset_pad(script *s, script_command *c, void *arg)
{
    int count;
    int offset;
    int origin;
    char *offset_str;
    char *origin_str;
    char *size_str;
    if ((offset_str = script_get_command_arg(c, "offset")) != NULL) {
        offset = str_to_int(offset_str);
        /* Calculate number of zeroes to write */
        count = offset - bank_offset;
    }
    else if ((origin_str = script_get_command_arg(c, "origin")) != NULL) {
        origin = str_to_int(origin_str);
        /* Calculate number of zeroes to write */
        count = origin - pc;
    }
    else if ((size_str = script_get_command_arg(c, "size")) != NULL) {
        count = str_to_int(size_str);
    }
    else {
        /* Error */
        scripterr(s, c, "missing argument");
        count = 0;
    }
    /* Sanity check */
    if (count < 0) {
        scripterr(s, c, "cannot pad %d bytes backwards", -count);
        count = 0;
    }
    /* Advance offset */
    bank_offset += count;
    pc += count;
    /* Check if exceeded bank size */
    if (bank_offset > bank_size) {
        scripterr(s, c, "bank size (%d) exceeded by %d bytes", bank_size, bank_offset - bank_size);
    }
}

/**
 * Relocates code of all units according to script commands and/or their position
 * in the final binary.
 * @param sc Linker script
 */
static void relocate_units(script *sc)
{
    /* Table of mappings for our purpose */
    static script_commandprocmap map[] = {
        { COPY_COMMAND, inc_offset_copy },
        { BANK_COMMAND, start_bank },
        { LINK_COMMAND, set_unit_origin },
        { PAD_COMMAND, inc_offset_pad },
        { BAD_COMMAND, NULL }
    };
    /* Reset offsets */
    bank_size = 0x7FFFFFFF;
    bank_offset = 0;
    bank_origin = 0;
    bank_id = -1;
    pc = 0;
    /* Do the walk */
    script_walk(sc, map, NULL);
}

/**
 *
 */
static void maybe_print_ram_statistics()
{
    int used;
    int left;
    if (total_ram > 0) {
        left = ram_left();
        used = total_ram - left;
        verbose("  total RAM: %d bytes", total_ram);
        verbose("  RAM used:  %d bytes (%d%%)", used, (int)(((float)used / (float)total_ram)*100.0f) );
        verbose("  RAM left:  %d bytes (%d%%)", left, (int)(((float)left / (float)total_ram)*100.0f) );
    }
}

/*--------------------------------------------------------------------------*/

/**
 * Program entrypoint.
 */
int main(int argc, char **argv)
{
    int i;
    script sc;

    /* Parse our arguments. */
    parse_arguments(argc, argv);

    suppress = 0;
    /* Reset error and warning count */
    err_count = 0;
    warn_count = 0;

    /* Parse the linker script */
    verbose("parsing linker script...");
    if (script_parse(program_args.input_file, &sc) == 0) {
        /* Something bad happened when parsing script, halt */
        return(1);
    }

    /* Process all ram commands */
    verbose("registering RAM blocks...");
    register_ram_blocks(&sc);

    /* Create hash tables to hold symbols */
    constant_hash = hashtab_create(23, HASHTAB_STRKEYHSH, HASHTAB_STRKEYCMP);
    label_hash = hashtab_create(23, HASHTAB_STRKEYHSH, HASHTAB_STRKEYCMP);
    unit_hash = hashtab_create(11, HASHTAB_STRKEYHSH, HASHTAB_STRKEYCMP);

    /* Count units. One unit per link command. */
    unit_count = script_count_command_type(&sc, LINK_COMMAND);
    /* Allocate array of xunits */
    if (unit_count > 0) {
        units = (xunit *)malloc( sizeof(xunit) * unit_count );
    }
    else {
        units = NULL;
    }
    /* Process link commands */
    verbose("loading units...");
    register_units(&sc);
    /* Make sure all units were loaded */
    if (err_count != 0) {
        // TODO
    }

    /* Only continue with processing if no unresolved symbols */
    if (err_count == 0) {
        /* Calculate 0-relative addresses of data labels */
        verbose("calculating data addresses...");
        for (i=0; i<unit_count; i++) {
            calc_data_addresses(&units[i]);
        }

        // TODO: Count references: go through all instructions, find EXTRN and LOCAL operands in expressions
        // TODO: Find modes of access for each DATA label (i.e. label MUST be allocated in zero page)

        /* Map all data labels to 6502 RAM locations */
        verbose("mapping data to RAM...");
        map_data_to_ram();
        maybe_print_ram_statistics();

        /* Only continue with processing if all data labels were mapped */
        if (err_count == 0) {
            verbose("relocating code...");
            suppress = 1;
            relocate_units(&sc);
            suppress = 0;
            relocate_units(&sc);

            /* Only continue with processing if all code labels were mapped */
            if (err_count == 0) {
                verbose("generating output...");
                generate_output(&sc);
            }
        }
    }

    /* Cleanup */
    verbose("cleaning up...");

    /* Finalize units */
    for (i=0; i<unit_count; i++) {
        if (units[i].loaded) {
            finalize_local_array( &units[i].data_locals );
            finalize_local_array( &units[i].code_locals );
            unit_finalize( &units[i]._unit_ );
        }
    }
    /* Finalize hash tables */
    hashtab_finalize(label_hash);
    hashtab_finalize(constant_hash);
    hashtab_finalize(unit_hash);
    /* Finalize RAM blocks */
    finalize_ram_blocks();
    /* Finalize the script */
    script_finalize(&sc);

    /* All done. */
    return (err_count == 0) ? 0 : 1;
}
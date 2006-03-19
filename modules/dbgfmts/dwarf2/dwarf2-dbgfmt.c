/*
 * DWARF2 debugging format
 *
 *  Copyright (C) 2006  Peter Johnson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND OTHER CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR OTHER CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <util.h>
/*@unused@*/ RCSID("$Id$");

#define YASM_LIB_INTERNAL
#define YASM_BC_INTERNAL
#include <libyasm.h>

#include "dwarf2-dbgfmt.h"

struct dwarf2_head {
    yasm_dbgfmt_dwarf2 *dbgfmt_dwarf2;
    yasm_bytecode *start_prevbc;
    yasm_bytecode *end_prevbc;
    /*@null@*/ yasm_symrec *debug_ptr;
    int with_address;
    int with_segment;
};

/* Bytecode callback function prototypes */
static void dwarf2_head_bc_destroy(void *contents);
static void dwarf2_head_bc_print(const void *contents, FILE *f,
				 int indent_level);
static yasm_bc_resolve_flags dwarf2_head_bc_resolve
    (yasm_bytecode *bc, int save, yasm_calc_bc_dist_func calc_bc_dist);
static int dwarf2_head_bc_tobytes
    (yasm_bytecode *bc, unsigned char **bufp, void *d,
     yasm_output_value_func output_value,
     /*@null@*/ yasm_output_reloc_func output_reloc);

/* Bytecode callback structures */
static const yasm_bytecode_callback dwarf2_head_bc_callback = {
    dwarf2_head_bc_destroy,
    dwarf2_head_bc_print,
    yasm_bc_finalize_common,
    dwarf2_head_bc_resolve,
    dwarf2_head_bc_tobytes
};

/* Section data callback function prototypes */
static void dwarf2_section_data_destroy(void *data);
static void dwarf2_section_data_print(void *data, FILE *f, int indent_level);

/* Section data callback */
const yasm_assoc_data_callback yasm_dwarf2__section_data_cb = {
    dwarf2_section_data_destroy,
    dwarf2_section_data_print
};

yasm_dbgfmt_module yasm_dwarf2_LTX_dbgfmt;


static /*@null@*/ /*@only@*/ yasm_dbgfmt *
dwarf2_dbgfmt_create(yasm_object *object, yasm_objfmt *of, yasm_arch *a)
{
    yasm_dbgfmt_dwarf2 *dbgfmt_dwarf2 =
	yasm_xmalloc(sizeof(yasm_dbgfmt_dwarf2));
    size_t i;

    dbgfmt_dwarf2->dbgfmt.module = &yasm_dwarf2_LTX_dbgfmt;

    dbgfmt_dwarf2->object = object;
    dbgfmt_dwarf2->symtab = yasm_object_get_symtab(object);
    dbgfmt_dwarf2->linemap = yasm_object_get_linemap(object);
    dbgfmt_dwarf2->arch = a;

    dbgfmt_dwarf2->dirs_allocated = 32;
    dbgfmt_dwarf2->dirs_size = 0;
    dbgfmt_dwarf2->dirs =
	yasm_xmalloc(sizeof(char *)*dbgfmt_dwarf2->dirs_allocated);

    dbgfmt_dwarf2->filenames_allocated = 32;
    dbgfmt_dwarf2->filenames_size = 0;
    dbgfmt_dwarf2->filenames =
	yasm_xmalloc(sizeof(dwarf2_filename)*dbgfmt_dwarf2->filenames_allocated);
    for (i=0; i<dbgfmt_dwarf2->filenames_allocated; i++) {
	dbgfmt_dwarf2->filenames[i].pathname = NULL;
	dbgfmt_dwarf2->filenames[i].filename = NULL;
	dbgfmt_dwarf2->filenames[i].dir = 0;
    }

    dbgfmt_dwarf2->format = DWARF2_FORMAT_32BIT;    /* TODO: flexible? */

    dbgfmt_dwarf2->sizeof_address = yasm_arch_get_address_size(a)/8;
    switch (dbgfmt_dwarf2->format) {
	case DWARF2_FORMAT_32BIT:
	    dbgfmt_dwarf2->sizeof_offset = 4;
	    break;
	case DWARF2_FORMAT_64BIT:
	    dbgfmt_dwarf2->sizeof_offset = 8;
	    break;
    }
    dbgfmt_dwarf2->min_insn_len = yasm_arch_min_insn_len(a);

    return (yasm_dbgfmt *)dbgfmt_dwarf2;
}

static void
dwarf2_dbgfmt_destroy(/*@only@*/ yasm_dbgfmt *dbgfmt)
{
    yasm_dbgfmt_dwarf2 *dbgfmt_dwarf2 = (yasm_dbgfmt_dwarf2 *)dbgfmt;
    size_t i;
    for (i=0; i<dbgfmt_dwarf2->dirs_size; i++)
	if (dbgfmt_dwarf2->dirs[i])
	    yasm_xfree(dbgfmt_dwarf2->dirs[i]);
    yasm_xfree(dbgfmt_dwarf2->dirs);
    for (i=0; i<dbgfmt_dwarf2->filenames_size; i++) {
	if (dbgfmt_dwarf2->filenames[i].pathname)
	    yasm_xfree(dbgfmt_dwarf2->filenames[i].pathname);
	if (dbgfmt_dwarf2->filenames[i].filename)
	    yasm_xfree(dbgfmt_dwarf2->filenames[i].filename);
    }
    yasm_xfree(dbgfmt_dwarf2->filenames);
    yasm_xfree(dbgfmt);
}

/* Add a bytecode to a section, updating offset on insertion;
 * no optimization necessary.
 */
yasm_bytecode *
yasm_dwarf2__append_bc(yasm_section *sect, yasm_bytecode *bc)
{
    yasm_bytecode *precbc = yasm_section_bcs_last(sect);
    bc->offset = precbc ? precbc->offset + precbc->len : 0;
    yasm_section_bcs_append(sect, bc);
    return precbc;
}

static void
dwarf2_dbgfmt_generate(yasm_dbgfmt *dbgfmt)
{
    yasm_dbgfmt_dwarf2 *dbgfmt_dwarf2 = (yasm_dbgfmt_dwarf2 *)dbgfmt;
    size_t num_line_sections;
    /*@null@*/ yasm_section *debug_info, *debug_line, *main_code;

    /* If we don't have any .file directives, generate line information
     * based on the asm source.
     */
    debug_line = yasm_dwarf2__generate_line(dbgfmt_dwarf2,
					    dbgfmt_dwarf2->filenames_size == 0,
					    &main_code, &num_line_sections);

    /* If we don't have a .debug_info (or it's empty), generate the minimal
     * set of .debug_info, .debug_aranges, and .debug_abbrev so that the
     * .debug_line we're generating is actually useful.
     */
    debug_info = yasm_object_find_general(dbgfmt_dwarf2->object, ".debug_info");
    if (num_line_sections > 0 &&
	(!debug_info || yasm_section_bcs_first(debug_info)
			== yasm_section_bcs_last(debug_info))) {
	debug_info = yasm_dwarf2__generate_info(dbgfmt_dwarf2, debug_line,
						main_code);
	yasm_dwarf2__generate_aranges(dbgfmt_dwarf2, debug_info);
	/*yasm_dwarf2__generate_pubnames(dbgfmt_dwarf2, debug_info);*/
    }
}

yasm_symrec *
yasm_dwarf2__bc_sym(yasm_symtab *symtab, yasm_bytecode *bc)
{
    /*@dependent@*/ yasm_symrec *sym;
    if (bc->symrecs && bc->symrecs[0])
	sym = bc->symrecs[0];
    else
	sym = yasm_symtab_define_label(symtab, ".bcsym", bc, 0, 0);
    return sym;
}

dwarf2_head *
yasm_dwarf2__add_head
    (yasm_dbgfmt_dwarf2 *dbgfmt_dwarf2, yasm_section *sect,
     /*@null@*/ yasm_section *debug_ptr, int with_address, int with_segment)
{
    dwarf2_head *head;
    yasm_bytecode *bc;

    head = yasm_xmalloc(sizeof(dwarf2_head));
    head->dbgfmt_dwarf2 = dbgfmt_dwarf2;
    head->start_prevbc = yasm_section_bcs_last(sect);

    bc = yasm_bc_create_common(&dwarf2_head_bc_callback, head, 0);
    bc->len = dbgfmt_dwarf2->sizeof_offset + 2;
    if (dbgfmt_dwarf2->format == DWARF2_FORMAT_64BIT)
	bc->len += 4;

    if (debug_ptr) {
	head->debug_ptr =
	    yasm_dwarf2__bc_sym(dbgfmt_dwarf2->symtab,
				yasm_section_bcs_first(debug_ptr));
	bc->len += dbgfmt_dwarf2->sizeof_offset;
    } else
	head->debug_ptr = NULL;

    head->with_address = with_address;
    head->with_segment = with_segment;
    if (with_address)
	bc->len++;
    if (with_segment)
	bc->len++;

    head->end_prevbc = bc;
    yasm_dwarf2__append_bc(sect, bc);
    return head;
}

void
yasm_dwarf2__set_head_end(dwarf2_head *head, yasm_bytecode *end_prevbc)
{
    head->end_prevbc = end_prevbc;
}

static void
dwarf2_head_bc_destroy(void *contents)
{
    dwarf2_head *head = (dwarf2_head *)contents;
    yasm_xfree(contents);
}

static void
dwarf2_head_bc_print(const void *contents, FILE *f, int indent_level)
{
    /* TODO */
}

static yasm_bc_resolve_flags
dwarf2_head_bc_resolve(yasm_bytecode *bc, int save,
		       yasm_calc_bc_dist_func calc_bc_dist)
{
    yasm_internal_error(N_("tried to resolve a dwarf2 head bytecode"));
    /*@notreached@*/
    return YASM_BC_RESOLVE_MIN_LEN;
}

static int
dwarf2_head_bc_tobytes(yasm_bytecode *bc, unsigned char **bufp, void *d,
		       yasm_output_value_func output_value,
		       yasm_output_reloc_func output_reloc)
{
    dwarf2_head *head = (dwarf2_head *)bc->contents;
    yasm_dbgfmt_dwarf2 *dbgfmt_dwarf2 = head->dbgfmt_dwarf2;
    unsigned char *buf = *bufp;
    yasm_intnum *intn, *cval;

    if (dbgfmt_dwarf2->format == DWARF2_FORMAT_64BIT) {
	YASM_WRITE_8(buf, 0xff);
	YASM_WRITE_8(buf, 0xff);
	YASM_WRITE_8(buf, 0xff);
	YASM_WRITE_8(buf, 0xff);
    }

    /* Total length of aranges info (following this field) */
    cval = yasm_intnum_create_uint(dbgfmt_dwarf2->sizeof_offset);
    intn = yasm_common_calc_bc_dist(head->start_prevbc, head->end_prevbc);
    yasm_intnum_calc(intn, YASM_EXPR_SUB, cval, bc->line);
    yasm_arch_intnum_tobytes(dbgfmt_dwarf2->arch, intn, buf,
			     dbgfmt_dwarf2->sizeof_offset,
			     dbgfmt_dwarf2->sizeof_offset*8, 0, bc, 0, 0);
    buf += dbgfmt_dwarf2->sizeof_offset;
    yasm_intnum_destroy(intn);

    /* DWARF version */
    yasm_intnum_set_uint(cval, 2);
    yasm_arch_intnum_tobytes(dbgfmt_dwarf2->arch, cval, buf, 2, 16, 0, bc, 0,
			     0);
    buf += 2;

    /* Pointer to another debug section */
    if (head->debug_ptr) {
	yasm_value value;
	yasm_value_init_sym(&value, head->debug_ptr);
	output_value(&value, buf, dbgfmt_dwarf2->sizeof_offset,
		     dbgfmt_dwarf2->sizeof_offset*8, 0,
		     (unsigned long)(buf-*bufp), bc, 0, d);
	buf += dbgfmt_dwarf2->sizeof_offset;
    }

    /* Size of the offset portion of the address */
    if (head->with_address)
	YASM_WRITE_8(buf, dbgfmt_dwarf2->sizeof_address);

    /* Size of a segment descriptor.  0 = flat address space */
    if (head->with_segment)
	YASM_WRITE_8(buf, 0);

    *bufp = buf;

    yasm_intnum_destroy(cval);
    return 0;
}

static void
dwarf2_section_data_destroy(void *data)
{
    dwarf2_section_data *dsd = data;
    dwarf2_loc *n1, *n2;

    /* Delete locations */
    n1 = STAILQ_FIRST(&dsd->locs);
    while (n1) {
	n2 = STAILQ_NEXT(n1, link);
	yasm_xfree(n1);
	n1 = n2;
    }

    yasm_xfree(data);
}

static void
dwarf2_section_data_print(void *data, FILE *f, int indent_level)
{
    /* TODO */
}

static int
dwarf2_dbgfmt_directive(yasm_dbgfmt *dbgfmt, const char *name,
			yasm_section *sect, yasm_valparamhead *valparams,
			unsigned long line)
{
    yasm_dbgfmt_dwarf2 *dbgfmt_dwarf2 = (yasm_dbgfmt_dwarf2 *)dbgfmt;
    return yasm_dwarf2__line_directive(dbgfmt_dwarf2, name, sect, valparams,
				       line);
}

/* Define dbgfmt structure -- see dbgfmt.h for details */
yasm_dbgfmt_module yasm_dwarf2_LTX_dbgfmt = {
    "DWARF2 debugging format",
    "dwarf2",
    dwarf2_dbgfmt_create,
    dwarf2_dbgfmt_destroy,
    dwarf2_dbgfmt_directive,
    dwarf2_dbgfmt_generate
};

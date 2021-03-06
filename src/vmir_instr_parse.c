/*
 * Copyright (c) 2016 Lonelycoder AB
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


static ir_bb_t *
bb_make(ir_function_t *f)
{
  ir_bb_t *ib = calloc(1, sizeof(ir_bb_t));
  TAILQ_INIT(&ib->ib_instrs);
  ib->ib_id = f->if_num_bbs++;
  return ib;
}

/**
 *
 */
static ir_bb_t *
bb_add(ir_function_t *f, ir_bb_t *after)
{
  ir_bb_t *ib = bb_make(f);
  if(after != NULL)
    TAILQ_INSERT_AFTER(&f->if_bbs, after, ib, ib_link);
  else
    TAILQ_INSERT_TAIL(&f->if_bbs, ib, ib_link);
  return ib;
}


/**
 *
 */
static ir_bb_t *
bb_add_before(ir_function_t *f, ir_bb_t *before)
{
  ir_bb_t *ib = bb_make(f);
  TAILQ_INSERT_BEFORE(before, ib, ib_link);
  return ib;
}


/**
 *
 */
__attribute__((unused))
static ir_bb_t *
bb_add_named(ir_function_t *f, ir_bb_t *after, const char *name)
{
  ir_bb_t *ib = bb_add(f, after);
  ib->ib_name = strdup(name);
  return ib;
}


/**
 *
 */
static void
cfg_create_edge(ir_function_t *f, ir_bb_t *from, ir_bb_t *to)
{
  ir_bb_edge_t *ibe = malloc(sizeof(ir_bb_edge_t));
  LIST_INSERT_HEAD(&f->if_edges,             ibe, ibe_function_link);
  ibe->ibe_from = from;
  LIST_INSERT_HEAD(&from->ib_outgoing_edges, ibe, ibe_from_link);
  ibe->ibe_to   = to;
  LIST_INSERT_HEAD(&to->ib_incoming_edges,   ibe, ibe_to_link);
}


/**
 *
 */
static ir_valuetype_t
instr_get_vtp(ir_unit_t *iu, unsigned int *argcp, const ir_arg_t **argvp)
{
  const ir_arg_t *argv = *argvp;
  int argc = *argcp;
  if(argc < 1)
    parser_error(iu, "Missing value code");

  unsigned int val = iu->iu_next_value - argv[0].i64;
  int type;

  if(val < iu->iu_next_value) {
    *argvp = argv + 1;
    *argcp = argc - 1;
    type = VECTOR_ITEM(&iu->iu_values, val)->iv_type;
  } else {

    type = argv[1].i64;

    if(val >= VECTOR_LEN(&iu->iu_values)) {
      size_t prevsize = VECTOR_LEN(&iu->iu_values);
      VECTOR_RESIZE(&iu->iu_values, val + 1);
      for(int i = prevsize; i <= val; i++) {
        VECTOR_ITEM(&iu->iu_values, i) = NULL;
      }
    }
    ir_value_t *iv = VECTOR_ITEM(&iu->iu_values, val);

    if(iv == NULL) {
      iv = calloc(1, sizeof(ir_value_t));
      VECTOR_ITEM(&iu->iu_values, val) = iv;
      iv->iv_class = IR_VC_UNDEF;
    }
    iv->iv_type = type;

    *argvp = argv + 2;
    *argcp = argc - 2;
  }
  ir_valuetype_t r = {.type = type, .value = val};
  return r;
}


/**
 *
 */
static ir_valuetype_t
instr_get_value(ir_unit_t *iu, unsigned int *argcp, const ir_arg_t **argvp,
                int type)
{
  const ir_arg_t *argv = *argvp;
  int argc = *argcp;

  if(argc < 1)
    parser_error(iu, "Missing value code");

  *argvp = argv + 1;
  *argcp = argc - 1;

  int val = iu->iu_next_value - argv[0].i64;
  ir_valuetype_t r = {.type = type, .value = val};
  return r;
}


/**
 *
 */
static ir_valuetype_t
instr_get_value_signed(ir_unit_t *iu,
                       unsigned int *argcp, const ir_arg_t **argvp,
                       int type)
{
  const ir_arg_t *argv = *argvp;
  int argc = *argcp;

  if(argc < 1)
    parser_error(iu, "Missing value code");

  *argvp = argv + 1;
  *argcp = argc - 1;

  int val = iu->iu_next_value - read_sign_rotated(argv);

  ir_valuetype_t r = {.type = type, .value = val};
  return r;
}


/**
 *
 */
static unsigned int
instr_get_uint(ir_unit_t *iu, unsigned int *argcp, const ir_arg_t **argvp)
{
  const ir_arg_t *argv = *argvp;
  int argc = *argcp;

  if(argc < 1)
    parser_error(iu, "Missing argument");

  *argvp = argv + 1;
  *argcp = argc - 1;
  return argv[0].i64;
}

/**
 *
 */
static void *
instr_isa(ir_instr_t *ii, instr_class_t c)
{
  if(ii == NULL || ii->ii_class != c)
    return NULL;
  return ii;
}


/**
 *
 */
typedef struct ir_instr_unary {
  ir_instr_t super;
  ir_valuetype_t value;  // Value must be first so we can alias on ir_instr_move
  int op;

} ir_instr_unary_t;


/**
 *
 */
typedef struct ir_instr_store {
  ir_instr_t super;
  ir_valuetype_t ptr;
  ir_valuetype_t value;
  int immediate_offset;
} ir_instr_store_t;


typedef struct ir_instr_insertval {
  ir_instr_t super;
  ir_valuetype_t src;
  ir_valuetype_t replacement;
  int num_indicies;
  int indicies[0];
} ir_instr_insertval_t;


/**
 *
 */
typedef struct ir_instr_load {
  ir_instr_t super;
  ir_valuetype_t ptr;
  int immediate_offset;
  ir_valuetype_t value_offset;
  int value_offset_multiply;

  int load_type; // Only valid when cast != -1
  int8_t cast;
} ir_instr_load_t;


/**
 *
 */
typedef struct ir_instr_binary {
  ir_instr_t super;
  int op;
  ir_valuetype_t lhs_value;
  ir_valuetype_t rhs_value;

} ir_instr_binary_t;


/**
 *
 */
typedef struct ir_instr_ternary {
  ir_instr_t super;
  ir_valuetype_t arg1;
  ir_valuetype_t arg2;
  ir_valuetype_t arg3;
} ir_instr_ternary_t;


/**
 *
 */
typedef struct ir_gep_index {
  ir_valuetype_t value;
  int type;
} ir_gep_index_t;

/**
 *
 */
typedef struct ir_instr_gep {
  ir_instr_t super;
  int num_indicies;
  ir_valuetype_t baseptr;
  ir_gep_index_t indicies[0];
} ir_instr_gep_t;


/**
 *
 */
typedef struct ir_instr_lea {
  ir_instr_t super;
  ir_valuetype_t baseptr;
  int immediate_offset;
  ir_valuetype_t value_offset;
  int value_offset_multiply;
} ir_instr_lea_t;


/**
 *
 */
typedef struct ir_instr_br {
  ir_instr_t super;
  ir_valuetype_t condition;
  int true_branch;
  int false_branch;
} ir_instr_br_t;


typedef struct ir_phi_node {
  int predecessor;
  ir_valuetype_t value;
} ir_phi_node_t;

/**
 *
 */
typedef struct ir_instr_phi {
  ir_instr_t super;
  int num_nodes;
  ir_phi_node_t nodes[0];
} ir_instr_phi_t;


typedef struct ir_instr_arg {
  ir_valuetype_t value;
  int copy_size;
} ir_instr_arg_t;

/**
 * Shared with IR_IC_INTRINSIC
 */
typedef struct ir_instr_call {
  ir_instr_t super;
  ir_valuetype_t callee;
  int vmop;

  // the destination of the call.
  // if the instr_call is an invoke, then there are two possible destinations
  // the normal_dest which is jumped to if there is not exception thrown
  // or the unwind_dest which is jumped to if there was an exception thrown
  // if the instr_call is not an invoke, these member variables are not used.
  unsigned int normal_dest, unwind_dest;

  int argc;
  ir_instr_arg_t argv[0];
} ir_instr_call_t;

typedef ir_instr_call_t ir_instr_invoke_t;

// it is unclear exactly what this does or if it is even necessary
// the actual exception switching code is handled via BR on the type of exception
// not some language intrinsic
typedef struct ir_instr_landingpad_clause {
  unsigned int is_catch;
  unsigned int clause;
} ir_instr_landingpad_clause_t;

typedef struct ir_instr_landingpad {
  ir_instr_t super;
  unsigned int type;

  // unclear what this does
  unsigned int personality;

  // unclear what this does
  unsigned int is_clean_up;
  int num_clauses;
  ir_instr_landingpad_clause_t clauses[0];
} ir_instr_landingpad_t;

/**
 *
 */
typedef struct ir_instr_jsr {
  ir_instr_t super;
  int callee;
  int registers;
} ir_instr_jsr_t;


/**
 *
 */
typedef struct ir_instr_path {
  uint64_t v64;
  int block;
} ir_instr_path_t;

/**
 *
 */
typedef struct ir_instr_switch {
  ir_instr_t super;
  ir_valuetype_t value;
  int defblock;
  int num_paths;
  ir_instr_path_t paths[0];
} ir_instr_switch_t;


/**
 *
 */
typedef struct ir_instr_alloca {
  ir_instr_t super;
  int size;
  ir_valuetype_t num_items_value;
  int alignment;
} ir_instr_alloca_t;


/**
 *
 */
typedef struct ir_instr_select {
  ir_instr_t super;
  ir_valuetype_t  true_value;
  ir_valuetype_t  false_value;
  ir_valuetype_t  pred;
} ir_instr_select_t;



/**
 *
 */
typedef struct ir_instr_move {
  ir_instr_t super;
  ir_valuetype_t  value;
} ir_instr_move_t;


typedef struct ir_instr_stackcopy {
  ir_instr_t super;
  ir_valuetype_t value;
  int size;
} ir_instr_stackcopy_t;


typedef struct ir_instr_stackshrink {
  ir_instr_t super;
  int size;
} ir_instr_stackshrink_t;

/**
 *
 */
typedef struct ir_instr_cmp_branch {
  ir_instr_t super;
  int op;
  ir_valuetype_t lhs_value;
  ir_valuetype_t rhs_value;
  int true_branch;
  int false_branch;
} ir_instr_cmp_branch_t;


/**
 *
 */
typedef struct ir_instr_cmp_select {
  ir_instr_t super;
  int op;
  ir_valuetype_t lhs_value;
  ir_valuetype_t rhs_value;
  ir_valuetype_t true_value;
  ir_valuetype_t false_value;
} ir_instr_cmp_select_t;


typedef struct ir_instr_extractval {
  ir_instr_t super;
  ir_valuetype_t  value;
  int num_indicies;
  int indicies[0];
} ir_instr_extractval_t;


#define MAX_RESUME_VALUES 8
typedef struct ir_instr_resume {
  ir_instr_t super;

  int num_values;
  ir_valuetype_t values[0];
} ir_instr_resume_t;

/**
 *
 */
static void *
instr_create(size_t size, instr_class_t ic)
{
  ir_instr_t *ii = calloc(1, size);
  LIST_INIT(&ii->ii_values);
  ii->ii_class = ic;
  ii->ii_ret.value = -1;
  ii->ii_ret.type = -1;
  return ii;
}


/**
 *
 */
static void *
instr_add(ir_bb_t *ib, size_t size, instr_class_t ic)
{
  ir_instr_t *ii = instr_create(size, ic);
  ii->ii_bb = ib;
  TAILQ_INSERT_TAIL(&ib->ib_instrs, ii, ii_link);
  return ii;
}


/**
 *
 */
static void *
instr_add_before(size_t size, instr_class_t ic, ir_instr_t *before)
{
  ir_bb_t *ib = before->ii_bb;
  ir_instr_t *ii = instr_create(size, ic);
  ii->ii_bb = ib;
  TAILQ_INSERT_BEFORE(before, ii, ii_link);
  return ii;
}


/**
 *
 */
static void *
instr_add_after(size_t size, instr_class_t ic, ir_instr_t *after)
{
  ir_bb_t *ib = after->ii_bb;
  ir_instr_t *ii = instr_create(size, ic);
  ii->ii_bb = ib;
  TAILQ_INSERT_AFTER(&ib->ib_instrs,  after, ii, ii_link);
  return ii;
}


/**
 *
 */
static void
instr_destroy(ir_instr_t *ii)
{
  instr_bind_clear(ii);
  free(ii->ii_rets);
  free(ii->ii_succ);
  free(ii->ii_liveness);

  TAILQ_REMOVE(&ii->ii_bb->ib_instrs, ii, ii_link);
  free(ii);
}


/**
 *
 */
static void
parse_ret(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_unary_t *i = instr_add(ib, sizeof(ir_instr_unary_t), IR_IC_RET);

  if(argc == 0) {
    i->value.value = -1;
  } else {
    i->value = instr_get_vtp(iu, &argc, &argv);
  }
}


/**
 *
 */
static void
parse_unreachable(ir_unit_t *iu)
{
  ir_bb_t *ib = iu->iu_current_bb;

  instr_add(ib, sizeof(ir_instr_t), IR_IC_UNREACHABLE);
}


/**
 *
 */
static void
parse_binop(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_binary_t *i = instr_add(ib, sizeof(ir_instr_binary_t), IR_IC_BINOP);
  i->lhs_value = instr_get_vtp(iu, &argc, &argv);
  i->rhs_value = instr_get_value(iu, &argc, &argv, i->lhs_value.type);
  i->op        = instr_get_uint(iu, &argc, &argv);

  value_alloc_instr_ret(iu, i->lhs_value.type, &i->super);
}


/**
 *
 */
static void
parse_cast(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_unary_t *i = instr_add(ib, sizeof(ir_instr_unary_t), IR_IC_CAST);
  i->value = instr_get_vtp(iu, &argc, &argv);
  int type = instr_get_uint(iu, &argc, &argv);
  i->op    = instr_get_uint(iu, &argc, &argv);
  value_alloc_instr_ret(iu, type, &i->super);
}


/**
 *
 */
static void
parse_load(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_load_t *i = instr_add(ib, sizeof(ir_instr_load_t), IR_IC_LOAD);
  i->immediate_offset = 0;
  i->ptr = instr_get_vtp(iu, &argc, &argv);
  i->value_offset.value = -1;
  i->value_offset_multiply = 0;
  i->cast = -1;
  if(argc == 3) {
    // Explicit type
    value_alloc_instr_ret(iu, argv[0].i64, &i->super);
  } else {
    value_alloc_instr_ret(iu, type_get_pointee(iu, i->ptr.type), &i->super);
  }
}


/**
 *
 */
static void
parse_store(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv,
            int old)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_store_t *i = instr_add(ib, sizeof(ir_instr_store_t), IR_IC_STORE);
  i->immediate_offset = 0;
  i->ptr   = instr_get_vtp(iu, &argc, &argv);
  if(old)
    i->value = instr_get_value(iu, &argc, &argv,
                               type_get_pointee(iu, i->ptr.type));
  else
    i->value = instr_get_vtp(iu, &argc, &argv);

}

static void
parse_insertval(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_valuetype_t src = instr_get_vtp(iu, &argc, &argv);
  ir_valuetype_t replacement = instr_get_vtp(iu, &argc, &argv);

  int num_indices = argc;

  ir_instr_insertval_t *i =
    instr_add(ib, sizeof(ir_instr_insertval_t) +
              sizeof(int) * num_indices, IR_IC_INSERTVAL);

  i->src = src;
  i->replacement = replacement;
  i->num_indicies = num_indices;

  for(int j = 0; j < num_indices; j++) {
    i->indicies[j] = instr_get_uint(iu, &argc, &argv);
  }

  value_alloc_instr_ret(iu, i->src.type, &i->super);
}



/**
 *
 */
static void
parse_gep(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv, int op)
{
  ir_bb_t *ib = iu->iu_current_bb;

  if(op == FUNC_CODE_INST_GEP) {
    argv+=2;
    argc-=2;
  }

  ir_valuetype_t baseptr = instr_get_vtp(iu, &argc, &argv);

  ir_valuetype_t *values = alloca(argc * sizeof(ir_valuetype_t));

  int num_indicies = 0;
  while(argc > 0)
    values[num_indicies++] = instr_get_vtp(iu, &argc, &argv);

  ir_instr_gep_t *i = instr_add(ib,
                                sizeof(ir_instr_gep_t) +
                                sizeof(ir_gep_index_t) *
                                num_indicies, IR_IC_GEP);

  i->num_indicies = num_indicies;
  i->baseptr = baseptr;
  int current_type_index = baseptr.type;

  for(int n = 0; n < num_indicies; n++) {
    i->indicies[n].value = values[n];
    i->indicies[n].type = current_type_index;
    ir_value_t *index_value = value_get(iu, values[n].value);
    int element;
    int inner_type_index;
    ir_type_t *ct = type_get(iu, current_type_index);

    switch(ct->it_code) {
    case IR_TYPE_POINTER:
      inner_type_index = ct->it_pointer.pointee;
      break;

    case IR_TYPE_STRUCT:
      switch(index_value->iv_class) {
      case IR_VC_CONSTANT:
        element = value_get_const32(iu, index_value);
        if(element >= ct->it_struct.num_elements)
          parser_error(iu, "Bad index %d into struct %s",
                       element, type_str_index(iu, current_type_index));
        inner_type_index = ct->it_struct.elements[element].type;
        break;
      default:
        parser_error(iu, "Bad value class %d for struct index",
                     index_value->iv_class);
      }
      break;

    case IR_TYPE_ARRAY:
      inner_type_index = ct->it_array.element_type;
      break;

    default:
      parser_error(iu, "gep unable to index %s",
                   type_str_index(iu, current_type_index));
    }
    current_type_index = inner_type_index;
  }

  int gep_return_type = type_make_pointer(iu, current_type_index, 1);
  value_alloc_instr_ret(iu, gep_return_type, &i->super);
}


/**
 *
 */
static void
parse_cmp2(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_binary_t *i = instr_add(ib, sizeof(ir_instr_binary_t), IR_IC_CMP2);
  i->lhs_value = instr_get_vtp(iu, &argc, &argv);
  i->rhs_value = instr_get_value(iu, &argc, &argv, i->lhs_value.type);
  i->op    = instr_get_uint(iu, &argc, &argv);
  value_alloc_instr_ret(iu, type_find_by_code(iu, IR_TYPE_INT1), &i->super);
}


/**
 *
 */
static void
parse_br(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_br_t *i = instr_add(ib, sizeof(ir_instr_br_t), IR_IC_BR);

  i->true_branch = instr_get_uint(iu, &argc, &argv);

  if(argc == 0) {
    i->condition.value = -1;
  } else {
    i->false_branch = instr_get_uint(iu, &argc, &argv);
    i->condition = instr_get_value(iu, &argc, &argv,
                                   type_make(iu, IR_TYPE_INT1));
  }
}


/**
 *
 */
static int
phi_sort(const void *A, const void *B)
{
  const ir_phi_node_t *a = (const ir_phi_node_t *)A;
  const ir_phi_node_t *b = (const ir_phi_node_t *)B;
  return a->predecessor - b->predecessor;
}

/**
 *
 */
static void
parse_phi(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  int type = instr_get_uint(iu, &argc, &argv);

  int num_nodes = argc / 2;

  ir_instr_phi_t *i =
    instr_add(ib, sizeof(ir_instr_phi_t) + num_nodes * sizeof(ir_phi_node_t),
              IR_IC_PHI);

  i->num_nodes = num_nodes;

  for(int j = 0; j < num_nodes; j++) {
    i->nodes[j].value       = instr_get_value_signed(iu, &argc, &argv, type);
    i->nodes[j].predecessor = instr_get_uint(iu, &argc, &argv);
  }
  qsort(i->nodes, num_nodes, sizeof(ir_phi_node_t), phi_sort);

  int w = 1;
  for(int j = 1; j < num_nodes; j++) {
    if(i->nodes[j].predecessor == i->nodes[w - 1].predecessor)
      continue;
    i->nodes[w].predecessor = i->nodes[j].predecessor;
    i->nodes[w].value       = i->nodes[j].value;
    w++;
  }
  i->num_nodes = w;

  value_alloc_instr_ret(iu, type, &i->super);
}

/**
 *
 */
static void
parse_call_or_invoke(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv,
                     int ii_class)
{
  // http://llvm.org/docs/LangRef.html#call-instruction
  // http://llvm.org/docs/LangRef.html#invoke-instruction

  ir_bb_t *ib = iu->iu_current_bb;

  unsigned int attribute_set = instr_get_uint(iu, &argc, &argv) - 1;
  int cc            = instr_get_uint(iu, &argc, &argv);



  int normal_dest            = -1;
  int unwind_dest            = -1;

  if(ii_class == IR_IC_INVOKE) {
    normal_dest = instr_get_uint(iu, &argc, &argv);
    unwind_dest = instr_get_uint(iu, &argc, &argv);

    if(cc & 0x2000) {
      argc--;
      argv++;
    }

  } else {
    if(cc & 0x8000) {
      argc--;
      argv++;
    }

  }

  ir_valuetype_t fnidx = instr_get_vtp(iu, &argc, &argv);

  const ir_value_t *fn;
  const ir_type_t *fnty = NULL;

  while(1) {
    fn = value_get(iu, fnidx.value);
    if(fn->iv_class == IR_VC_ALIAS) {
      fnidx.value = fn->iv_reg;
      continue;
    }
    break;
  }

  switch(fn->iv_class) {
  case IR_VC_FUNCTION:
    {
      const ir_function_t *f = fn->iv_func;
      // Some functions that have no effect for us, drop them here
      if(!strcmp(f->if_name, "llvm.lifetime.start") ||
         !strcmp(f->if_name, "llvm.lifetime.end") ||
         !strcmp(f->if_name, "llvm.prefetch") ||
         !strcmp(f->if_name, "llvm.va_end"))
        return;

    }
    fnty = type_get(iu, fn->iv_type);
    break;

  case IR_VC_TEMPORARY:
  case IR_VC_REGFRAME:
    fnty = type_get(iu, type_get_pointee(iu, fn->iv_type));
    break;
  default:
    parser_error(iu, "Funcation call via value '%s' not supported",
                 value_str(iu, fn));
    break;
  }

  if(fnty->it_code != IR_TYPE_FUNCTION)
    parser_error(iu, "Call to non-function type %s",
                 type_str(iu, fnty));

  if(cc & (1 << 14))
    // MustTail
    parser_error(iu, "Can't handle must-tail call to %s",
                 type_str(iu, fnty));

  int function_args = fnty->it_function.num_parameters;

  ir_valuetype_t *args = alloca(argc * sizeof(ir_valuetype_t));
  int n = 0;

  while(argc > 0) {

    if(n >= function_args) {
      // Vararg, so type not know, encoded as valuetypepair
      args[n] = instr_get_vtp(iu, &argc, &argv);
    } else {
      // Just the value
      args[n] = instr_get_value(iu, &argc, &argv,
                                fnty->it_function.parameters[n]);
    }
    n++;
  }
  ir_instr_call_t *i =
    instr_add(ib, sizeof(ir_instr_call_t) +
              sizeof(ir_instr_arg_t) * n, ii_class);

  i->callee = fnidx;
  i->normal_dest = normal_dest;
  i->unwind_dest = unwind_dest;
  i->argc = n;


  for(int j = 0; j < n; j++) {
    i->argv[j].value = args[j];
    i->argv[j].copy_size = 0;
  }

  if(attribute_set < VECTOR_LEN(&iu->iu_attrsets)) {
    const ir_attrset_t *ias = &VECTOR_ITEM(&iu->iu_attrsets, attribute_set);
    for(int k = 0; k < ias->ias_size; k++) {
      const ir_attr_t *ia = ias->ias_list[k];
      if(ia->ia_index == -1) {
        // Function attributes
      } if(ia->ia_index == 0) {
        // Return value attributes
      } else {
        int arg = ia->ia_index - 1;
        if(arg < i->argc) {
          if(ia->ia_flags & (1ULL << ATTR_KIND_BY_VAL)) {
            ir_type_t *ty = type_get(iu, i->argv[arg].value.type);
            if(ty->it_code != IR_TYPE_POINTER) {
              parser_error(iu, "Copy-by-value on non-pointer %s",
                           type_str(iu, ty));
            }
            i->argv[arg].copy_size = type_sizeof(iu, ty->it_pointer.pointee);
          }
        }
      }
    }
  }

  const ir_type_t *rety = type_get(iu, fnty->it_function.return_type);

  if(rety->it_code == IR_TYPE_VOID)
    return;

  value_alloc_instr_ret(iu, fnty->it_function.return_type, &i->super);
}


/**
 *
 */
static void
parse_landingpad(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv,
                 int old)
{
  ir_bb_t *ib = iu->iu_current_bb;

  unsigned int type = instr_get_uint(iu, &argc, &argv);

  if(old) {
    instr_get_vtp(iu, &argc, &argv);
  }
  unsigned int is_clean_up = instr_get_uint(iu, &argc, &argv);
  unsigned int num_clauses = instr_get_uint(iu, &argc, &argv);

  ir_instr_landingpad_t *i =
    instr_add(ib, sizeof(ir_instr_landingpad_t) +
              sizeof(ir_instr_landingpad_clause_t) * num_clauses,
              IR_IC_LANDINGPAD);

  i->type = type;
  //  i->personality = personality;
  i->is_clean_up = is_clean_up;
  i->num_clauses = num_clauses;

  for(int j = 0; j < num_clauses; j++) {
    i->clauses[j].clause = instr_get_uint(iu, &argc, &argv);
    i->clauses[j].is_catch = instr_get_uint(iu, &argc, &argv);
  }

  value_alloc_instr_ret(iu, i->type, &i->super);
}

/**
 *
 */

static void
parse_resume(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_resume_t *i =
    instr_add(ib, sizeof(ir_instr_resume_t) + MAX_RESUME_VALUES * sizeof(ir_valuetype_t), IR_IC_RESUME);

  i->values[0] = instr_get_vtp(iu, &argc, &argv);
  i->num_values = 1;
}


/**
 *
 */
static int
switch_sort64(const void *A, const void *B)
{
  const ir_instr_path_t *a = (const ir_instr_path_t *)A;
  const ir_instr_path_t *b = (const ir_instr_path_t *)B;
  if(a->v64 > b->v64)
    return 1;
  if(a->v64 < b->v64)
    return -1;
  return 0;
}

/**
 *
 */
static void
parse_switch(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  unsigned int typeid = instr_get_uint(iu, &argc, &argv);
  ir_valuetype_t value = instr_get_value(iu, &argc, &argv, typeid);
  unsigned int defblock = instr_get_uint(iu, &argc, &argv);
  int paths = argc / 2;

  ir_instr_switch_t *i =
    instr_add(ib, sizeof(ir_instr_switch_t) +
              sizeof(ir_instr_path_t) * paths, IR_IC_SWITCH);

  i->value = value;
  i->defblock = defblock;
  i->num_paths = paths;

  const int width = type_bitwidth(iu, type_get(iu, typeid));
  const uint64_t mask = width == 64 ? ~1ULL : (1ULL << width) - 1;
  for(int n = 0; n < paths; n++) {
    int val = instr_get_uint(iu, &argc, &argv);
    i->paths[n].block = instr_get_uint(iu, &argc, &argv);
    ir_value_t *iv = value_get(iu, val);

    if(iv->iv_class != IR_VC_CONSTANT)
      parser_error(iu, "Switch on non-constant value");
    if(iv->iv_type != typeid)
      parser_error(iu, "Type mismatch for switch/case values");
    i->paths[n].v64 = value_get_const64(iu, iv) & mask;
  }
  qsort(i->paths, paths, sizeof(ir_instr_path_t), switch_sort64);
}


/**
 *
 */
static void
parse_alloca(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  if(argc != 4)
    parser_error(iu, "Invalid number of args to alloca");

  int flags = argv[3].i64;

  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_alloca_t *i =
    instr_add(ib, sizeof(ir_instr_alloca_t), IR_IC_ALLOCA);

  unsigned int rtype  = argv[0].i64;

  if(flags & (1 << 6)) { // ExplicitType
    i->size = type_sizeof(iu, rtype);
    rtype = type_make_pointer(iu, rtype, 1);
  } else {
    unsigned int pointee = type_get_pointee(iu, rtype);
    i->size = type_sizeof(iu, pointee);
  }

  value_alloc_instr_ret(iu, rtype, &i->super);

  i->alignment = vmir_llvm_alignment(flags & 0x1f, 4);
  i->num_items_value.value = argv[2].i64;
  i->num_items_value.type = argv[1].i64;
}


/**
 *
 */
static void
parse_vselect(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_select_t *i = instr_add(ib, sizeof(ir_instr_select_t), IR_IC_SELECT);
  i->true_value  = instr_get_vtp(iu, &argc, &argv);
  i->false_value = instr_get_value(iu, &argc, &argv, i->true_value.type);
  i->pred        = instr_get_vtp(iu, &argc, &argv);

  value_alloc_instr_ret(iu, i->true_value.type, &i->super);
}


/**
 *
 */
static void
parse_vaarg(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_instr_unary_t *i = instr_add(ib, sizeof(ir_instr_unary_t), IR_IC_VAARG);
  int type = argv[0].i64;
  argc--;
  argv++;
  i->value = instr_get_value(iu, &argc, &argv, type);
  int rtype = instr_get_uint(iu,  &argc, &argv);
  value_alloc_instr_ret(iu, rtype, &i->super);
}

/**
 *
 */
static void
parse_extractval(ir_unit_t *iu, unsigned int argc, const ir_arg_t *argv)
{
  ir_bb_t *ib = iu->iu_current_bb;

  ir_valuetype_t base = instr_get_vtp(iu, &argc, &argv);
  const int num_indicies = argc;
  int current_type_index = base.type;

  ir_instr_extractval_t *ii = instr_add(ib,
                                        sizeof(ir_instr_extractval_t) +
                                        sizeof(int) * num_indicies,
                                        IR_IC_EXTRACTVAL);
  ii->num_indicies = num_indicies;
  ii->value = base;

  for(int i = 0; i < num_indicies; i++) {
    ir_type_t *ty = type_get(iu, current_type_index);
    int idx = argv[i].i64;
    ii->indicies[i] = idx;
    switch(ty->it_code) {
    default:
      parser_error(iu, "Bad type %s into struct in extractval", type_str(iu, ty));

    case IR_TYPE_STRUCT:
      if(idx >= ty->it_struct.num_elements)
        parser_error(iu, "Bad index %d into struct in extractval", idx);
      current_type_index = ty->it_struct.elements[idx].type;
      break;
    case IR_TYPE_ARRAY:
      current_type_index = ty->it_array.element_type;
      break;
    }
  }

  value_alloc_instr_ret(iu, current_type_index, &ii->super);
}

/**
 *
 */
static void
function_rec_handler(ir_unit_t *iu, int op,
                     unsigned int argc, const ir_arg_t *argv)
{
  ir_function_t *f = iu->iu_current_function;

  switch(op) {
  case FUNC_CODE_DECLAREBLOCKS:

    if(TAILQ_FIRST(&f->if_bbs) != NULL)
      parser_error(iu, "Multiple BB decl in function");

    unsigned int numbbs = argv[0].i64;
    if(numbbs == 0)
      parser_error(iu, "Declareblocks: Zero basic blocks");
    if(numbbs > 65535)
      parser_error(iu, "Declareblocks: Too many basic blocks: %d", numbbs);

    for(int i = 0; i < numbbs; i++)
      bb_add(f, NULL);

    iu->iu_current_bb = TAILQ_FIRST(&f->if_bbs);
    return;

  case FUNC_CODE_INST_RET:
    parse_ret(iu, argc, argv);
    iu->iu_current_bb = TAILQ_NEXT(iu->iu_current_bb, ib_link);
    return;

  case FUNC_CODE_INST_BINOP:
    return parse_binop(iu, argc, argv);

  case FUNC_CODE_INST_CAST:
    return parse_cast(iu, argc, argv);

  case FUNC_CODE_INST_LOAD:
  case FUNC_CODE_INST_LOADATOMIC:
    return parse_load(iu, argc, argv);

  case FUNC_CODE_INST_STORE_OLD:
  case FUNC_CODE_INST_STOREATOMIC_OLD:
    return parse_store(iu, argc, argv, 1);

  case FUNC_CODE_INST_STORE:
  case FUNC_CODE_INST_STOREATOMIC:
    return parse_store(iu, argc, argv, 0);

  case FUNC_CODE_INST_INBOUNDS_GEP_OLD:
  case FUNC_CODE_INST_GEP_OLD:
  case FUNC_CODE_INST_GEP:
    return parse_gep(iu, argc, argv, op);

  case FUNC_CODE_INST_CMP2:
    return parse_cmp2(iu, argc, argv);

  case FUNC_CODE_INST_BR:
    parse_br(iu, argc, argv);
    iu->iu_current_bb = TAILQ_NEXT(iu->iu_current_bb, ib_link);
    break;

  case FUNC_CODE_INST_PHI:
    return parse_phi(iu, argc, argv);

  case FUNC_CODE_INST_INVOKE:
    parse_call_or_invoke(iu, argc, argv, IR_IC_INVOKE);
    iu->iu_current_bb = TAILQ_NEXT(iu->iu_current_bb, ib_link);
    break;

  case FUNC_CODE_INST_CALL:
    parse_call_or_invoke(iu, argc, argv, IR_IC_CALL);
    break;

  case FUNC_CODE_INST_SWITCH:
    parse_switch(iu, argc, argv);
    iu->iu_current_bb = TAILQ_NEXT(iu->iu_current_bb, ib_link);
    break;

  case FUNC_CODE_INST_ALLOCA:
    parse_alloca(iu, argc, argv);
    break;

  case FUNC_CODE_INST_UNREACHABLE:
    parse_unreachable(iu);
    iu->iu_current_bb = TAILQ_NEXT(iu->iu_current_bb, ib_link);
    break;

  case FUNC_CODE_INST_VSELECT:
    parse_vselect(iu, argc, argv);
    break;

  case FUNC_CODE_INST_VAARG:
    parse_vaarg(iu, argc, argv);
    break;

  case FUNC_CODE_INST_EXTRACTVAL:
    parse_extractval(iu, argc, argv);
    break;

  case FUNC_CODE_INST_LANDINGPAD_OLD:
    parse_landingpad(iu, argc, argv, 1);
    break;

  case FUNC_CODE_INST_LANDINGPAD:
    parse_landingpad(iu, argc, argv, 0);
    break;

  case FUNC_CODE_INST_INSERTVAL:
    parse_insertval(iu, argc, argv);
    break;

  case FUNC_CODE_INST_RESUME:
    parse_resume(iu, argc, argv);
    iu->iu_current_bb = TAILQ_NEXT(iu->iu_current_bb, ib_link);
    break;

  default:
    printargs(argv, argc);
    parser_error(iu, "Can't handle functioncode %d", op);
  }
}



static int
instr_print(char **dstp, ir_unit_t *iu, const ir_instr_t *ii, int flags)
{
  int len = 0;
  len += addstr(dstp, ii->ii_jit ? "J" : " ");
  if(ii->ii_ret.value < -1) {
    int num_values = -ii->ii_ret.value;
    len += addstr(dstp, "{ ");
    for(int i = 0; i < num_values; i++) {
      if(i)
        len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, ii->ii_rets[i]);
    }
    len += addstr(dstp, " } = ");
  } else if(ii->ii_ret.value != -1) {
    len += value_print_vt(dstp, iu, ii->ii_ret);
    len += addstr(dstp, " = ");
  }

  switch(ii->ii_class) {

  case IR_IC_UNREACHABLE:
    len += addstr(dstp, "unreachable");
    break;

  case IR_IC_RET:
    {
      ir_instr_unary_t *u = (ir_instr_unary_t *)ii;
      len += addstr(dstp, "ret ");
      if(u->value.value != -1)
        len += value_print_vt(dstp, iu, u->value);
    }
    break;

  case IR_IC_BINOP:
    {
      ir_instr_binary_t *b = (ir_instr_binary_t *)ii;
      const char *op = "???";
      switch(b->op) {
      case BINOP_ADD:        op = "add"; break;
      case BINOP_SUB:        op = "sub"; break;
      case BINOP_MUL:        op = "mul"; break;
      case BINOP_UDIV:       op = "udiv"; break;
      case BINOP_SDIV:       op = "sdiv"; break;
      case BINOP_UREM:       op = "urem"; break;
      case BINOP_SREM:       op = "srem"; break;
      case BINOP_SHL:        op = "shl"; break;
      case BINOP_LSHR:       op = "lshr"; break;
      case BINOP_ASHR:       op = "ashr"; break;
      case BINOP_AND:        op = "and"; break;
      case BINOP_OR:         op = "or"; break;
      case BINOP_XOR:        op = "xor"; break;
      case BINOP_ROL:        op = "rol"; break;
      case BINOP_ROR:        op = "ror"; break;
      }
      len += addstr(dstp, op);
      len += addstr(dstp, " ");
      len += value_print_vt(dstp, iu, b->lhs_value);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, b->rhs_value);
    }
    break;
  case IR_IC_CAST:
    {
      ir_instr_unary_t *u = (ir_instr_unary_t *)ii;
      const char *op = "???";
      switch(u->op) {
      case CAST_TRUNC:    op = "trunc"; break;
      case CAST_ZEXT:     op = "zext"; break;
      case CAST_SEXT:     op = "sext"; break;
      case CAST_FPTOUI:   op = "fptoui"; break;
      case CAST_FPTOSI:   op = "fptosi"; break;
      case CAST_UITOFP:   op = "uitofp"; break;
      case CAST_SITOFP:   op = "sitofp"; break;
      case CAST_FPTRUNC:  op = "fptrunc"; break;
      case CAST_FPEXT:    op = "fpext"; break;
      case CAST_PTRTOINT: op = "ptrtoint"; break;
      case CAST_INTTOPTR: op = "inttoptr"; break;
      case CAST_BITCAST:  op = "bitcast"; break;
      }
      len += addstr(dstp, op);
      len += addstr(dstp, " ");
      len += value_print_vt(dstp, iu, u->value);
    }
    break;

  case IR_IC_LOAD:
    {
      ir_instr_load_t *u = (ir_instr_load_t *)ii;
      len += addstr(dstp, "load");
      switch(u->cast) {
      case CAST_ZEXT:  len += addstr(dstp, ".zext");  break;
      case CAST_SEXT:  len += addstr(dstp, ".sext");  break;
      }
      len += addstr(dstp, " ");
      len += value_print_vt(dstp, iu, u->ptr);
      if(u->immediate_offset) {
        len += addstrf(dstp, " + #%x", u->immediate_offset);
      }
      if(u->value_offset.value >= 0) {
        len += addstr(dstp, " + ");
        len += value_print_vt(dstp, iu, u->value_offset);
        if(u->value_offset_multiply)
          len += addstrf(dstp, " * #0x%x", u->value_offset_multiply);
      }
    }
    break;

  case IR_IC_STORE:
    {
      ir_instr_store_t *s = (ir_instr_store_t *)ii;
      len += addstr(dstp, "store ");
      len += value_print_vt(dstp, iu, s->ptr);
      if(s->immediate_offset)
        len += addstrf(dstp, " + #%x", s->immediate_offset);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, s->value);
    }
    break;

  case IR_IC_GEP:
    {
      ir_instr_gep_t *g = (ir_instr_gep_t *)ii;
      len += addstr(dstp, "gep ");
      if(g->baseptr.value != -1)
        len += value_print_vt(dstp, iu, g->baseptr);

      for(int i = 0; i < g->num_indicies; i++) {
        len += addstr(dstp, " + ");
        len += type_print_id(dstp, iu, g->indicies[i].type);
        len += addstr(dstp, "[");
        len += value_print_vt(dstp, iu, g->indicies[i].value);
        len += addstr(dstp, "]");
      }
    }
    break;
  case IR_IC_CMP2:
    {
      ir_instr_binary_t *b = (ir_instr_binary_t *)ii;
      const char *op = "???";
      switch(b->op) {
      case FCMP_FALSE: op = "fcmp_false"; break;
      case FCMP_OEQ: op = "fcmp_oeq"; break;
      case FCMP_OGT: op = "fcmp_ogt"; break;
      case FCMP_OGE: op = "fcmp_oge"; break;
      case FCMP_OLT: op = "fcmp_olt"; break;
      case FCMP_OLE: op = "fcmp_ole"; break;
      case FCMP_ONE: op = "fcmp_one"; break;
      case FCMP_ORD: op = "fcmp_ord"; break;
      case FCMP_UNO: op = "fcmp_uno"; break;
      case FCMP_UEQ: op = "fcmp_ueq"; break;
      case FCMP_UGT: op = "fcmp_ugt"; break;
      case FCMP_UGE: op = "fcmp_uge"; break;
      case FCMP_ULT: op = "fcmp_ult"; break;
      case FCMP_ULE: op = "fcmp_ule"; break;
      case FCMP_UNE: op = "fcmp_une"; break;
      case FCMP_TRUE: op = "fcmp_true"; break;
      case ICMP_EQ: op = "icmp_eq"; break;
      case ICMP_NE: op = "icmp_ne"; break;
      case ICMP_UGT: op = "icmp_ugt"; break;
      case ICMP_UGE: op = "icmp_uge"; break;
      case ICMP_ULT: op = "icmp_ult"; break;
      case ICMP_ULE: op = "icmp_ule"; break;
      case ICMP_SGT: op = "icmp_sgt"; break;
      case ICMP_SGE: op = "icmp_sge"; break;
      case ICMP_SLT: op = "icmp_slt"; break;
      case ICMP_SLE: op = "icmp_sle"; break;
      }
      len += addstr(dstp, op);
      len += addstr(dstp, " ");
      len += value_print_vt(dstp, iu, b->lhs_value);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, b->rhs_value);
    }
    break;

  case IR_IC_CMP_BRANCH:
    {
      ir_instr_cmp_branch_t *icb = (ir_instr_cmp_branch_t *)ii;
      const char *op = "???";
      switch(icb->op) {
      case ICMP_EQ: op = "eq"; break;
      case ICMP_NE: op = "ne"; break;
      case ICMP_UGT: op = "ugt"; break;
      case ICMP_UGE: op = "uge"; break;
      case ICMP_ULT: op = "ult"; break;
      case ICMP_ULE: op = "ule"; break;
      case ICMP_SGT: op = "sgt"; break;
      case ICMP_SGE: op = "sge"; break;
      case ICMP_SLT: op = "slt"; break;
      case ICMP_SLE: op = "sle"; break;
      }

      len += addstr(dstp, "cmpbr.");
      len += addstr(dstp, op);
      len += addstr(dstp, " ");
      len += value_print_vt(dstp, iu, icb->lhs_value);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, icb->rhs_value);

      len += addstrf(dstp, " true:.%d false:.%d",
                     icb->true_branch, icb->false_branch);
    }
    break;

  case IR_IC_CMP_SELECT:
    {
      ir_instr_cmp_select_t *ics = (ir_instr_cmp_select_t *)ii;
      const char *op = "???";
      switch(ics->op) {
      case ICMP_EQ: op = "eq"; break;
      case ICMP_NE: op = "ne"; break;
      case ICMP_UGT: op = "ugt"; break;
      case ICMP_UGE: op = "uge"; break;
      case ICMP_ULT: op = "ult"; break;
      case ICMP_ULE: op = "ule"; break;
      case ICMP_SGT: op = "sgt"; break;
      case ICMP_SGE: op = "sge"; break;
      case ICMP_SLT: op = "slt"; break;
      case ICMP_SLE: op = "sle"; break;
      }

      len += addstr(dstp, "cmpselect.");
      len += addstr(dstp, op);
      len += addstr(dstp, " ");
      len += value_print_vt(dstp, iu, ics->lhs_value);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, ics->rhs_value);
      len += addstr(dstp, " true:");
      len += value_print_vt(dstp, iu, ics->true_value);
      len += addstr(dstp, " false:");
      len += value_print_vt(dstp, iu, ics->false_value);
    }
    break;

  case IR_IC_BR:
    {
      ir_instr_br_t *br = (ir_instr_br_t *)ii;
      if(br->condition.value == -1) {
        len += addstrf(dstp, "b .%d", br->true_branch);
      } else {
        len += addstr(dstp, "bcond ");
        len += value_print_vt(dstp, iu, br->condition);
        len += addstrf(dstp, ", true:.%d, false:.%d",
                       br->true_branch, br->false_branch);
      }
    }
    break;
  case IR_IC_PHI:
    {
      ir_instr_phi_t *p = (ir_instr_phi_t *)ii;
      len += addstr(dstp, "phi ");
      for(int i = 0; i < p->num_nodes; i++) {
        len += addstrf(dstp, " [.%d ", p->nodes[i].predecessor);
        len += value_print_vt(dstp, iu, p->nodes[i].value);
        len += addstr(dstp, "]");
      }
    }
    break;
  case IR_IC_INVOKE:
  case IR_IC_CALL:
  case IR_IC_VMOP:
    {
      ir_instr_call_t *p = (ir_instr_call_t *)ii;
      ir_function_t *f = value_function(iu, p->callee.value);

      if(ii->ii_class == IR_IC_INVOKE) {
        len += addstrf(dstp, "invoke normal:.%d unwind:.%d ",
                       p->normal_dest, p->unwind_dest);
      } else {

        len += addstr(dstp, ii->ii_class == IR_IC_CALL ? "call" : "vmop");
      }

      if(f != NULL) {
        len += addstr(dstp, " ");
        len += addstr(dstp, f->if_name ?: "<anon>");
      } else {
        len += addstr(dstp, " fptr in ");
        len += value_print_vt(dstp, iu, p->callee);
      }
      len += addstr(dstp, " (");
      len += type_print_id(dstp, iu, p->callee.type);
      len += addstr(dstp, ") (");
      for(int i = 0; i < p->argc; i++) {
        if(i)
          len += addstr(dstp, ", ");
        len += value_print_vt(dstp, iu, p->argv[i].value);
        if(p->argv[i].copy_size)
          len += addstrf(dstp, " (byval %d bytes)", p->argv[i].copy_size);
      }
      len += addstr(dstp, ")");
    }
    break;

  case IR_IC_SWITCH:
    {
      ir_instr_switch_t *s = (ir_instr_switch_t *)ii;
      len += addstr(dstp, "switch");
      len += value_print_vt(dstp, iu, s->value);
      for(int i = 0; i < s->num_paths; i++)
        len += addstrf(dstp, " [#%"PRId64" -> .%d]",
                       s->paths[i].v64, s->paths[i].block);
      len += addstrf(dstp, " default: .%d", s->defblock);
    }
    break;
  case IR_IC_ALLOCA:
    {
      ir_instr_alloca_t *a = (ir_instr_alloca_t *)ii;
      len += addstrf(dstp, "alloca [%d * ", a->size);
      len += value_print_vt(dstp, iu, a->num_items_value);
      len += addstrf(dstp, " align: %d", a->alignment);
    }
    break;
  case IR_IC_SELECT:
    {
      ir_instr_select_t *s = (ir_instr_select_t *)ii;
      len += addstr(dstp, "select ");
      len += value_print_vt(dstp, iu, s->pred);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, s->true_value);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, s->false_value);
    }
    break;
  case IR_IC_VAARG:
    {
      ir_instr_unary_t *m = (ir_instr_unary_t *)ii;
      len += addstr(dstp, "vaarg ");
      len += value_print_vt(dstp, iu, m->value);
    }
    break;
  case IR_IC_EXTRACTVAL:
    {
      ir_instr_extractval_t *jj = (ir_instr_extractval_t *)ii;
      len += addstr(dstp, "extractval ");
      len += value_print_vt(dstp, iu, jj->value);
      for(int i = 0; i < jj->num_indicies; i++)
        len += addstrf(dstp, ":%d", jj->indicies[i]);

      len += addstr(dstp, "]");
    }
    break;
  case IR_IC_INSERTVAL:
    {
      ir_instr_insertval_t *jj = (ir_instr_insertval_t *)ii;
      len += addstr(dstp, "insertval ");
      len += value_print_vt(dstp, iu, jj->src);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, jj->replacement);
      len += addstr(dstp, " [");
      for(int i = 0; i < jj->num_indicies; i++)
        len += addstrf(dstp, ":%d", jj->indicies[i]);
      len += addstr(dstp, "]");
    }
    break;
  case IR_IC_LANDINGPAD:
    {
      len += addstr(dstp, "landingpad");
    }
    break;
  case IR_IC_RESUME:
    {
      ir_instr_resume_t *r = (ir_instr_resume_t *)ii;
      len += addstr(dstp, "resume ");
      for(int i = 0; i < r->num_values; i++) {
        if(i)
          len += addstr(dstp, ", ");
        len += value_print_vt(dstp, iu, r->values[i]);
      }
    }
    break;
  case IR_IC_LEA:
    {
      ir_instr_lea_t *l = (ir_instr_lea_t *)ii;
      len += addstr(dstp, "lea ");
      len += value_print_vt(dstp, iu, l->baseptr);
      if(l->immediate_offset)
        len += addstrf(dstp, " + #0x%x", l->immediate_offset);

      if(l->value_offset.value >= 0) {
        len += addstr(dstp, " + ");
        len += value_print_vt(dstp, iu, l->value_offset);
        if(l->value_offset_multiply)
          len += addstrf(dstp, " * #0x%x", l->value_offset_multiply);
      }
    }
    break;
  case IR_IC_MOVE:
    {
      ir_instr_move_t *m = (ir_instr_move_t *)ii;
      len += addstr(dstp, "move ");
      len += value_print_vt(dstp, iu, m->value);
    }
    break;
  case IR_IC_STACKCOPY:
    {
      ir_instr_stackcopy_t *sc = (ir_instr_stackcopy_t *)ii;
      len += addstr(dstp, "stackcopy ");
      len += value_print_vt(dstp, iu, sc->value);
      len += addstrf(dstp, "size:#0x%x", sc->size);
    }
    break;
  case IR_IC_STACKSHRINK:
    {
      ir_instr_stackshrink_t *ss = (ir_instr_stackshrink_t *)ii;
      len += addstrf(dstp, "stackshrink #0x%x", ss->size);
    }
    break;
  case IR_IC_MLA:
    {
      ir_instr_ternary_t *mla = (ir_instr_ternary_t *)ii;
      len += addstr(dstp, "mla ");
      len += value_print_vt(dstp, iu, mla->arg1);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, mla->arg2);
      len += addstr(dstp, ", ");
      len += value_print_vt(dstp, iu, mla->arg3);
    }
    break;
  }

  if(flags & 1) {
    const ir_value_instr_t *ivi;
    len += addstr(dstp, " <");
    LIST_FOREACH(ivi, &ii->ii_values, ivi_instr_link) {
      len += addstrf(dstp, "%s= ",
                     ivi->ivi_relation == IVI_INPUT ? "input" :
                     ivi->ivi_relation == IVI_OUTPUT ? "output" : "???");
      len += value_print(dstp, iu, ivi->ivi_value, NULL);
      len += addstr(dstp, " ");
    }
    len += addstr(dstp, ">");
  }
  return len;
}


/**
 *
 */
static const char *
instr_str(ir_unit_t *iu, const ir_instr_t *ii, int flags)
{
  int len = instr_print(NULL, iu, ii, flags);
  char *dst = tmpstr(iu, len);
  const char *ret = dst;
  instr_print(&dst, iu, ii, flags);
  return ret;
}


/**
 *
 */
__attribute__((unused)) static char *
instr_stra(ir_unit_t *iu, const ir_instr_t *ii, int flags)
{
  int len = instr_print(NULL, iu, ii, flags);
  char *dst = malloc(len + 1);
  char *ret = dst;
  dst[len] = 0;
  instr_print(&dst, iu, ii, flags);
  return ret;
}


__attribute__((unused)) static int
invert_pred(int pred)
{
  switch(pred) {
  default:
    abort();
  case ICMP_EQ: return ICMP_NE;
  case ICMP_NE: return ICMP_EQ;
  case ICMP_UGT: return ICMP_ULE;
  case ICMP_ULT: return ICMP_UGE;
  case ICMP_UGE: return ICMP_ULT;
  case ICMP_ULE: return ICMP_UGT;
  case ICMP_SGT: return ICMP_SLE;
  case ICMP_SLT: return ICMP_SGE;
  case ICMP_SGE: return ICMP_SLT;
  case ICMP_SLE: return ICMP_SGT;
  case FCMP_OEQ: return FCMP_UNE;
  case FCMP_ONE: return FCMP_UEQ;
  case FCMP_OGT: return FCMP_ULE;
  case FCMP_OLT: return FCMP_UGE;
  case FCMP_OGE: return FCMP_ULT;
  case FCMP_OLE: return FCMP_UGT;
  case FCMP_UEQ: return FCMP_ONE;
  case FCMP_UNE: return FCMP_OEQ;
  case FCMP_UGT: return FCMP_OLE;
  case FCMP_ULT: return FCMP_OGE;
  case FCMP_UGE: return FCMP_OLT;
  case FCMP_ULE: return FCMP_OGT;
  case FCMP_ORD: return FCMP_UNO;
  case FCMP_UNO: return FCMP_ORD;
  case FCMP_TRUE: return FCMP_FALSE;
  case FCMP_FALSE: return FCMP_TRUE;
  }
}

static int
swap_pred(int pred)
{
  switch(pred) {
  default:
    abort();
  case ICMP_EQ: case ICMP_NE:
    return pred;
  case ICMP_SGT: return ICMP_SLT;
    case ICMP_SLT: return ICMP_SGT;
    case ICMP_SGE: return ICMP_SLE;
    case ICMP_SLE: return ICMP_SGE;
    case ICMP_UGT: return ICMP_ULT;
    case ICMP_ULT: return ICMP_UGT;
    case ICMP_UGE: return ICMP_ULE;
    case ICMP_ULE: return ICMP_UGE;
    case FCMP_FALSE: case FCMP_TRUE:
    case FCMP_OEQ: case FCMP_ONE:
    case FCMP_UEQ: case FCMP_UNE:
    case FCMP_ORD: case FCMP_UNO:
      return pred;
    case FCMP_OGT: return FCMP_OLT;
    case FCMP_OLT: return FCMP_OGT;
    case FCMP_OGE: return FCMP_OLE;
    case FCMP_OLE: return FCMP_OGE;
    case FCMP_UGT: return FCMP_ULT;
    case FCMP_ULT: return FCMP_UGT;
    case FCMP_UGE: return FCMP_ULE;
    case FCMP_ULE: return FCMP_UGE;
  }
}


static int
instr_have_side_effects(const ir_instr_t *ii)
{
  switch(ii->ii_class) {
  case IR_IC_UNREACHABLE:
  case IR_IC_RET:
  case IR_IC_VAARG:
  case IR_IC_STORE:
  case IR_IC_BR:
  case IR_IC_ALLOCA:
  case IR_IC_CALL:
  case IR_IC_VMOP:
  case IR_IC_INVOKE:
  case IR_IC_RESUME:
  case IR_IC_INSERTVAL:
  case IR_IC_LANDINGPAD:
  case IR_IC_STACKCOPY:
  case IR_IC_STACKSHRINK:
  case IR_IC_CMP_BRANCH:
    return 1;

  case IR_IC_GEP:
  case IR_IC_CAST:
  case IR_IC_LOAD:
  case IR_IC_BINOP:
  case IR_IC_CMP2:
  case IR_IC_SELECT:
  case IR_IC_LEA:
  case IR_IC_SWITCH:
  case IR_IC_PHI:
  case IR_IC_MOVE:
  case IR_IC_EXTRACTVAL:
  case IR_IC_CMP_SELECT:
  case IR_IC_MLA:
    return 0;
  }
  return 1;
}

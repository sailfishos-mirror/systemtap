// parse tree functions
// Copyright (C) 2005-2019 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "parse.h"
#include "staputil.h"
#include "session.h"
#include "stringtable.h"

#include <iostream>
#include <typeinfo>
#include <sstream>
#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstring>
#include <atomic>

using namespace std;

visitable::~visitable ()
{
}


expression::expression ():
  type (pe_unknown), tok (0)
{
}


expression::~expression ()
{
}


statement::statement ():
  tok (0)
{
}


statement::statement (const token* tok):
  tok (tok)
{
}


null_statement::null_statement (const token* tok):
  statement(tok)
{
}


statement::~statement ()
{
}


symbol::symbol ():
  referent (0)
{
}


arrayindex::arrayindex ():
  base (0)
{
}

functioncall::functioncall ()
{
}


symboldecl::symboldecl ():
  tok (0), systemtap_v_conditional (0),
  type (pe_unknown)
{
}


symboldecl::~symboldecl ()
{
}

probe_point::probe_point (std::vector<component*> const & comps):
  components(comps), optional (false), sufficient (false),
  well_formed (false), condition (0)
{
}

// NB: shallow-copy of components & condition!
probe_point::probe_point (const probe_point& pp):
  components(pp.components), optional (pp.optional), sufficient (pp.sufficient),
  well_formed (pp.well_formed), condition (pp.condition), auto_path (pp.auto_path)
{
}


probe_point::probe_point ():
  optional (false), sufficient (false), well_formed (false), condition (0)
{
}

bool
probe_point::from_globby_comp(const std::string& comp)
{
  vector<component*>::const_iterator it;
  for (it = components.begin(); it != components.end(); it++)
    if ((*it)->functor == comp)
      return (*it)->from_glob;
  return false;
}

static atomic<unsigned> last_probeidx(0);

probe::probe ():
  body (0), base (0), tok (0), systemtap_v_conditional (0), privileged (false),
  synthetic (false), id (atomic_fetch_add(&last_probeidx,1U))
{
}


// Copy constructor, but with overriding probe-point.  To be used when
// mapping script-level probe points to another one, early during pass
// 2.  There should be no symbol resolution done yet.
probe::probe(probe* p, probe_point* l):
  locations (1, l), body (deep_copy_visitor::deep_copy (p->body)),
  base (p), tok (p->tok), systemtap_v_conditional (p->systemtap_v_conditional),
  privileged (p->privileged), synthetic (p->synthetic), id (atomic_fetch_add(&last_probeidx,1U))
{
  assert (p->locals.size() == 0);
  assert (p->unused_locals.size() == 0);
}


string
probe::name () const
{
  return string ("probe_") + lex_cast(id);
}


probe_point::component::component ():
  arg (0), from_glob(false), tok(0)
{
}


probe_point::component::component (interned_string f,
  literal * a, bool from_glob):
    functor(f), arg(a), from_glob(from_glob), tok(0)
{
}


vardecl::vardecl ():
  arity_tok(0), arity (-1), maxsize(0), init(NULL), synthetic(false), wrap(false),
  char_ptr_arg(false)
{
}


void
vardecl::set_arity (int a, const token* t)
{
  if (a < 0)
    return;

  if (a == 0 && maxsize > 0)
    throw SEMANTIC_ERROR (_("inconsistent arity"), tok);

  if (arity != a && arity >= 0)
    {
      semantic_error err (ERR_SRC, _F("inconsistent arity (%s vs %d)",
                                      lex_cast(arity).c_str(), a), t?:tok);
      if (arity_tok)
        {
          semantic_error chain(ERR_SRC, _F("arity %s first inferred here",
                                           lex_cast(arity).c_str()), arity_tok);
          err.set_chain(chain);
        }
      throw err;
    }

  if (arity != a)
    {
      arity_tok = t;
      arity = a;
      index_types.resize (arity);
      for (int i=0; i<arity; i++)
	index_types[i] = pe_unknown;
    }
}

bool
vardecl::compatible_arity (int a)
{
  if (a == 0 && maxsize > 0)
    return false;
  if (arity == -1 || a == -1)
    return true;
  return arity == a;
}


functiondecl::functiondecl ():
  body (0), synthetic (false), mangle_oldstyle (false), has_next(false), cloned_p(false),
  priority(1)
{
}

void
functiondecl::join (systemtap_session& s)
{
  if (!synthetic)
    throw SEMANTIC_ERROR (_("internal error, joining a non-synthetic function"), tok);
  if (!s.functions.insert (make_pair (name, this)).second)
    throw SEMANTIC_ERROR (_F("synthetic function '%s' conflicts with an existing function",
                             name.to_string().c_str()), tok);
  tok->location.file->functions.push_back (this);
}


literal_number::literal_number (int64_t v, bool hex)
{
  value = v;
  print_hex = hex;
  type = pe_long;
}


literal_string::literal_string (interned_string v)
{
  value = v;
  type = pe_string;
}


ostream&
operator << (ostream& o, const exp_type& e)
{
  switch (e)
    {
    case pe_unknown: o << "unknown"; break;
    case pe_long: o << "long"; break;
    case pe_string: o << "string"; break;
    case pe_stats: o << "stats"; break;
    default: o << "???"; break;
    }
  return o;
}


void
target_symbol::assert_no_components(const std::string& tapset, bool pretty_ok)
{
  if (components.empty())
    return;

  const string& name = this->name;
  switch (components[0].type)
    {
    case comp_literal_array_index:
    case comp_expression_array_index:
      throw SEMANTIC_ERROR(_F("%s variable '%s' may not be used as array",
                              tapset.c_str(), name.c_str()), components[0].tok);
    case comp_struct_member:
      throw SEMANTIC_ERROR(_F("%s variable '%s' may not be used as a structure",
                              tapset.c_str(), name.c_str()), components[0].tok);
    case comp_pretty_print:
      if (!pretty_ok)
        throw SEMANTIC_ERROR(_F("%s variable '%s' may not be pretty-printed",
                                tapset.c_str(), name.c_str()), components[0].tok);
      return;
    default:
      throw SEMANTIC_ERROR (_F("invalid use of %s variable '%s'",
                            tapset.c_str(), name.c_str()), components[0].tok);
    }
}


size_t
target_symbol::pretty_print_depth () const
{
  if (! components.empty ())
    {
      const component& last = components.back ();
      if (last.type == comp_pretty_print)
	return last.member.length ();
    }
  return 0;
}


bool
target_symbol::check_pretty_print (bool lvalue) const
{
  if (pretty_print_depth () == 0)
    return false;

  if (lvalue)
    throw SEMANTIC_ERROR(_("cannot write to pretty-printed variable"), tok);

  return true;
}


void target_symbol::chain (const semantic_error &er)
{
  semantic_error* e = new semantic_error(er);
  if (!e->tok1)
    e->tok1 = this->tok;
  assert (e->get_chain() == 0);
  if (this->saved_conversion_error)
    {
      e->set_chain(*this->saved_conversion_error);
      delete this->saved_conversion_error;
    }
  this->saved_conversion_error = e;
}


string target_symbol::sym_name ()
{
  return (string)name.substr(1);
}


string atvar_op::sym_name ()
{
  if (cu_name == "")
    return target_name;
  else
    return (string)target_name.substr(0, target_name.length() - cu_name.length() - 1);
}


// Substring operations like interned_string.find() are repeated a
// surprising number of times (with nested / widely used tapset emb-C
// functions), and need to be efficient.  We memoize the search
// results.
bool memo_tagged_p (const interned_string& haystack, const string& needle)
{
  static map <pair<interned_string,string>,bool> string_find_memoized;
  auto key = make_pair(haystack,needle);
  
  auto it = string_find_memoized.find(key);
  if (it != string_find_memoized.end())
    return it->second;

  auto findres = haystack.find(needle);
  bool res = (findres != interned_string::npos);
  string_find_memoized.insert(make_pair(key,res));
  
  return res;
}



bool
embedded_expr::tagged_p (const char *tag) const
{
  return memo_tagged_p (code, tag);
}


bool
embedded_expr::tagged_p (const string &tag) const
{
  return memo_tagged_p (code, tag);
}


bool
embedded_expr::tagged_p (const interned_string& tag) const
{
  return memo_tagged_p (code, tag);
}


bool
embeddedcode::tagged_p (const char *tag) const
{
  return memo_tagged_p (code, tag);
}


bool
embeddedcode::tagged_p (const string &tag) const
{
  return memo_tagged_p (code, tag);
}


bool
embeddedcode::tagged_p (const interned_string& tag) const
{
  return memo_tagged_p (code, tag);
}


// ------------------------------------------------------------------------
// parse tree printing

ostream& operator << (ostream& o, const expression& k)
{
  k.print (o);
  return o;
}


void literal_string::print (ostream& o) const
{
  o << '"';
  for (unsigned i=0; i<value.size(); i++)
    if (value[i] == '"') // or other escapeworthy characters?
      o << '\\' << '"';
    else
      o << value[i];
  o << '"';
}


void literal_number::print (ostream& o) const
{
  if (print_hex)
    o << hex << showbase;
  o << value;
  if (print_hex)
    o << dec << noshowbase;
}


void embedded_expr::print (ostream& o) const
{
  o << "%{ " << code << " %}";
}


void binary_expression::print (ostream& o) const
{
  o << "(" << *left << ") "
    << op
    << " (" << *right << ")";
}

void regex_query::print (ostream& o) const
{
  // NB: we need a custom printer, because the parser does not accept
  // a parenthesized RHS.
  o << "(" << *left << ") "
    << op
    << " " << *right;
}


void unary_expression::print (ostream& o) const
{
  o << op << '(' << *operand << ")";
}

void array_in::print (ostream& o) const
{
  o << "[";
  for (unsigned i=0; i<operand->indexes.size(); i++)
    {
      if (i > 0) o << ", ";
      if (operand->indexes[i])
        operand->indexes[i]->print (o);
      else
        o << "*";
    }
  o << "] in ";
  operand->base->print (o);
}

void post_crement::print (ostream& o) const
{
  o << '(' << *operand << ")" << op;
}


void ternary_expression::print (ostream& o) const
{
  o << "(" << *cond << ")?("
    << *truevalue << "):("
    << *falsevalue << ")";
}


void symbol::print (ostream& o) const
{
  o << name;
}

void target_register::print (ostream &o) const
{
  o << "@" << (userspace_p ? "u" : "k") << "register(" << regno << ")";
}

void target_deref::print (ostream &o) const
{
  o << "@" << (userspace_p ? "u" : "k") << "deref(" << size << ", " << *addr << ")";
}

void target_bitfield::print (ostream &o) const
{
  o << "@" << (signed_p ? "s" : "u") << "bitfield(" << *base
    << ", " << offset << ", " << size << ")";
}

void target_symbol::component::print (ostream& o) const
{
  switch (type)
    {
    case comp_pretty_print:
    case comp_struct_member:
      o << "->" << member;
      break;
    case comp_literal_array_index:
      o << '[' << num_index << ']';
      break;
    case comp_expression_array_index:
      o << '[' << *expr_index << ']';
      break;
    }
}


std::ostream& operator << (std::ostream& o, const target_symbol::component& c)
{
  c.print (o);
  return o;
}


void target_symbol::print (ostream& o) const
{
  if (addressof)
    o << "&";
  o << name;
  for (unsigned i = 0; i < components.size(); ++i)
    o << components[i];
}


void atvar_op::print (ostream& o) const
{
  if (addressof)
    o << "&";
  o << name << "(\"" << target_name << "\"";
  if (module.length() > 0)
    o << ", " << lex_cast_qstring (module);
  o << ')';
  for (unsigned i = 0; i < components.size(); ++i)
    o << components[i];
}


void cast_op::print (ostream& o) const
{
  if (addressof)
    o << "&";
  o << name << '(' << *operand;
  o << ", " << lex_cast_qstring (type_name);
  if (module.length() > 0)
    o << ", " << lex_cast_qstring (module);
  o << ')';
  for (unsigned i = 0; i < components.size(); ++i)
    o << components[i];
}


void autocast_op::print (ostream& o) const
{
  if (addressof)
    o << "&";
  o << '(' << *operand << ')';
  for (unsigned i = 0; i < components.size(); ++i)
    o << components[i];
}


void defined_op::print (ostream& o) const
{
  o << "@defined(" << *operand << ")";
}


void probewrite_op::print (ostream& o) const
{
  o << "@probewrite(" << name << ")";
}


void entry_op::print (ostream& o) const
{
  o << "@entry(" << *operand << ")";
}


void perf_op::print (ostream& o) const
{
  o << "@perf(" << *operand << ")";
}


void vardecl::print (ostream& o) const
{
  o << ((unmangled_name != "") ? unmangled_name : name); // unmangled_name empty for some synthesized vardecls
  if(wrap)
    o << "%";
  if (maxsize > 0)
    o << "[" << maxsize << "]";
  if (arity > 0 || index_types.size() > 0)
    o << "[...]";
  if (init)
    {
      o << " = ";
      init->print(o);
    }
}


void vardecl::printsig (ostream& o) const
{
  if (unmangled_name != "")
    o << unmangled_name;
  else
    o << name;
  if(wrap)
     o << "%";
  if (maxsize > 0)
    o << "[" << maxsize << "]";
  o << ":" << type;
  if (index_types.size() > 0)
    {
      o << " [";
      for (unsigned i=0; i<index_types.size(); i++)
        o << (i>0 ? ", " : "") << index_types[i];
      o << "]";
    }
}


void functiondecl::print (ostream& o) const
{
  o << "function " << unmangled_name << " (";
  for (unsigned i=0; i<formal_args.size(); i++)
    o << (i>0 ? ", " : "") << *formal_args[i];
  o << ")" << endl;
  body->print(o);
}


void functiondecl::printsig (ostream& o) const
{
  o << unmangled_name << ":" << type << " (";
  for (unsigned i=0; i<formal_args.size(); i++)
    o << (i>0 ? ", " : "")
      << *formal_args[i]
      << ":"
      << formal_args[i]->type;
  o << ")";
}

embedded_tags_visitor::embedded_tags_visitor(bool all_tags)
{
  // populate the set of tags that could appear in embedded code/expressions
  available_tags.insert("/* bpf */");
  available_tags.insert("/* guru */");
  available_tags.insert("/* unprivileged */");
  available_tags.insert("/* myproc-unprivileged */");
  if (all_tags)
    {
      available_tags.insert("/* pure */");
      available_tags.insert("/* unmangled */");
      available_tags.insert("/* unmodified-fnargs */");
      available_tags.insert("/* stable */");
    }
}

void embedded_tags_visitor::visit_embeddedcode (embeddedcode *s)
{
  for (auto tag = available_tags.begin(); tag != available_tags.end(); ++tag)
    if (s->tagged_p(*tag))
      tags.insert(*tag);
}

void embedded_tags_visitor::visit_embedded_expr (embedded_expr *e)
{
  for (auto tag = available_tags.begin(); tag != available_tags.end(); ++tag)
    if (e->tagged_p(*tag))
      tags.insert(*tag);
}

void functiondecl::printsigtags (ostream& o, bool all_tags) const
{
  this->printsig(o);

  // Visit the function's body to see if there's any embedded_code or
  // embeddedexpr that have special tags (e.g. /* guru */)
  embedded_tags_visitor etv(all_tags);
  this->body->visit(&etv);

  for (auto tag = etv.tags.begin(); tag != etv.tags.end(); ++tag)
    o << " " << *tag;
}

void arrayindex::print (ostream& o) const
{
  base->print (o);
  o << "[";
  for (unsigned i=0; i<indexes.size(); i++)
    {
      o << (i>0 ? ", " : "");
      if (indexes[i]==0)
        o << "*";
      else
        o << *indexes[i];
    }
  o << "]";
}


void functioncall::print (ostream& o) const
{
  o << function << "(";
  for (unsigned i=0; i<args.size(); i++)
    o << (i>0 ? ", " : "") << *args[i];
  o << ")";
}


print_format*
print_format::create(const token *t, const char *n)
{
  bool stream, format, delim, newline, _char;
  interned_string type;
  string str_type;

  if (n == NULL)
    {
      type = t->content;
      str_type = type;
      n = str_type.c_str();
    }
  else
    type = n;

  stream = true;
  format = delim = newline = _char = false;

  if (strcmp(n, "print_char") == 0)
    _char = true;
  else
    {
      if (*n == 's')
	{
	  stream = false;
	  ++n;
	}

      if (0 != strncmp(n, "print", 5))
	return NULL;
      n += 5;

      if (*n == 'f')
	{
	  format = true;
	  ++n;
	}
      else
	{
	  if (*n == 'd')
	    {
	      delim = true;
	      ++n;
	    }

	  if (*n == 'l' && *(n+1) == 'n')
	    {
	      newline = true;
	      n += 2;
	    }
	}

      if (*n != '\0')
	return NULL;
    }

  print_format *pf = new print_format(stream, format, delim, newline, _char, type);
  pf->tok = t;
  return pf;
}


string
print_format::components_to_string(vector<format_component> const & components)
{
  ostringstream oss;

  for (vector<format_component>::const_iterator i = components.begin();
       i != components.end(); ++i)
    {

      assert (i->type != conv_unspecified);

      if (i->type == conv_literal)
	{
	  assert(!i->literal_string.empty());
	  for (interned_string::const_iterator j = i->literal_string.begin();
	       j != i->literal_string.end(); ++j)
	    {
              // See also: c_unparser::visit_literal_string and lex_cast_qstring
	      if (*j == '%')
		oss << '%';
              else if(*j == '"')
                oss << '\\';
	      oss << *j;
	    }
	}
      else
	{
	  oss << '%';

	  if (i->test_flag (fmt_flag_zeropad))
	    oss << '0';

	  if (i->test_flag (fmt_flag_plus))
	    oss << '+';

	  if (i->test_flag (fmt_flag_space))
	    oss << ' ';

	  if (i->test_flag (fmt_flag_left))
	    oss << '-';

	  if (i->test_flag (fmt_flag_special))
	    oss << '#';

	  if (i->widthtype == width_dynamic)
	    oss << '*';
	  else if (i->widthtype != width_unspecified && i->width > 0)
	    oss << i->width;

	  if (i->prectype == prec_dynamic)
	    oss << ".*";
	  else if (i->prectype != prec_unspecified && i->precision > 0)
	    oss << '.' << i->precision;

	  switch (i->type)
	    {
	    case conv_binary:
	      oss << "b";
	      break;

	    case conv_char:
	      oss << "llc";
	      break;

	    case conv_number:
	      if (i->base == 16)
		{
		  if (i->test_flag (fmt_flag_large))
		    oss << "llX";
		  else
		    oss << "llx";
		}
	      else if (i->base == 8)
		oss << "llo";
	      else if (i->test_flag (fmt_flag_sign))
		oss << "lld";
	      else
		oss << "llu";
	      break;

	    case conv_pointer:
	      oss << "p";
	      break;

	    case conv_string:
	      oss << 's';
	      break;

	    case conv_memory:
	      oss << 'm';
	      break;

	    case conv_memory_hex:
	      oss << 'M';
	      break;

	    default:
	      break;
	    }
	}
    }
  return oss.str ();
}

vector<print_format::format_component>
print_format::string_to_components(string const & str)
{
  format_component curr;
  vector<format_component> res;

  curr.clear();

  string::const_iterator i = str.begin();
  string literal_str;
  
  while (i != str.end())
    {
      if (*i != '%')
	{
	  assert (curr.type == conv_unspecified || curr.type == conv_literal);
	  curr.type = conv_literal;
          literal_str += *i;
	  ++i;
	  continue;
	}
      else if (i+1 == str.end() || *(i+1) == '%')
	{
	  assert(*i == '%');
	  // *i == '%' and *(i+1) == '%'; append only one '%' to the literal string
	  assert (curr.type == conv_unspecified || curr.type == conv_literal);
	  curr.type = conv_literal;
	  literal_str += '%';
          i += 2;
	  continue;
	}
      else
	{
	  assert(*i == '%');
	  if (curr.type != conv_unspecified)
	    {
	      // Flush any component we were previously accumulating
	      assert (curr.type == conv_literal);
              curr.literal_string = literal_str;
              res.push_back(curr);
              literal_str.clear();
	      curr.clear();
	    }
	}
      ++i;

      if (i == str.end())
	break;

      // Now we are definitely parsing a conversion.
      // Begin by parsing flags (which are optional).

      switch (*i)
	{
	case '0':
	  curr.set_flag (fmt_flag_zeropad);
	  ++i;
	  break;

	case '+':
	  curr.set_flag (fmt_flag_plus);
	  ++i;
	  break;

	case '-':
	  curr.set_flag (fmt_flag_left);
	  ++i;
	  break;

	case ' ':
	  curr.set_flag (fmt_flag_space);
	  ++i;
	  break;

	case '#':
	  curr.set_flag (fmt_flag_special);
	  ++i;
	  break;

	default:
	  break;
	}

      if (i == str.end())
	break;

      // Parse optional width
      if (*i == '*')
	{
	  curr.widthtype = width_dynamic;
	  ++i;
	}
      else if (isdigit(*i))
	{
	  curr.widthtype = width_static;
	  curr.width = 0;
	  do
	    {
	      curr.width *= 10;
	      curr.width += (*i - '0');
	      ++i;
	    }
	  while (i != str.end() && isdigit(*i));
	}

      if (i == str.end())
	break;

      // Parse optional precision
      if (*i == '.')
	{
	  ++i;
	  if (i == str.end())
	    break;
	  if (*i == '*')
	    {
	      curr.prectype = prec_dynamic;
	      ++i;
	    }
	  else if (isdigit(*i))
	    {
	      curr.prectype = prec_static;
	      curr.precision = 0;
	      do
		{
		  curr.precision *= 10;
		  curr.precision += (*i - '0');
		  ++i;
		}
	      while (i != str.end() && isdigit(*i));
	    }
	}

      if (i == str.end())
	break;

      // Parse the type modifier
      switch (*i)
      {
      case 'l':
        ++i;
        break;
      }

      if (i == str.end())
	break;

      // Parse the actual conversion specifier (bcsmdioupxXn)
      switch (*i)
	{
	  // Valid conversion types
	case 'b':
	  curr.type = conv_binary;
	  break;

	case 'c':
	  curr.type = conv_char;
	  break;

	case 's':
	  curr.type = conv_string;
	  break;

	case 'm':
	  curr.type = conv_memory;
	  break;

	case 'M':
	  curr.type = conv_memory_hex;
	  break;

	case 'd':
	case 'i':
	  curr.set_flag (fmt_flag_sign);
	  /* Fallthrough */
	case 'u':
	  curr.type = conv_number;
	  curr.base = 10;
	  break;

	case 'o':
	  curr.type = conv_number;
	  curr.base = 8;
	  break;

	case 'X':
	  curr.set_flag (fmt_flag_large);
	  /* Fallthrough */
	case 'x':
	  curr.type = conv_number;
	  curr.base = 16;
	  break;

	case 'p':
	  // Since stap 1.3, %p == %#x.
	  curr.set_flag (fmt_flag_special);
	  curr.type = conv_pointer;
	  curr.base = 16;
	  // Oddness for stap < 1.3 is handled in translation
	  break;

	default:
	  break;
	}

      if (curr.type == conv_unspecified)
	throw PARSE_ERROR(_("invalid or missing conversion specifier"));

      ++i;
      curr.literal_string = literal_str;
      res.push_back(curr);
      literal_str.clear();
      curr.clear();
    }

  // If there's a remaining partly-composed conversion, fail.
  if (!curr.is_empty())
    {
      if (curr.type == conv_literal)
        {
          curr.literal_string = literal_str;
          res.push_back(curr);
          // no need to clear curr / literal_str; we're leaving
        }
      else
	throw PARSE_ERROR(_("trailing incomplete print format conversion"));
    }

  return res;
}


void print_format::print (ostream& o) const
{
  o << print_format_type << "(";
  if (print_with_format)
    o << lex_cast_qstring (raw_components);
  if (print_with_delim)
    o << lex_cast_qstring (delimiter);
  if (hist)
    hist->print(o);
  for (vector<expression*>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      if (i != args.begin() || print_with_format || print_with_delim)
	o << ", ";
      (*i)->print(o);
    }
  o << ")";
}

void stat_op::print (ostream& o) const
{
  o << '@';
  switch (ctype)
    {
    case sc_average:
      o << "avg(";
      break;

    case sc_count:
      o << "count(";
      break;

    case sc_sum:
      o << "sum(";
      break;

    case sc_min:
      o << "min(";
      break;

    case sc_max:
      o << "max(";
      break;

    case sc_variance:
      o << "variance(";
      break;

    case sc_none:
      assert (0); // should not happen, as sc_none is only used in foreach sorts
      break;
    }
  stat->print(o);

  if (ctype == sc_variance && params.size() == 1)
    o << ", " << params[0];

  o << ")";
}

void
hist_op::print (ostream& o) const
{
  o << '@';
  switch (htype)
    {
    case hist_linear:
      assert(params.size() == 3);
      o << "hist_linear(";
      stat->print(o);
      for (size_t i = 0; i < params.size(); ++i)
	{
	  o << ", " << params[i];
	}
      o << ")";
      break;

    case hist_log:
      assert(params.size() == 0);
      o << "hist_log(";
      stat->print(o);
      o << ")";
      break;
    }
}

ostream& operator << (ostream& o, const statement& k)
{
  k.print (o);
  return o;
}


void embeddedcode::print (ostream &o) const
{
  o << "%{";
  o << code;
  o << "%}";
}

void block::print (ostream& o) const
{
  o << "{" << endl;
  for (unsigned i=0; i<statements.size(); i++)
    o << *statements [i] << ";" << endl;
  o << "}";
}

block::block (statement* car, statement* cdr)
{
  statements.push_back(car);
  statements.push_back(cdr);
  this->tok = car->tok;
}

block::block (statement* car, statement* cdr1, statement* cdr2)
{
  statements.push_back(car);
  statements.push_back(cdr1);
  statements.push_back(cdr2);
  this->tok = car->tok;
}


void try_block::print (ostream& o) const
{
  o << "try {" << endl;
  if (try_block) o << *try_block << endl;
  o << "} catch ";
  if (catch_error_var) o << "(" << *catch_error_var << ") ";
  o << "{" << endl;
  if (catch_block) o << *catch_block << endl;
  o << "}" << endl;
}


void for_loop::print (ostream& o) const
{
  o << "for (";
  if (init) init->print (o);
  o << "; ";
  cond->print (o);
  o << "; ";
  if (incr) incr->print (o);
  o << ") ";
  block->print (o);
}


void foreach_loop::print (ostream& o) const
{
  o << "foreach (";
  if (value)
    {
      value->print (o);
      o << " = ";
    }
  o << "[";
  for (unsigned i=0; i<indexes.size(); i++)
    {
      if (i > 0) o << ", ";
      indexes[i]->print (o);
      if (sort_direction != 0 && sort_column == i+1)
	o << (sort_direction > 0 ? "+" : "-");
    }
  o << "] in ";
  base->print (o);
  if (!array_slice.empty())
    {
      o << "[";
      for (unsigned i=0; i<array_slice.size(); i++)
        {
          if (i > 0) o << ", ";
          if (array_slice[i])
            array_slice[i]->print (o);
          else
            o << "*";
        }
      o << "]";
    }
  if (sort_direction != 0 && sort_column == 0)
    {
      switch (sort_aggr)
        {
        case sc_count: o << " @count"; break;
        case sc_average: o << " @avg"; break;
        case sc_min: o << " @min"; break;
        case sc_max: o << " @max"; break;
        case sc_sum: o << " @sum"; break;
        case sc_none:
        default: 
          ;
        }
        
      o << (sort_direction > 0 ? "+" : "-");
    }
  if (limit)
    {
      o << " limit ";
      limit->print (o);
    }
  o << ") ";
  block->print (o);
}


void null_statement::print (ostream& o) const
{
  o << ";";
}


void expr_statement::print (ostream& o) const
{
  if (value)
    o << *value;
  else
    o << ";"; /* mid-elision-pass null-statement */
}


void return_statement::print (ostream& o) const
{
  if (value)
    o << "return " << *value;
  else
    o << "return";
}


void delete_statement::print (ostream& o) const
{
  o << "delete " << *value;
}

void next_statement::print (ostream& o) const
{
  o << "next";
}

void break_statement::print (ostream& o) const
{
  o << "break";
}

void continue_statement::print (ostream& o) const
{
  o << "continue";
}

void if_statement::print (ostream& o) const
{
  o << "if (" << *condition << ") ";
  if (thenblock) o << *thenblock; else o << ";"; o << endl;
  if (elseblock)
    o << "else " << *elseblock << endl;
}


void stapfile::print (ostream& o) const
{
  o << "# file " << name << endl;

  for (unsigned i=0; i<embeds.size(); i++)
    embeds[i]->print (o);

  for (unsigned i=0; i<globals.size(); i++)
    {
      o << "global ";
      globals[i]->print (o);
      o << endl;
    }

  for (unsigned i=0; i<aliases.size(); i++)
    {
      aliases[i]->print (o);
      o << endl;
    }

  for (unsigned i=0; i<probes.size(); i++)
    {
      probes[i]->print (o);
      o << endl;
    }

  for (unsigned j = 0; j < functions.size(); j++)
    {
      functions[j]->print (o);
      o << endl;
    }
}


void probe::print (ostream& o) const
{
  o << "probe ";
  printsig (o);
  o << *body;
}


void probe::printsig (ostream& o) const
{
  const probe_alias *alias = get_alias ();
  if (alias)
    {
      alias->printsig (o);
      return;
    }

  for (unsigned i=0; i<locations.size(); i++)
    {
      if (i > 0) o << ",";
      locations[i]->print (o);
    }
}


void
probe::collect_derivation_chain (std::vector<probe*> &probes_list) const
{
  probes_list.push_back(const_cast<probe*>(this));
  if (base)
    base->collect_derivation_chain(probes_list);
}


void
probe::collect_derivation_pp_chain (std::vector<probe_point*> &pp_list) const
{
  pp_list.push_back(const_cast<probe_point*>(this->locations[0]));
  if (base)
    base->collect_derivation_pp_chain(pp_list);
}



void probe_point::print (ostream& o, bool print_extras) const
{
  for (unsigned i=0; i<components.size(); i++)
    {
      if (i>0) o << ".";
      probe_point::component* c = components[i];
      if (!c)
        {
          // We might like to excise this weird 0 pointer, just in
          // case some recursion during the error-handling path, but
          // can't, because what we have here is a const *this.
          static int been_here = 0;
          if (been_here == 0) {
            been_here ++;
            throw SEMANTIC_ERROR (_("internal error: missing probe point component"));
          } else
            continue; // ... sad panda decides to skip the bad boy
        }
      o << c->functor;
      if (c->arg)
        o << "(" << *c->arg << ")";
    }
  if (!print_extras)
    return;
  if (sufficient)
    o << "!";
  else if (optional) // sufficient implies optional
    o << "?";
  if (condition)
    o<< " if (" << *condition << ")";
}

string probe_point::str (bool print_extras) const
{
  ostringstream o;
  print(o, print_extras);
  return o.str();
}


probe_alias::probe_alias(std::vector<probe_point*> const & aliases):
  probe (), alias_names (aliases), epilogue_style(false), body2(0)
{
}

void probe_alias::printsig (ostream& o) const
{
  for (unsigned i=0; i<alias_names.size(); i++)
    {
      o << (i>0 ? " = " : "");
      alias_names[i]->print (o);
    }
  o << (epilogue_style ? " += " : " = ");
  for (unsigned i=0; i<locations.size(); i++)
    {
      if (i > 0) o << ", ";
      locations[i]->print (o);
    }
}


ostream& operator << (ostream& o, const probe_point& k)
{
  k.print (o);
  return o;
}


ostream& operator << (ostream& o, const symboldecl& k)
{
  k.print (o);
  return o;
}

void
debug(expression *e)
{
  cout << *e << endl;
}

// ------------------------------------------------------------------------
// visitors


void
block::visit (visitor* u)
{
  u->visit_block (this);
}


void
try_block::visit (visitor* u)
{
  u->visit_try_block (this);
}


void
embeddedcode::visit (visitor* u)
{
  u->visit_embeddedcode (this);
}


void
for_loop::visit (visitor* u)
{
  u->visit_for_loop (this);
}

void
foreach_loop::visit (visitor* u)
{
  u->visit_foreach_loop (this);
}

void
null_statement::visit (visitor* u)
{
  u->visit_null_statement (this);
}

void
expr_statement::visit (visitor* u)
{
  u->visit_expr_statement (this);
}

void
return_statement::visit (visitor* u)
{
  u->visit_return_statement (this);
}

void
delete_statement::visit (visitor* u)
{
  u->push_active_lvalue (this->value);
  u->visit_delete_statement (this);
  u->pop_active_lvalue ();
}

void
if_statement::visit (visitor* u)
{
  u->visit_if_statement (this);
}

void
next_statement::visit (visitor* u)
{
  u->visit_next_statement (this);
}

void
break_statement::visit (visitor* u)
{
  u->visit_break_statement (this);
}

void
continue_statement::visit (visitor* u)
{
  u->visit_continue_statement (this);
}

void
literal_string::visit(visitor* u)
{
  u->visit_literal_string (this);
}

void
literal_number::visit(visitor* u)
{
  u->visit_literal_number (this);
}

void
binary_expression::visit (visitor* u)
{
  u->visit_binary_expression (this);
}

void
embedded_expr::visit (visitor* u)
{
  u->visit_embedded_expr (this);
}

void
unary_expression::visit (visitor* u)
{
  u->visit_unary_expression (this);
}

void
pre_crement::visit (visitor* u)
{
  u->push_active_lvalue (this->operand);
  u->visit_pre_crement (this);
  u->pop_active_lvalue ();
}

void
post_crement::visit (visitor* u)
{
  u->push_active_lvalue (this->operand);
  u->visit_post_crement (this);
  u->pop_active_lvalue ();
}

void
logical_or_expr::visit (visitor* u)
{
  u->visit_logical_or_expr (this);
}

void
logical_and_expr::visit (visitor* u)
{
  u->visit_logical_and_expr (this);
}

void
array_in::visit (visitor* u)
{
  u->visit_array_in (this);
}

void
regex_query::visit (visitor* u)
{
  u->visit_regex_query (this);
}

void
compound_expression::visit (visitor* u)
{
  u->visit_compound_expression (this);
}

void
comparison::visit (visitor* u)
{
  u->visit_comparison (this);
}

void
concatenation::visit (visitor* u)
{
  u->visit_concatenation (this);
}

void
ternary_expression::visit (visitor* u)
{
  u->visit_ternary_expression (this);
}

void
assignment::visit (visitor* u)
{
  u->push_active_lvalue (this->left);
  u->visit_assignment (this);
  u->pop_active_lvalue ();
}

void
symbol::visit (visitor* u)
{
  u->visit_symbol (this);
}

void
target_register::visit (visitor* u)
{
  u->visit_target_register(this);
}

void
target_deref::visit (visitor* u)
{
  u->visit_target_deref(this);
}

void
target_bitfield::visit (visitor* u)
{
  u->visit_target_bitfield(this);
}

void
target_symbol::visit (visitor* u)
{
  u->visit_target_symbol(this);
}

void
target_symbol::visit_components (visitor* u)
{
  for (unsigned i = 0; i < components.size(); ++i)
    if (components[i].type == comp_expression_array_index)
      components[i].expr_index->visit (u);
}

void
target_symbol::visit_components (update_visitor* u)
{
  for (unsigned i = 0; i < components.size(); ++i)
    if (components[i].type == comp_expression_array_index)
      u->replace (components[i].expr_index);
}

void
cast_op::visit (visitor* u)
{
  u->visit_cast_op(this);
}


void
autocast_op::visit (visitor* u)
{
  u->visit_autocast_op(this);
}


void
atvar_op::visit (visitor* u)
{
  u->visit_atvar_op(this);
}


void
defined_op::visit (visitor* u)
{
  u->visit_defined_op(this);
}


void
probewrite_op::visit (visitor* u)
{
  u->visit_probewrite_op(this);
}

void
entry_op::visit (visitor* u)
{
  u->visit_entry_op(this);
}


void
perf_op::visit (visitor* u)
{
  u->visit_perf_op(this);
}


void
arrayindex::visit (visitor* u)
{
  u->visit_arrayindex (this);
}

void
functioncall::visit (visitor* u)
{
  u->visit_functioncall (this);
}

void
print_format::visit (visitor *u)
{
  u->visit_print_format (this);
}

void
stat_op::visit (visitor *u)
{
  u->visit_stat_op (this);
}

void
hist_op::visit (visitor *u)
{
  u->visit_hist_op (this);
}


bool
expression::is_symbol(symbol *& sym_out)
{
  sym_out = NULL;
  return false;
}

bool
indexable::is_symbol(symbol *& sym_out)
{
  sym_out = NULL;
  return false;
}

bool
indexable::is_hist_op(hist_op *& hist_out)
{
  hist_out = NULL;
  return false;
}

bool
symbol::is_symbol(symbol *& sym_out)
{
  sym_out = this;
  return true;
}

bool
hist_op::is_hist_op(hist_op *& hist_out)
{
  hist_out = this;
  return true;
}

void
classify_indexable(indexable* ix,
		   symbol *& array_out,
		   hist_op *& hist_out)
{
  array_out = NULL;
  hist_out = NULL;
  assert(ix != NULL);
  if (!(ix->is_symbol (array_out) || ix->is_hist_op (hist_out)))
    throw SEMANTIC_ERROR(_("Expecting symbol or histogram operator"), ix->tok);
  if (!(hist_out || array_out))
    throw SEMANTIC_ERROR(_("Failed to classify indexable"), ix->tok);
}


// ------------------------------------------------------------------------

bool
visitor::is_active_lvalue(expression *e)
{
  for (unsigned i = 0; i < active_lvalues.size(); ++i)
    {
      if (active_lvalues[i] == e)
	return true;
    }
  return false;
}

void
visitor::push_active_lvalue(expression *e)
{
  active_lvalues.push_back(e);
}

void
visitor::pop_active_lvalue()
{
  assert(!active_lvalues.empty());
  active_lvalues.pop_back();
}



// ------------------------------------------------------------------------

void
traversing_visitor::visit_block (block* s)
{
  for (unsigned i=0; i<s->statements.size(); i++)
    s->statements[i]->visit (this);
}

void
traversing_visitor::visit_try_block (try_block* s)
{
  if (s->try_block)
    s->try_block->visit (this);
  if (s->catch_error_var)
    s->catch_error_var->visit (this);
  if (s->catch_block)
    s->catch_block->visit (this);
}

void
traversing_visitor::visit_embeddedcode (embeddedcode*)
{
}

void
traversing_visitor::visit_null_statement (null_statement*)
{
}

void
traversing_visitor::visit_expr_statement (expr_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_if_statement (if_statement* s)
{
  s->condition->visit (this);
  s->thenblock->visit (this);
  if (s->elseblock)
    s->elseblock->visit (this);
}

void
traversing_visitor::visit_for_loop (for_loop* s)
{
  if (s->init) s->init->visit (this);
  s->cond->visit (this);
  if (s->incr) s->incr->visit (this);
  s->block->visit (this);
}

void
traversing_visitor::visit_foreach_loop (foreach_loop* s)
{
  s->base->visit(this);

  for (unsigned i=0; i<s->indexes.size(); i++)
    s->indexes[i]->visit (this);

  if (s->value)
    s->value->visit (this);

  if (s->limit)
    s->limit->visit (this);

  s->block->visit (this);
}

void
traversing_visitor::visit_return_statement (return_statement* s)
{
  if (s->value)
    s->value->visit (this);
}

void
traversing_visitor::visit_delete_statement (delete_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_next_statement (next_statement*)
{
}

void
traversing_visitor::visit_break_statement (break_statement*)
{
}

void
traversing_visitor::visit_continue_statement (continue_statement*)
{
}

void
traversing_visitor::visit_literal_string (literal_string*)
{
}

void
traversing_visitor::visit_literal_number (literal_number*)
{
}

void
traversing_visitor::visit_embedded_expr (embedded_expr*)
{
}

void
traversing_visitor::visit_binary_expression (binary_expression* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_unary_expression (unary_expression* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_pre_crement (pre_crement* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_post_crement (post_crement* e)
{
  e->operand->visit (this);
}


void
traversing_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_array_in (array_in* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_regex_query (regex_query* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_compound_expression (compound_expression* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_comparison (comparison* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_concatenation (concatenation* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_ternary_expression (ternary_expression* e)
{
  e->cond->visit (this);
  e->truevalue->visit (this);
  e->falsevalue->visit (this);
}

void
traversing_visitor::visit_assignment (assignment* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_symbol (symbol*)
{
}

void
traversing_visitor::visit_target_register (target_register*)
{
}

void
traversing_visitor::visit_target_deref (target_deref* e)
{
  e->addr->visit (this);
}

void
traversing_visitor::visit_target_bitfield (target_bitfield* e)
{
  e->base->visit (this);
}

void
traversing_visitor::visit_target_symbol (target_symbol* e)
{
  e->visit_components (this);
}

void
traversing_visitor::visit_cast_op (cast_op* e)
{
  e->operand->visit (this);
  e->visit_components (this);
}

void
traversing_visitor::visit_autocast_op (autocast_op* e)
{
  e->operand->visit (this);
  e->visit_components (this);
}

void
traversing_visitor::visit_atvar_op (atvar_op* e)
{
  e->visit_components (this);
}

void
traversing_visitor::visit_defined_op (defined_op* e)
{
  e->operand->visit (this);
}


void
traversing_visitor::visit_probewrite_op (probewrite_op* e)
{
  throw SEMANTIC_ERROR(_("unexpected @probewrite"), e->tok);
}


void
traversing_visitor::visit_entry_op (entry_op* e)
{
  e->operand->visit (this);
}


void
traversing_visitor::visit_perf_op (perf_op* e)
{
  e->operand->visit (this);
}


void
traversing_visitor::visit_arrayindex (arrayindex* e)
{
  for (unsigned i=0; i<e->indexes.size(); i++)
    if (e->indexes[i])
      e->indexes[i]->visit (this);

  e->base->visit(this);
}

void
traversing_visitor::visit_functioncall (functioncall* e)
{
  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);
}

void
traversing_visitor::visit_print_format (print_format* e)
{
  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);
  if (e->hist)
    e->hist->visit(this);
}

void
traversing_visitor::visit_stat_op (stat_op* e)
{
  e->stat->visit (this);
}

void
traversing_visitor::visit_hist_op (hist_op* e)
{
  e->stat->visit (this);
}


void
expression_visitor::visit_literal_string (literal_string* e)
{
  traversing_visitor::visit_literal_string (e);
  visit_expression (e);
}

void
expression_visitor::visit_literal_number (literal_number* e)
{
  traversing_visitor::visit_literal_number (e);
  visit_expression (e);
}

void
expression_visitor::visit_embedded_expr (embedded_expr* e)
{
  traversing_visitor::visit_embedded_expr (e);
  visit_expression (e);
}

void
expression_visitor::visit_binary_expression (binary_expression* e)
{
  traversing_visitor::visit_binary_expression (e);
  visit_expression (e);
}

void
expression_visitor::visit_unary_expression (unary_expression* e)
{
  traversing_visitor::visit_unary_expression (e);
  visit_expression (e);
}

void
expression_visitor::visit_pre_crement (pre_crement* e)
{
  traversing_visitor::visit_pre_crement (e);
  visit_expression (e);
}

void
expression_visitor::visit_post_crement (post_crement* e)
{
  traversing_visitor::visit_post_crement (e);
  visit_expression (e);
}

void
expression_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  traversing_visitor::visit_logical_or_expr (e);
  visit_expression (e);
}

void
expression_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  traversing_visitor::visit_logical_and_expr (e);
  visit_expression (e);
}

void
expression_visitor::visit_array_in (array_in* e)
{
  traversing_visitor::visit_array_in (e);
  visit_expression (e);
}

void
expression_visitor::visit_regex_query (regex_query* e)
{
  traversing_visitor::visit_regex_query (e);
  visit_expression (e);
}

void
expression_visitor::visit_compound_expression (compound_expression* e)
{
  traversing_visitor::visit_compound_expression (e);
  visit_expression (e);
}

void
expression_visitor::visit_comparison (comparison* e)
{
  traversing_visitor::visit_comparison (e);
  visit_expression (e);
}

void
expression_visitor::visit_concatenation (concatenation* e)
{
  traversing_visitor::visit_concatenation (e);
  visit_expression (e);
}

void
expression_visitor::visit_ternary_expression (ternary_expression* e)
{
  traversing_visitor::visit_ternary_expression (e);
  visit_expression (e);
}

void
expression_visitor::visit_assignment (assignment* e)
{
  traversing_visitor::visit_assignment (e);
  visit_expression (e);
}

void
expression_visitor::visit_symbol (symbol* e)
{
  traversing_visitor::visit_symbol (e);
  visit_expression (e);
}

void
expression_visitor::visit_target_register (target_register* e)
{
  traversing_visitor::visit_target_register (e);
  visit_expression (e);
}

void
expression_visitor::visit_target_deref (target_deref* e)
{
  traversing_visitor::visit_target_deref (e);
  visit_expression (e);
}

void
expression_visitor::visit_target_bitfield (target_bitfield* e)
{
  traversing_visitor::visit_target_bitfield (e);
  visit_expression (e);
}

void
expression_visitor::visit_target_symbol (target_symbol* e)
{
  traversing_visitor::visit_target_symbol (e);
  visit_expression (e);
}

void
expression_visitor::visit_arrayindex (arrayindex* e)
{
  traversing_visitor::visit_arrayindex (e);
  visit_expression (e);
}

void
expression_visitor::visit_functioncall (functioncall* e)
{
  traversing_visitor::visit_functioncall (e);
  visit_expression (e);
}

void
expression_visitor::visit_print_format (print_format* e)
{
  traversing_visitor::visit_print_format (e);
  visit_expression (e);
}

void
expression_visitor::visit_stat_op (stat_op* e)
{
  traversing_visitor::visit_stat_op (e);
  visit_expression (e);
}

void
expression_visitor::visit_hist_op (hist_op* e)
{
  traversing_visitor::visit_hist_op (e);
  visit_expression (e);
}

void
expression_visitor::visit_cast_op (cast_op* e)
{
  traversing_visitor::visit_cast_op (e);
  visit_expression (e);
}

void
expression_visitor::visit_autocast_op (autocast_op* e)
{
  traversing_visitor::visit_autocast_op (e);
  visit_expression (e);
}

void
expression_visitor::visit_atvar_op (atvar_op* e)
{
  traversing_visitor::visit_atvar_op (e);
  visit_expression (e);
}

void
expression_visitor::visit_defined_op (defined_op* e)
{
  traversing_visitor::visit_defined_op (e);
  visit_expression (e);
}

void
expression_visitor::visit_entry_op (entry_op* e)
{
  traversing_visitor::visit_entry_op (e);
  visit_expression (e);
}

void
expression_visitor::visit_perf_op (perf_op* e)
{
  traversing_visitor::visit_perf_op (e);
  visit_expression (e);
}


void
functioncall_traversing_visitor::visit_functioncall (functioncall* e)
{
  traversing_visitor::visit_functioncall (e);
  this->enter_functioncall(e);
}

void
functioncall_traversing_visitor::enter_functioncall (functioncall* e)
{
  for (unsigned i = 0; i < e->referents.size(); i++)
    {
      functiondecl* referent = e->referents[i];
      // prevent infinite recursion
      if (nested.find (referent) == nested.end ())
        {
          if (seen.find(referent) == seen.end())
            seen.insert (referent);
          nested.insert (referent);
          // recurse
          functiondecl* last_current_function = current_function;
          current_function = referent;
          referent->body->visit (this);
          current_function = last_current_function;
          nested.erase (referent);
        }
      else { this->note_recursive_functioncall(e); }
    }
}

void
functioncall_traversing_visitor::note_recursive_functioncall (functioncall*)
{
}


void
symuse_collecting_visitor::visit_try_block(try_block* s)
{
  // Default to varuse visitor behaviour if the referent is set.
  if (s->catch_error_var && s->catch_error_var->referent)
    varuse_collecting_visitor::visit_try_block(s);
  else
    {
      if (s->try_block)
        s->try_block->visit(this);
      if (s->catch_block)
        s->catch_block->visit(this);
    }

  // Add the name regardless of whether the referent is set or not.
  if (s->catch_error_var)
    written_names.insert(s->catch_error_var->name);
}


void
symuse_collecting_visitor::visit_embeddedcode(embeddedcode* s)
{
  // If either of the referent vectors have a single element, then
  // we can default to varuse visitor behaviour.
  if (!s->read_referents.empty() || !s->write_referents.empty())
    varuse_collecting_visitor::visit_embeddedcode(s);
}


void
symuse_collecting_visitor::visit_embedded_expr(embedded_expr* e)
{
  // Similar to symuse_collecting_visitor::visit_embeddedcode.
  if (!e->read_referents.empty() || !e->write_referents.empty())
    varuse_collecting_visitor::visit_embedded_expr(e);
}


void
symuse_collecting_visitor::visit_target_symbol(target_symbol* e)
{
  varuse_collecting_visitor::visit_target_symbol(e);

  // We treat target symbols differently under the symuse visitor
  // when compared to the varuse visitor. Target symbol names are
  // added to the name sets that track reads and writes.
  if (is_active_lvalue(e))
    written_names.insert(e->name);
  else
    read_names.insert(e->name);
}


void
symuse_collecting_visitor::visit_symbol(symbol* e)
{
  // Similar to the varuse visitor case. The symbol names are also tracked.
  if (e->referent)
    varuse_collecting_visitor::visit_symbol(e);

  if (current_lvalue == e || current_lrvalue == e)
    written_names.insert(e->name);

  if (current_lvalue != e || current_lrvalue)
    read_names.insert(e->name);
}


void
symuse_collecting_visitor::visit_probewrite_op(probewrite_op*)
{
}


void
varuse_collecting_visitor::visit_if_statement (if_statement *s)
{
  assert(!current_lvalue_read);
  current_lvalue_read = true;
  s->condition->visit (this);
  current_lvalue_read = false;

  s->thenblock->visit (this);
  if (s->elseblock)
    s->elseblock->visit (this);
}


void
varuse_collecting_visitor::visit_for_loop (for_loop *s)
{
  if (s->init) s->init->visit (this);

  assert(!current_lvalue_read);
  current_lvalue_read = true;
  s->cond->visit (this);
  current_lvalue_read = false;

  if (s->incr) s->incr->visit (this);
  s->block->visit (this);
}

void
varuse_collecting_visitor::visit_try_block (try_block *s)
{
  if (s->try_block)
    s->try_block->visit (this);
  if (s->catch_error_var)
    written.insert (s->catch_error_var->referent);
  if (s->catch_block)
    s->catch_block->visit (this);

  // NB: don't functioncall_traversing_visitor::visit_try_block (s);
  // since that would count s->catch_error_var as a read also.
}

void
varuse_collecting_visitor::visit_functioncall (functioncall* e)
{
  // NB: don't call functioncall_traversing_visitor::visit_functioncall(). We
  // replicate functionality here but split argument visiting from actual
  // function visiting.

  bool last_lvalue_read = current_lvalue_read;

  // arguments are used
  current_lvalue_read = true;
  traversing_visitor::visit_functioncall(e);

  // but function body shouldn't all be marked used
  current_lvalue_read = false;
  functioncall_traversing_visitor::enter_functioncall(e);

  current_lvalue_read = last_lvalue_read;
}

void
varuse_collecting_visitor::visit_return_statement (return_statement *s)
{
  assert(!current_lvalue_read);
  current_lvalue_read = true;
  functioncall_traversing_visitor::visit_return_statement(s);
  current_lvalue_read = false;
}


void
varuse_collecting_visitor::visit_embeddedcode (embeddedcode *s)
{
  assert (current_function); // only they get embedded code

  // collect vardecls earlier identified during sym resolution
  read.insert(s->read_referents.begin(), s->read_referents.end());
  written.insert(s->write_referents.begin(), s->write_referents.end());  

  // check types
  for (auto it = s->read_referents.begin(); it != s->read_referents.end(); it++) {
    vardecl* v = *it;
    if (v->type == pe_stats)
      throw SEMANTIC_ERROR(_("Aggregates not available in embedded-C"), v->tok);
  }
  for (auto it = s->write_referents.begin(); it != s->write_referents.end(); it++) {
    vardecl* v = *it;
    if (v->type == pe_stats)
      throw SEMANTIC_ERROR(_("Aggregates not available in embedded-C"), v->tok);
  }

  
  // NB: we used to do security/safety checks here, which are now over
  // in elaborate.cxx functioncall_security_check because they may be
  // call-site-sensitive.

  // PR14524: Support old-style THIS->local syntax on per-function basis.
  if (s->tagged_p ("/* unmangled */"))
    current_function->mangle_oldstyle = true;

  // We want to elide embedded-C functions when possible.  For
  // example, each $target variable access is expanded to an
  // embedded-C function call.  Yet, for safety reasons, we should
  // presume that embedded-C functions have intentional side-effects.
  //
  // To tell these two types of functions apart, we apply a
  // Kludge(tm): we look for a magic string within the function body.
  // $target variables as rvalues will have this; lvalues won't.
  // Also, explicit side-effect-free tapset functions will have this.

  if (s->tagged_p ("/* pure */"))
    return;

  embedded_seen = true;
}


// About the same case as above.
void
varuse_collecting_visitor::visit_embedded_expr (embedded_expr *e)
{
  // collect vardecls earlier identified during sym resolution
  read.insert(e->read_referents.begin(), e->read_referents.end());
  written.insert(e->write_referents.begin(), e->write_referents.end());  

  // check types
  for (auto it = e->read_referents.begin(); it != e->read_referents.end(); it++) {
    vardecl* v = *it;
    if (v->type == pe_stats)
      throw SEMANTIC_ERROR(_("Aggregates not available in embedded-C"), v->tok);
  }
  for (auto it = e->write_referents.begin(); it != e->write_referents.end(); it++) {
    vardecl* v = *it;
    if (v->type == pe_stats)
      throw SEMANTIC_ERROR(_("Aggregates not available in embedded-C"), v->tok);
  }

  // Don't allow embedded C expressions in unprivileged mode unless
  // they are tagged with /* unprivileged */ or /* myproc-unprivileged */
  // or we're in a usermode runtime.
  if (! pr_contains (session.privilege, pr_stapdev) &&
      ! pr_contains (session.privilege, pr_stapsys) &&
      ! session.runtime_usermode_p () &&
      ! e->tagged_p ("/* unprivileged */") &&
      ! e->tagged_p ("/* myproc-unprivileged */"))
    throw SEMANTIC_ERROR (_F("embedded expression may not be used when --privilege=%s is specified",
			     pr_name (session.privilege)),
			  e->tok);

  // Don't allow /* guru */ functions unless -g is active.
  if (!session.guru_mode && e->tagged_p ("/* guru */"))
    throw SEMANTIC_ERROR (_("embedded expression may not be used unless -g is specified"),
			  e->tok);

  // We want to elide embedded-C functions when possible.  For
  // example, each $target variable access is expanded to an
  // embedded-C function call.  Yet, for safety reasons, we should
  // presume that embedded-C functions have intentional side-effects.
  //
  // To tell these two types of functions apart, we apply a
  // Kludge(tm): we look for a magic string within the function body.
  // $target variables as rvalues will have this; lvalues won't.
  // Also, explicit side-effect-free tapset functions will have this.

  if (e->tagged_p ("/* pure */"))
    return;

  embedded_seen = true;
}

void
varuse_collecting_visitor::visit_target_symbol (target_symbol *e)
{
  // Still-unresolved target symbol assignments get treated as
  // generating side-effects like embedded-C, to prevent premature
  // elision and later error message suppression (PR5516).  rvalue use
  // of unresolved target symbols is OTOH not considered a side-effect.

  if (is_active_lvalue (e))
    embedded_seen = true;

  functioncall_traversing_visitor::visit_target_symbol (e);
}


void
varuse_collecting_visitor::visit_target_register (target_register *e)
{
  // Similar to visit_target_symbol.
  if (is_active_lvalue (e))
    embedded_seen = true;

  // No need to call the base op, we've done everything here.
}

void
varuse_collecting_visitor::visit_target_deref (target_deref *e)
{
  // Similar to visit_target_symbol.
  if (is_active_lvalue (e))
    embedded_seen = true;

  functioncall_traversing_visitor::visit_target_deref (e);
}

void
varuse_collecting_visitor::visit_atvar_op (atvar_op *e)
{
  // Similar to visit_target_symbol

  if (is_active_lvalue (e))
    embedded_seen = true;

  functioncall_traversing_visitor::visit_atvar_op (e);
}


void
varuse_collecting_visitor::visit_cast_op (cast_op *e)
{
  // As with target_symbols, unresolved cast assignments need to preserved
  // for later error handling.
  if (is_active_lvalue (e))
    embedded_seen = true;

  functioncall_traversing_visitor::visit_cast_op (e);
}

void
varuse_collecting_visitor::visit_autocast_op (autocast_op *e)
{
  // As with target_symbols, unresolved cast assignments need to preserved
  // for later error handling.
  if (is_active_lvalue (e))
    embedded_seen = true;

  functioncall_traversing_visitor::visit_autocast_op (e);
}

void
varuse_collecting_visitor::visit_defined_op (defined_op *e)
{
  // XXX
  functioncall_traversing_visitor::visit_defined_op (e);
}

void
varuse_collecting_visitor::visit_entry_op (entry_op *e)
{
  // XXX
  functioncall_traversing_visitor::visit_entry_op (e);
}


void
varuse_collecting_visitor::visit_perf_op (perf_op *e)
{
  functioncall_traversing_visitor::visit_perf_op (e);
}


void
varuse_collecting_visitor::visit_print_format (print_format* e)
{
  // NB: Instead of being top-level statements, "print" and "printf"
  // are implemented as statement-expressions containing a
  // print_format.  They have side-effects, but not via the
  // embedded-code detection method above.
  //
  // But sprint and sprintf don't have side-effects.

  bool last_lvalue_read = current_lvalue_read;
  current_lvalue_read = true;
  if (e->print_to_stream)
    embedded_seen = true; // a proxy for "has unknown side-effects"

  functioncall_traversing_visitor::visit_print_format (e);
  current_lvalue_read = last_lvalue_read;
}


void
varuse_collecting_visitor::visit_assignment (assignment *e)
{
  if (e->op == "=" || e->op == "<<<") // pure writes
    {
      expression* last_lvalue = current_lvalue;
      bool last_lvalue_read = current_lvalue_read;
      current_lvalue = e->left; // leave a mark for ::visit_symbol
      current_lvalue_read = true;
      functioncall_traversing_visitor::visit_assignment (e);
      current_lvalue = last_lvalue;
      current_lvalue_read = last_lvalue_read;
    }
  else // read-modify-writes
    {
      expression* last_lrvalue = current_lrvalue;
      current_lrvalue = e->left; // leave a mark for ::visit_symbol
      functioncall_traversing_visitor::visit_assignment (e);
      current_lrvalue = last_lrvalue;
    }
}

void
varuse_collecting_visitor::visit_ternary_expression (ternary_expression* e)
{
  // NB: don't call base class's implementation. We do the work here already.

  bool last_lvalue_read = current_lvalue_read;
  current_lvalue_read = true;
  e->cond->visit (this);
  current_lvalue_read = last_lvalue_read;

  e->truevalue->visit (this);
  e->falsevalue->visit (this);
}

void
varuse_collecting_visitor::visit_symbol (symbol *e)
{
  if (e->referent == 0)
    throw SEMANTIC_ERROR (_("symbol without referent"), e->tok);

  // We could handle initialized globals by marking them as "written".
  // However, this current visitor may be called for a function or
  // probe body, from the point of view of which this global is
  // already initialized, so not written.
  /*
  if (e->referent->init)
    written.insert (e->referent);
  */

  if (current_lvalue == e || current_lrvalue == e)
    {
      written.insert (e->referent);
    }
  if (current_lvalue != e || current_lrvalue == e)
    {
      read.insert (e->referent);
    }

  if (current_lrvalue == e)
    {
      if (current_lvalue_read)
        used.insert (e->referent);
    }
  else if (current_lvalue != e)
    used.insert (e->referent);
}

// NB: stat_op need not be overridden, since it will get to
// visit_symbol and only as a possible rvalue.


void
varuse_collecting_visitor::visit_arrayindex (arrayindex *e)
{
  // NB: don't call parent implementation, we do the work here.

  // First let's visit the indexes separately
  bool old_lvalue_read = current_lvalue_read;
  current_lvalue_read = true;
  for (unsigned i=0; i<e->indexes.size(); i++)
    if (e->indexes[i])
      e->indexes[i]->visit (this);
  current_lvalue_read = old_lvalue_read;

  // Hooking this callback is necessary because of the hacky
  // statistics representation.  For the expression "i[4] = 5", the
  // incoming lvalue will point to this arrayindex.  However, the
  // symbol corresponding to the "i[4]" is multiply inherited with
  // arrayindex.  If the symbol base part of this object is not at
  // offset 0, then static_cast<symbol*>(e) may result in a different
  // address, and not match lvalue by number when we recurse that way.
  // So we explicitly override the incoming lvalue/lrvalue values to
  // point at the embedded objects' actual base addresses.

  expression* last_lrvalue = current_lrvalue;
  expression* last_lvalue = current_lvalue;

  symbol *array = NULL;
  hist_op *hist = NULL;
  classify_indexable(e->base, array, hist);
  expression *value = array ?: hist->stat;

  if (current_lrvalue == e) current_lrvalue = value;
  if (current_lvalue == e) current_lvalue = value;

  e->base->visit(this);

  current_lrvalue = last_lrvalue;
  current_lvalue = last_lvalue;
}


void
varuse_collecting_visitor::visit_pre_crement (pre_crement *e)
{
  expression* last_lrvalue = current_lrvalue;
  current_lrvalue = e->operand; // leave a mark for ::visit_symbol
  functioncall_traversing_visitor::visit_pre_crement (e);
  current_lrvalue = last_lrvalue;
}

void
varuse_collecting_visitor::visit_post_crement (post_crement *e)
{
  expression* last_lrvalue = current_lrvalue;
  current_lrvalue = e->operand; // leave a mark for ::visit_symbol
  functioncall_traversing_visitor::visit_post_crement (e);
  current_lrvalue = last_lrvalue;
}

void
varuse_collecting_visitor::visit_foreach_loop (foreach_loop* s)
{
  assert(!current_lvalue_read);

  // NB: we duplicate so don't bother call
  // functioncall_traversing_visitor::visit_foreach_loop (s);

  s->base->visit(this);

  // If the collection is sorted, imply a "write" access to the
  // array in addition to the "read" one already noted above.
  if (s->sort_direction)
    {
      symbol *array = NULL;
      hist_op *hist = NULL;
      classify_indexable (s->base, array, hist);
      if (array) this->written.insert (array->referent);
      // XXX: Can hist_op iterations be sorted?
    }

  // NB: don't forget to visit the index expressions, which are lvalues.
  for (unsigned i=0; i<s->indexes.size(); i++)
    {
      expression* last_lvalue = current_lvalue;
      current_lvalue = s->indexes[i]; // leave a mark for ::visit_symbol
      s->indexes[i]->visit (this);
      current_lvalue = last_lvalue;
    }

  // visit the additional specified array slice
  current_lvalue_read = true;
  for (unsigned i=0; i<s->array_slice.size(); i++)
    {
      if (s->array_slice[i])
        s->array_slice[i]->visit (this);
    }
  current_lvalue_read = false;

  // The value is an lvalue too
  if (s->value)
    {
      expression* last_lvalue = current_lvalue;
      current_lvalue = s->value; // leave a mark for ::visit_symbol
      s->value->visit (this);
      current_lvalue = last_lvalue;
    }

  if (s->limit)
    {
      current_lvalue_read = true;
      s->limit->visit (this);
      current_lvalue_read = false;
    }

  s->block->visit (this);
}


void
varuse_collecting_visitor::visit_delete_statement (delete_statement* s)
{
  // Ideally, this would be treated like an assignment: a plain write
  // to the underlying value ("lvalue").  XXX: However, the
  // optimization pass is not smart enough to remove an unneeded
  // "delete" yet, so we pose more like a *crement ("lrvalue").  This
  // should protect the underlying value from optimizional mischief.
  assert(!current_lvalue_read);
  expression* last_lrvalue = current_lrvalue;
  current_lrvalue = s->value; // leave a mark for ::visit_symbol
  current_lvalue_read = true;
  functioncall_traversing_visitor::visit_delete_statement (s);
  current_lrvalue = last_lrvalue;
  current_lvalue_read = false;
}

bool
varuse_collecting_visitor::side_effect_free ()
{
  return (written.empty() && !embedded_seen);
}


bool
varuse_collecting_visitor::side_effect_free_wrt (const set<vardecl*>& vars)
{
  // A looser notion of side-effect-freeness with respect to a given
  // list of variables.

  // That's useful because the written list may consist of local
  // variables of called functions.  But visible side-effects only
  // occur if the client's locals, or any globals are written-to.

  set<vardecl*> intersection;
  insert_iterator<set<vardecl*> > int_it (intersection, intersection.begin());
  set_intersection (written.begin(), written.end(),
                    vars.begin(), vars.end(),
                    int_it);

  return (intersection.empty() && !embedded_seen);
}




// ------------------------------------------------------------------------


throwing_visitor::throwing_visitor (const std::string& m): msg (m) {}
throwing_visitor::throwing_visitor (): msg (_("invalid element")) {}


void
throwing_visitor::throwone (const token* t)
{
  throw SEMANTIC_ERROR (msg, t);
}

void
throwing_visitor::visit_block (block* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_try_block (try_block* s)
{
  throwone (s->tok);
}


void
throwing_visitor::visit_embeddedcode (embeddedcode* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_null_statement (null_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_expr_statement (expr_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_if_statement (if_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_for_loop (for_loop* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_foreach_loop (foreach_loop* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_return_statement (return_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_delete_statement (delete_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_next_statement (next_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_break_statement (break_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_continue_statement (continue_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_literal_string (literal_string* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_literal_number (literal_number* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_embedded_expr (embedded_expr* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_binary_expression (binary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_unary_expression (unary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_pre_crement (pre_crement* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_post_crement (post_crement* e)
{
  throwone (e->tok);
}


void
throwing_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_array_in (array_in* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_regex_query (regex_query* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_compound_expression (compound_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_comparison (comparison* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_concatenation (concatenation* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_ternary_expression (ternary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_assignment (assignment* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_symbol (symbol* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_target_register (target_register* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_target_deref (target_deref* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_target_bitfield (target_bitfield* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_target_symbol (target_symbol* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_atvar_op (atvar_op* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_cast_op (cast_op* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_autocast_op (autocast_op* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_defined_op (defined_op* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_probewrite_op(probewrite_op* e)
{
  throwone(e->tok);
}

void
throwing_visitor::visit_entry_op (entry_op* e)
{
  throwone (e->tok);
}


void
throwing_visitor::visit_perf_op (perf_op* e)
{
  throwone (e->tok);
}


void
throwing_visitor::visit_arrayindex (arrayindex* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_functioncall (functioncall* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_print_format (print_format* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_stat_op (stat_op* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_hist_op (hist_op* e)
{
  throwone (e->tok);
}


// ------------------------------------------------------------------------


void
update_visitor::visit_block (block* s)
{
  for (unsigned i = 0; i < s->statements.size(); ++i)
    replace (s->statements[i]);
  provide (s);
}

void
update_visitor::visit_try_block (try_block* s)
{
  replace (s->try_block);
  replace (s->catch_error_var);
  replace (s->catch_block);
  provide (s);
}

void
update_visitor::visit_embeddedcode (embeddedcode* s)
{
  provide (s);
}

void
update_visitor::visit_null_statement (null_statement* s)
{
  provide (s);
}

void
update_visitor::visit_expr_statement (expr_statement* s)
{
  replace (s->value);
  provide (s);
}

void
update_visitor::visit_if_statement (if_statement* s)
{
  replace (s->condition);
  replace (s->thenblock);
  replace (s->elseblock);
  provide (s);
}

void
update_visitor::visit_for_loop (for_loop* s)
{
  replace (s->init);
  replace (s->cond);
  replace (s->incr);
  replace (s->block);
  provide (s);
}

void
update_visitor::visit_foreach_loop (foreach_loop* s)
{
  for (unsigned i = 0; i < s->indexes.size(); ++i)
    replace (s->indexes[i]);
  replace (s->base);
  replace (s->value);
  replace (s->limit);
  replace (s->block);
  provide (s);
}

void
update_visitor::visit_return_statement (return_statement* s)
{
  replace (s->value);
  provide (s);
}

void
update_visitor::visit_delete_statement (delete_statement* s)
{
  replace (s->value);
  provide (s);
}

void
update_visitor::visit_next_statement (next_statement* s)
{
  provide (s);
}

void
update_visitor::visit_break_statement (break_statement* s)
{
  provide (s);
}

void
update_visitor::visit_continue_statement (continue_statement* s)
{
  provide (s);
}

void
update_visitor::visit_literal_string (literal_string* e)
{
  provide (e);
}

void
update_visitor::visit_literal_number (literal_number* e)
{
  provide (e);
}

void
update_visitor::visit_embedded_expr (embedded_expr* e)
{
  provide (e);
}

void
update_visitor::visit_binary_expression (binary_expression* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_unary_expression (unary_expression* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_pre_crement (pre_crement* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_post_crement (post_crement* e)
{
  replace (e->operand);
  provide (e);
}


void
update_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_array_in (array_in* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_regex_query (regex_query* e)
{
  replace (e->left);
  replace (e->right); // XXX: do we *need* to replace literal in RHS?
  provide (e);
}

void
update_visitor::visit_compound_expression (compound_expression* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_comparison (comparison* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_concatenation (concatenation* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_ternary_expression (ternary_expression* e)
{
  replace (e->cond);
  replace (e->truevalue);
  replace (e->falsevalue);
  provide (e);
}

void
update_visitor::visit_assignment (assignment* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_symbol (symbol* e)
{
  provide (e);
}

void
update_visitor::visit_target_register (target_register* e)
{
  provide (e);
}

void
update_visitor::visit_target_deref (target_deref* e)
{
  replace (e->addr);
  provide (e);
}

void
update_visitor::visit_target_bitfield (target_bitfield* e)
{
  replace (e->base);
  provide (e);
}

void
update_visitor::visit_target_symbol (target_symbol* e)
{
  e->visit_components (this);
  provide (e);
}

void
update_visitor::visit_cast_op (cast_op* e)
{
  replace (e->operand);
  e->visit_components (this);
  provide (e);
}

void
update_visitor::visit_autocast_op (autocast_op* e)
{
  replace (e->operand);
  e->visit_components (this);
  provide (e);
}

void
update_visitor::visit_atvar_op (atvar_op* e)
{
  e->visit_components (this);
  provide (e);
}

void
update_visitor::visit_defined_op (defined_op* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_probewrite_op(probewrite_op* e)
{
  provide(e);
}

void
update_visitor::visit_entry_op (entry_op* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_perf_op (perf_op* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_arrayindex (arrayindex* e)
{
  replace (e->base);
  for (unsigned i = 0; i < e->indexes.size(); ++i)
    replace (e->indexes[i]);
  provide (e);
}

void
update_visitor::visit_functioncall (functioncall* e)
{
  for (unsigned i = 0; i < e->args.size(); ++i)
    replace (e->args[i]);
  provide (e);
}

void
update_visitor::visit_print_format (print_format* e)
{
  for (unsigned i = 0; i < e->args.size(); ++i)
    replace (e->args[i]);
  replace (e->hist);
  provide (e);
}

void
update_visitor::visit_stat_op (stat_op* e)
{
  replace (e->stat);
  provide (e);
}

void
update_visitor::visit_hist_op (hist_op* e)
{
  replace (e->stat);
  provide (e);
}


// ------------------------------------------------------------------------


void
deep_copy_visitor::visit_block (block* s)
{
  update_visitor::visit_block(new block(*s));
}

void
deep_copy_visitor::visit_try_block (try_block* s)
{
  update_visitor::visit_try_block(new try_block(*s));
}

void
deep_copy_visitor::visit_embeddedcode (embeddedcode* s)
{
  update_visitor::visit_embeddedcode(new embeddedcode(*s));
}

void
deep_copy_visitor::visit_null_statement (null_statement* s)
{
  update_visitor::visit_null_statement(new null_statement(*s));
}

void
deep_copy_visitor::visit_expr_statement (expr_statement* s)
{
  update_visitor::visit_expr_statement(new expr_statement(*s));
}

void
deep_copy_visitor::visit_if_statement (if_statement* s)
{
  update_visitor::visit_if_statement(new if_statement(*s));
}

void
deep_copy_visitor::visit_for_loop (for_loop* s)
{
  update_visitor::visit_for_loop(new for_loop(*s));
}

void
deep_copy_visitor::visit_foreach_loop (foreach_loop* s)
{
  update_visitor::visit_foreach_loop(new foreach_loop(*s));
}

void
deep_copy_visitor::visit_return_statement (return_statement* s)
{
  update_visitor::visit_return_statement(new return_statement(*s));
}

void
deep_copy_visitor::visit_delete_statement (delete_statement* s)
{
  update_visitor::visit_delete_statement(new delete_statement(*s));
}

void
deep_copy_visitor::visit_next_statement (next_statement* s)
{
  update_visitor::visit_next_statement(new next_statement(*s));
}

void
deep_copy_visitor::visit_break_statement (break_statement* s)
{
  update_visitor::visit_break_statement(new break_statement(*s));
}

void
deep_copy_visitor::visit_continue_statement (continue_statement* s)
{
  update_visitor::visit_continue_statement(new continue_statement(*s));
}

void
deep_copy_visitor::visit_literal_string (literal_string* e)
{
  update_visitor::visit_literal_string(new literal_string(*e));
}

void
deep_copy_visitor::visit_literal_number (literal_number* e)
{
  update_visitor::visit_literal_number(new literal_number(*e));
}

void
deep_copy_visitor::visit_embedded_expr (embedded_expr* e)
{
  update_visitor::visit_embedded_expr(new embedded_expr(*e));
}

void
deep_copy_visitor::visit_binary_expression (binary_expression* e)
{
  update_visitor::visit_binary_expression(new binary_expression(*e));
}

void
deep_copy_visitor::visit_unary_expression (unary_expression* e)
{
  update_visitor::visit_unary_expression(new unary_expression(*e));
}

void
deep_copy_visitor::visit_pre_crement (pre_crement* e)
{
  update_visitor::visit_pre_crement(new pre_crement(*e));
}

void
deep_copy_visitor::visit_post_crement (post_crement* e)
{
  update_visitor::visit_post_crement(new post_crement(*e));
}


void
deep_copy_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  update_visitor::visit_logical_or_expr(new logical_or_expr(*e));
}

void
deep_copy_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  update_visitor::visit_logical_and_expr(new logical_and_expr(*e));
}

void
deep_copy_visitor::visit_array_in (array_in* e)
{
  update_visitor::visit_array_in(new array_in(*e));
}

void
deep_copy_visitor::visit_regex_query (regex_query* e)
{
  update_visitor::visit_regex_query(new regex_query(*e));
}

void
deep_copy_visitor::visit_compound_expression(compound_expression* e)
{
  update_visitor::visit_compound_expression(new compound_expression(*e));
}

void
deep_copy_visitor::visit_comparison (comparison* e)
{
  update_visitor::visit_comparison(new comparison(*e));
}

void
deep_copy_visitor::visit_concatenation (concatenation* e)
{
  update_visitor::visit_concatenation(new concatenation(*e));
}

void
deep_copy_visitor::visit_ternary_expression (ternary_expression* e)
{
  update_visitor::visit_ternary_expression(new ternary_expression(*e));
}

void
deep_copy_visitor::visit_assignment (assignment* e)
{
  update_visitor::visit_assignment(new assignment(*e));
}

void
deep_copy_visitor::visit_symbol (symbol* e)
{
  symbol* n = new symbol(*e);
  n->referent = NULL; // don't copy!
  update_visitor::visit_symbol(n);
}

void
deep_copy_visitor::visit_target_register (target_register* e)
{
  target_register* n = new target_register(*e);
  update_visitor::visit_target_register(n);
}

void
deep_copy_visitor::visit_target_deref (target_deref* e)
{
  target_deref* n = new target_deref(*e);
  update_visitor::visit_target_deref(n);
}

void
deep_copy_visitor::visit_target_bitfield (target_bitfield* e)
{
  target_bitfield* n = new target_bitfield(*e);
  update_visitor::visit_target_bitfield(n);
}

void
deep_copy_visitor::visit_target_symbol (target_symbol* e)
{
  target_symbol* n = new target_symbol(*e);
  update_visitor::visit_target_symbol(n);
}

void
deep_copy_visitor::visit_cast_op (cast_op* e)
{
  update_visitor::visit_cast_op(new cast_op(*e));
}

void
deep_copy_visitor::visit_autocast_op (autocast_op* e)
{
  update_visitor::visit_autocast_op(new autocast_op(*e));
}

void
deep_copy_visitor::visit_atvar_op (atvar_op* e)
{
  update_visitor::visit_atvar_op(new atvar_op(*e));
}

void
deep_copy_visitor::visit_defined_op (defined_op* e)
{
  update_visitor::visit_defined_op(new defined_op(*e));
}

void
deep_copy_visitor::visit_entry_op (entry_op* e)
{
  update_visitor::visit_entry_op(new entry_op(*e));
}

void
deep_copy_visitor::visit_perf_op (perf_op* e)
{
  update_visitor::visit_perf_op(new perf_op(*e));
}

void
deep_copy_visitor::visit_arrayindex (arrayindex* e)
{
  update_visitor::visit_arrayindex(new arrayindex(*e));
}

void
deep_copy_visitor::visit_functioncall (functioncall* e)
{
  functioncall* n = new functioncall(*e);
  
  // PR25841 ... we used to clear n->referents(), anticipating a later
  // symbol resolution pass to replace all the functiondecl*'s, for
  // reasons lost to history.  But in the face of function cloning,
  // and the peculiar naming conventions there, ::find_functions()
  // can't easily do the right thing.  So let's just preserve the
  // referents.
  //
  // n->referents.clear();

  update_visitor::visit_functioncall(n);
}

void
deep_copy_visitor::visit_print_format (print_format* e)
{
  update_visitor::visit_print_format(new print_format(*e));
}

void
deep_copy_visitor::visit_stat_op (stat_op* e)
{
  update_visitor::visit_stat_op(new stat_op(*e));
}

void
deep_copy_visitor::visit_hist_op (hist_op* e)
{
  update_visitor::visit_hist_op(new hist_op(*e));
}


/* for invocation from gdb */
void
debug_print(const expression* x)
{
  x->print(cout);
  cout << endl;
}
void
debug_print(const statement* x)
{
  x->print(cout);
  cout << endl;
}
void
debug_print(const probe_point* x)
{
  x->print(cout);
  cout << endl;
}


ostream& operator << (ostream& o, const exp_type_details& d)
{
  d.print (o);
  return o;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */

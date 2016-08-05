/* ----------------------------------------------------------------------------- 
 * This file is part of SWIG, which is licensed as a whole under version 3 
 * (or any later version) of the GNU General Public License. Some additional
 * terms also apply to certain portions of SWIG. The full details of the SWIG
 * license and copyrights can be found in the LICENSE and COPYRIGHT files
 * included with the SWIG source code as distributed by the SWIG developers
 * and at http://www.swig.org/legal.html.
 *
 * utils.cxx
 *
 * Various utility functions.
 * ----------------------------------------------------------------------------- */

#include <swigmod.h>

int is_public(Node *n) {
  String *access = Getattr(n, "access");
  return !access || !Cmp(access, "public");
}

int is_private(Node *n) {
  String *access = Getattr(n, "access");
  return access && !Cmp(access, "private");
}

int is_protected(Node *n) {
  String *access = Getattr(n, "access");
  return access && !Cmp(access, "protected");
}

static int is_member_director_helper(Node *parentnode, Node *member) {
  int parent_nodirector = GetFlag(parentnode, "feature:nodirector");
  if (parent_nodirector)
    return 0;
  int parent_director = Swig_director_mode() && GetFlag(parentnode, "feature:director");
  int cdecl_director = parent_director || GetFlag(member, "feature:director");
  int cdecl_nodirector = GetFlag(member, "feature:nodirector");
  return cdecl_director && !cdecl_nodirector && !GetFlag(member, "feature:extend");
}

int is_member_director(Node *parentnode, Node *member) {
  if (parentnode && checkAttribute(member, "storage", "virtual")) {
    return is_member_director_helper(parentnode, member);
  } else {
    return 0;
  }
}

int is_member_director(Node *member) {
  return is_member_director(Getattr(member, "parentNode"), member);
}

// Identifies the additional protected members that are generated when the allprotected option is used.
// This does not include protected virtual methods as they are turned on with the dirprot option.
int is_non_virtual_protected_access(Node *n) {
  int result = 0;
  if (Swig_director_mode() && Swig_director_protected_mode() && Swig_all_protected_mode() && is_protected(n) && !checkAttribute(n, "storage", "virtual")) {
    Node *parentNode = Getattr(n, "parentNode");
    // When vtable is empty, the director class does not get emitted, so a check for an empty vtable should be done.
    // However, vtable is set in Language and so is not yet set when methods in Typepass call clean_overloaded()
    // which calls is_non_virtual_protected_access. So commented out below.
    // Moving the director vtable creation into into Typepass should solve this problem.
    if (is_member_director_helper(parentNode, n) /* && Getattr(parentNode, "vtable")*/)
      result = 1;
  }
  return result;
}

/* Clean overloaded list.  Removes templates, ignored, and errors */

void clean_overloaded(Node *n) {
  Node *nn = Getattr(n, "sym:overloaded");
  Node *first = 0;
  while (nn) {
    String *ntype = nodeType(nn);
    if ((GetFlag(nn, "feature:ignore")) ||
	(Getattr(nn, "error")) ||
	(Strcmp(ntype, "template") == 0) ||
	((Strcmp(ntype, "cdecl") == 0) && is_protected(nn) && !is_member_director(nn) && !is_non_virtual_protected_access(n))) {
      /* Remove from overloaded list */
      Node *ps = Getattr(nn, "sym:previousSibling");
      Node *ns = Getattr(nn, "sym:nextSibling");
      if (ps) {
	Setattr(ps, "sym:nextSibling", ns);
      }
      if (ns) {
	Setattr(ns, "sym:previousSibling", ps);
      }
      Delattr(nn, "sym:previousSibling");
      Delattr(nn, "sym:nextSibling");
      Delattr(nn, "sym:overloaded");
      nn = ns;
      continue;
    } else {
      if (!first)
	first = nn;
      Setattr(nn, "sym:overloaded", first);
    }
    nn = Getattr(nn, "sym:nextSibling");
  }
  if (!first || (first && !Getattr(first, "sym:nextSibling"))) {
    if (Getattr(n, "sym:overloaded"))
      Delattr(n, "sym:overloaded");
  }
}

/* -----------------------------------------------------------------------------
 * Swig_set_max_hash_expand()
 *
 * Controls how many Hash objects are displayed when displaying nested Hash objects.
 * Makes DohSetMaxHashExpand an externally callable function (for debugger).
 * ----------------------------------------------------------------------------- */

void Swig_set_max_hash_expand(int count) {
  SetMaxHashExpand(count);
}


void populate_docParmList(Node *n) {

    Parm *pi = Getattr(n, "wrap:parms");
    
    List *arg_out = NewList();
    List *arg_in  = NewList();
    
    bool is_void = checkAttribute(n, "type", "void");
    if (!is_void) {
      Hash *node = NewHash();
      Setattr(node, "type", Getattr(n, "tmap:out:doc"));
      Append(arg_out, node);
    }

    // Find out how many inputs
    Parm *pj = pi;
    while (pj) {
      // Skip swallowed inputs
      if (!checkAttribute(pj, "tmap:in:numinputs", "0") || Getattr(pj, "tmap:argout")) {
        String *name = 0;
        String *pdoc = Getattr(pj, "tmap:doc");
        if (pdoc) name = Getattr(pj, "tmap:doc:name");

        // Note: the generated name should be consistent with that in kwnames[]
        name = name ? name : Getattr(pj, "name");
        name = name ? name : Getattr(pj, "lname");
        name = Swig_name_make(pj, 0, name, 0, 0); // rename parameter if a keyword
        
        bool output = Getattr(pj, "tmap:argout");
        
        Hash *node = NewHash();

        
        if (checkAttribute(pj, "name", "self")) {
          Setattr(node, "self", "1");
        } else {
          String *type_str = Getattr(pj, "tmap:in:doc");
          if (type_str) {
            type_str = Copy(type_str);
          } else {
            SwigType *type = Getattr(pj, "type");
            Node *nn = 0;//= classLookup(type);
            type_str = nn ? Copy(Getattr(nn, "sym:name")) : SwigType_str(type, 0);
          }
          Setattr(node, "type", type_str);
          //Delete(type_str);
          Setattr(node, "name", name);
        }
        Append(output? arg_out : arg_in, node);
      }
      
      
      Parm *pk = Getattr(pj, "tmap:in:next");
      if (pk) {
	pj = pk;
      } else {
	pj = nextSibling(pj);
      }
    }
    
    Setattr(n, "paramlist:in", arg_in);
    Setattr(n, "paramlist:out", arg_out);
}

int format_paramlist(String* f, List* paramlist, String* normal_entry, String* only_entry, String* no_name, String* self, String* separator) {
    int n = Len(paramlist);
    int i;
    
    bool print_sep = false;
    for (i=0;i<n;++i) {
      Hash *node = Getitem(paramlist, i);
      if (i>0) Printf(f, "%s", separator);
      
      if (Getattr(node, "self")) {
        Printf(f, "%s", self);
        continue;
      }
      
      String* name = Getattr(node, "name");
      String* type = Getattr(node, "type");
      
      String* s_i = NewString("");
      Printf(s_i, "%d", i);
      String* s_ip = NewString("");
      Printf(s_ip, "%d", i+1);
      
      if (! name) {
        name = Copy(no_name);
        Replaceall(name,"$ip",s_ip);
        Replaceall(name,"$i",s_i);
      }
      
      String* t_entry = n==1? Copy(only_entry) : Copy(normal_entry);

      Replaceall(t_entry, "$type", type);
      Replaceall(t_entry, "$name", name);
      Replaceall(t_entry, "$ip", s_ip);
      Replaceall(t_entry, "$i", s_i);
      
      Printf(f, "%s", t_entry);
    }
}

String* Swig_symname(Node *n) {
  String* name = 0;
  if (!name) name = Getattr(n, "memberfunctionHandler:sym:name");
  if (!name) name = Getattr(n, "staticmemberfunctionHandler:sym:name");
  if (!name) name = Getattr(n, "constructorHandler:sym:name");
  if (!name) name = Getattr(n, "destructorHandler:sym:name");    
  if (!name) name = Getattr(n, "sym:name");
  return name;
}

void Swig_doc_split(String *s, String *brief, String *main) {
  int i;
  if (!s) return;
  
  List *split = SplitLines(s);
  for (i=0;i<Len(split);++i) {
    String *line = Copy(Getitem(split, i));
    Chop(line);
    if (Len(line)!=0) break;
  }
  int start = i;
  
  if (start<Len(split)) {
    if (brief) Printf(brief, "%s", Getitem(split, start));
  }
  
  if (main) {
    for (i=start+1; i<Len(split);++i) {
      Printf(main, "%s\n", Getitem(split, i));
    }
  }
}

extern "C" {

/* -----------------------------------------------------------------------------
 * Swig_get_max_hash_expand()
 *
 * Returns how many Hash objects are displayed when displaying nested Hash objects.
 * Makes DohGetMaxHashExpand an externally callable function (for debugger).
 * ----------------------------------------------------------------------------- */

int Swig_get_max_hash_expand() {
  return GetMaxHashExpand();
}

/* -----------------------------------------------------------------------------
 * Swig_to_doh_string()
 *
 * DOH version of Swig_to_string()
 * ----------------------------------------------------------------------------- */

static String *Swig_to_doh_string(DOH *object, int count) {
  int old_count = Swig_get_max_hash_expand();
  if (count >= 0)
    Swig_set_max_hash_expand(count);

  String *debug_string = object ? NewStringf("%s", object) : NewString("NULL");

  Swig_set_max_hash_expand(old_count);
  return debug_string;
}

/* -----------------------------------------------------------------------------
 * Swig_to_doh_string_with_location()
 *
 * DOH version of Swig_to_string_with_location()
 * ----------------------------------------------------------------------------- */

static String *Swig_to_doh_string_with_location(DOH *object, int count) {
  int old_count = Swig_get_max_hash_expand();
  if (count >= 0)
    Swig_set_max_hash_expand(count);

  String *debug_string = Swig_stringify_with_location(object);

  Swig_set_max_hash_expand(old_count);
  return debug_string;
}

/* -----------------------------------------------------------------------------
 * Swig_to_string()
 *
 * Swig debug - return C string representation of any DOH type.
 * Nested Hash types expand count is value of Swig_get_max_hash_expand when count<0
 * Note: leaks memory.
 * ----------------------------------------------------------------------------- */

const char *Swig_to_string(DOH *object, int count) {
  return Char(Swig_to_doh_string(object, count));
}

/* -----------------------------------------------------------------------------
 * Swig_to_string_with_location()
 *
 * Swig debug - return C string representation of any DOH type, within [] brackets
 * for Hash and List types, prefixed by line and file information.
 * Nested Hash types expand count is value of Swig_get_max_hash_expand when count<0
 * Note: leaks memory.
 * ----------------------------------------------------------------------------- */

const char *Swig_to_string_with_location(DOH *object, int count) {
  return Char(Swig_to_doh_string_with_location(object, count));
}

/* -----------------------------------------------------------------------------
 * Swig_print()
 *
 * Swig debug - display string representation of any DOH type.
 * Nested Hash types expand count is value of Swig_get_max_hash_expand when count<0
 * ----------------------------------------------------------------------------- */

void Swig_print(DOH *object, int count) {
  String *output = Swig_to_doh_string(object, count);
  Printf(stdout, "%s\n", output);
  Delete(output);
}

/* -----------------------------------------------------------------------------
 * Swig_to_string_with_location()
 *
 * Swig debug - display string representation of any DOH type, within [] brackets
 * for Hash and List types, prefixed by line and file information.
 * Nested Hash types expand count is value of Swig_get_max_hash_expand when count<0
 * ----------------------------------------------------------------------------- */

void Swig_print_with_location(DOH *object, int count) {
  String *output = Swig_to_doh_string_with_location(object, count);
  Printf(stdout, "%s\n", output);
  Delete(output);
}

} // extern "C"


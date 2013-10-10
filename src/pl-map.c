/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 2013, VU University Amsterdam

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "pl-incl.h"

int PL_get_map_ex(term_t data, term_t class, term_t map);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Maps are associative arrays,  where  keys   are  either  atoms  or small
integers. Maps should be considered  an   abstract  data  type. They are
currently represented as compound terms   using the functor `map`/Arity.
The term has the following layout on the global stack:

  -----------
  | `map`/A |
  -----------
  | class   |
  -----------
  | key1    |
  -----------
  | value1  |
  -----------
  | key2    |
  -----------
  | value2  |
      ...

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define CACHED_MAP_FUNCTORS 128

static functor_t map_functors[CACHED_MAP_FUNCTORS] = {0};

static functor_t
map_functor(int arity)
{ if ( arity < CACHED_MAP_FUNCTORS )
  { if ( map_functors[arity] )
      return map_functors[arity];

    map_functors[arity] = lookupFunctorDef(ATOM_map, arity);
    return map_functors[arity];
  } else
  { return lookupFunctorDef(ATOM_map, arity);
  }
}


static int
get_map_ex(term_t t, Word mp, int create ARG_LD)
{ Word p = valTermRef(t);

  deRef(p);
  if ( isTerm(*p) )
  { Functor f = valueTerm(*p);
    FunctorDef fd = valueFunctor(f->definition);

    if ( fd->name == ATOM_map &&
	 fd->arity%2 == 1 )		/* does *not* validate ordering */
    { *mp = *p;
      return TRUE;
    }
  }

  if ( create )
  { term_t new;

    if ( (new = PL_new_term_ref()) &&
	  PL_get_map_ex(t, 0, new) )
    { p = valTermRef(new);
      deRef(p);
      *mp = *p;
      return TRUE;
    }

    return FALSE;
  }

  return PL_type_error("map", t);
}


static inline int
is_key(word w)
{ return isAtom(w) || isTaggedInt(w);
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
map_lookup_ptr() returns a pointer to the value for a given key
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

Word
map_lookup_ptr(word map, word name ARG_LD)
{ Functor data = valueTerm(map);
  int arity = arityFunctor(data->definition);
  int l = 1, h = arity-2;		/* odd numbers are the keys */

  assert(arity%2 == 1);

  for(;;)
  { int m = ((l+h)/2)|0x1;
    Word p;

    deRef2(&data->arguments[m], p);
    if ( *p == name )
      return p+1;

    if ( l == h )
      return NULL;

    if ( *p < name )
      l=m;
    else if ( h > m )
      h=m;
    else
      h=m-2;
  }
}


static int
compare_map_entry(const void *a, const void *b)
{ GET_LD				/* painful */
  Word p = (Word)a;
  Word q = (Word)b;

  deRef(p);
  deRef(q);
  return (*p<*q ? -1 : *p>*q ? 1 : 0);
}


static int
map_ordered(Word data, int count ARG_LD)
{ Word n1, n2;

  deRef2(data, n1);
  for(; count > 1; count--, data += 2, n1=n2)
  { deRef2(data+2, n2);
    if ( *n1 >= *n2 )
      return FALSE;
  }

  return TRUE;
}


static int
map_order(Word map ARG_LD)
{ Functor data = (Functor)map;
  int arity = arityFunctor(data->definition);

  assert(arity%2 == 1);

  qsort(data->arguments+1, arity/2, sizeof(word)*2,
	compare_map_entry);

  return map_ordered(data->arguments+1, arity/2 PASS_LD);
}


int
map_put(word map, int size, Word nv, word *new_map ARG_LD)
{ Functor data = valueTerm(map);
  int arity = arityFunctor(data->definition);
  Word new, out, in, in_end, nv_end;
  int modified = FALSE;

  assert(arity%2 == 1);

  if ( size == 0 )
  { *new_map = map;
    return TRUE;
  }

  if ( gTop+1+arity+2*size > gMax )
    return GLOBAL_OVERFLOW;

  new    = gTop;
  out    = new+2;			/* functor, class */
  in     = data->arguments+1;
  in_end = in+arity-1;
  nv_end = nv+size*2;

  while(in < in_end && nv < nv_end)
  { Word i_name, n_name;

    deRef2(in, i_name);
    deRef2(nv, n_name);
    if ( *i_name == *n_name )
    { *out++ = *i_name;
      *out++ = linkVal(nv+1);
      in += 2;
      nv += 2;
      if ( !modified && compareStandard(nv+1, in+1, TRUE PASS_LD) )
	modified = TRUE;
    } else if ( *i_name < *n_name )
    { *out++ = *i_name;
      *out++ = linkVal(in+1);
      in += 2;
    } else
    { *out++ = *n_name;
      *out++ = linkVal(nv+1);
      nv += 2;
      modified = TRUE;
    }
  }

  if ( nv == nv_end )
  { if ( !modified )
    { *new_map = map;
      return TRUE;
    }
    while(in < in_end)
    { Word i_name;

      deRef2(in, i_name);
      *out++ = *i_name;
      *out++ = linkVal(in+1);
      in += 2;
    }
  } else
  { while(nv < nv_end)
    { Word n_name;

      deRef2(nv, n_name);
      *out++ = *n_name;
      *out++ = linkVal(nv+1);
      nv += 2;
    }
  }

  gTop = out;
  new[1] = linkVal(&data->arguments[0]);
  new[0] = map_functor(out-(new+1));

  *new_map = consPtr(new, TAG_COMPOUND|STG_GLOBAL);

  return TRUE;
}


static int
get_name_value(Word p, Word name, Word value ARG_LD)
{ deRef(p);

  if ( isTerm(*p) )
  { Functor f = valueTerm(*p);

    if ( f->definition == FUNCTOR_minus2 ||  /* Name-Value */
	 f->definition == FUNCTOR_equals2 || /* Name=Value */
	 f->definition == FUNCTOR_colon2 )   /* Name:Value */
    { Word np, vp;

      deRef2(&f->arguments[0], np);
      if ( is_key(*np) )
      { *name = *np;
	deRef2(&f->arguments[1], vp);
	*value = linkVal(vp);

	return TRUE;
      }
    } else if ( arityFunctor(f->definition) == 1 ) /* Name(Value) */
    { Word vp;

      *name = nameFunctor(f->definition);
      deRef2(&f->arguments[0], vp);
      *value = linkVal(vp);
      return TRUE;
    }
  }

  return FALSE;				/* type error */
}



		 /*******************************
		 *	 FOREIGN SUPPORT	*
		 *******************************/

int
PL_is_map(term_t t)
{ GET_LD
  Word p = valTermRef(t);

  deRef(p);
  if ( isTerm(*p) )
  { Functor f = valueTerm(*p);
    FunctorDef fd = valueFunctor(f->definition);

    if ( fd->name == ATOM_map &&
	 fd->arity%2 == 1 &&
	 map_ordered(f->arguments+1, fd->arity/2 PASS_LD) )
      return TRUE;
  }

  return FALSE;
}


int
PL_get_map_ex(term_t data, term_t class, term_t map)
{ GET_LD

  if ( PL_is_map(data) )
  { PL_put_term(map, data);
    return TRUE;
  }

  if ( PL_is_list(data) )
  { intptr_t len = lengthList(data, TRUE);
    Word m, ap, tail;

    if ( len < 0 )
      return FALSE;			/* not a proper list */
    if ( !(m = allocGlobal(len*2+2)) )
      return FALSE;			/* global overflow */
    ap = m;
    *ap++ = map_functor(len*2+1);
    if ( class )
      *ap++ = linkVal(valTermRef(class));
    else
      setVar(*ap++);

    tail = valTermRef(data);
    deRef(tail);
    while( isList(*tail) )
    { Word head = HeadList(tail);

      if ( !get_name_value(head, ap, ap+1 PASS_LD) )
      { gTop = m;
	PL_type_error("name-value", pushWordAsTermRef(head));
	popTermRef();
	return FALSE;
      }

      tail = TailList(tail);
      deRef(tail);
      ap += 2;
    }

    if ( map_order(m PASS_LD) )
    { *valTermRef(map) = consPtr(m, TAG_COMPOUND|STG_GLOBAL);
      return TRUE;
    }

    gTop = m;
  }					/* TBD: {name:value, ...} */

  return PL_type_error("map-data", data);
}


		 /*******************************
		 *       PROLOG PREDICATES	*
		 *******************************/

/** is_map(@Term, ?Class)

True if Term is a map that belongs to Class.

@tbd What if Term has a variable class?
*/

static
PRED_IMPL("is_map", 2, is_map, 0)
{ PRED_LD
  Word p = valTermRef(A1);

  deRef(p);
  if ( isTerm(*p) )
  { Functor f = valueTerm(*p);
    FunctorDef fd = valueFunctor(f->definition);

    if ( fd->name == ATOM_map &&
	 fd->arity%2 == 1 &&
	 map_ordered(f->arguments+1, fd->arity/2 PASS_LD) )
      return unify_ptrs(&f->arguments[0], valTermRef(A2),
			ALLOW_GC|ALLOW_SHIFT PASS_LD);
  }

  return FALSE;
}


/** map_get(+Map, ?Name, ?Value)

True when Name is associated with Value in Map. If Name is unbound, this
predicate is true for all Name/Value  pairs   in  the  map. The order in
which these pairs are enumerated is _undefined_.
*/

static
PRED_IMPL("map_get", 3, map_get, PL_FA_NONDETERMINISTIC)
{ PRED_LD
  int i;
  word map;

  switch( CTX_CNTRL )
  { case FRG_FIRST_CALL:
    { Word np = valTermRef(A2);

      if ( !get_map_ex(A1, &map, FALSE PASS_LD) )
	return FALSE;

      deRef(np);
      if ( is_key(*np) )
      { Word vp;

	if ( (vp=map_lookup_ptr(map, *np PASS_LD)) )
	  return unify_ptrs(vp, valTermRef(A3), ALLOW_GC|ALLOW_SHIFT PASS_LD);

	return FALSE;
      }
      if ( canBind(*np) )
	i = 1;
      goto search;
    }
    case FRG_REDO:
    { Functor f;
      int arity;
      fid_t fid;
      Word p;

      i = (int)CTX_INT + 2;
      p = valTermRef(A1);
      deRef(p);
      map = *p;

    search:
      f = valueTerm(map);
      arity = arityFunctor(f->definition);

      if ( (fid=PL_open_foreign_frame()) )
      { for( ; i < arity; i += 2 )
	{ Word np;

	  deRef2(&f->arguments[i], np);	/* TBD: check type */
	  if ( unify_ptrs(&f->arguments[i+1], valTermRef(A3),
			  ALLOW_GC|ALLOW_SHIFT PASS_LD) &&
	       _PL_unify_atomic(A2, *np) )
	  { PL_close_foreign_frame(fid);

	    if ( i+2 < arity )
	      ForeignRedoInt(i);
	    else
	      return TRUE;
	  } else if ( exception_term )
	  { PL_close_foreign_frame(fid);
	    return FALSE;
	  }
	  PL_rewind_foreign_frame(fid);
	}
	PL_close_foreign_frame(fid);
      }
      return FALSE;
    }
    default:
      return TRUE;
  }
}


/** map_create(-Map, ?Class, +Data) is det.

Map represents the name-value pairs  in  Data.   If  Data  is a map, Map
unified  with  Data.  Otherwise,  a  new    Map   is  created.  Suitable
representations for Data are:

  - Class{Name:Value, ...}
  - {Name:Value, ...}
  - [Name=Value, ...]
  - [Name-Value, ...]
  - [Name(Value), ...]
*/


static
PRED_IMPL("map_create", 3, map_create, 0)
{ PRED_LD
  term_t m = PL_new_term_ref();

  if ( PL_get_map_ex(A3, A2, m) )
    return PL_unify(A1, m);

  return FALSE;
}


/** map_put(+Map0, +Map1, -Map)

True when Map is a copy of Map0 where values from Map1 replace or extend
the value set of Map0.
*/

static
PRED_IMPL("map_put", 3, map_put, 0)
{ PRED_LD
  word m1, m2;
  fid_t fid = PL_open_foreign_frame();

retry:

  if ( get_map_ex(A1, &m1, TRUE PASS_LD) &&
       get_map_ex(A2, &m2, TRUE PASS_LD) )
  { Functor f2 = valueTerm(m2);
    int arity = arityFunctor(f2->definition);
    word new;
    int rc;

    if ( (rc = map_put(m1, arity/2, &f2->arguments[1], &new PASS_LD)) == TRUE )
    { term_t t = PL_new_term_ref();

      *valTermRef(t) = new;
      return PL_unify(A3, t);
    } else
    { assert(rc == GLOBAL_OVERFLOW);
      if ( ensureGlobalSpace(0, ALLOW_GC) == TRUE )
      { PL_rewind_foreign_frame(fid);
	goto retry;
      }
    }
  }

  return FALSE;
}

/** map_put(+Map0, +Name, +Value, -Map)

True when Map is a copy of Map0 with Name Value added or replaced.
*/

static int
get_name_ex(term_t t, Word np ARG_LD)
{ Word p = valTermRef(t);

  deRef(p);
  if ( is_key(*p) )
  { *np = *p;
    return TRUE;
  }

  return PL_type_error("map-key", t);
}


static
PRED_IMPL("map_put", 4, map_put, 0)
{ PRED_LD
  word m1;
  term_t av = PL_new_term_refs(2);
  fid_t fid = PL_open_foreign_frame();

retry:
  if ( get_map_ex(A1, &m1, TRUE PASS_LD) &&
       get_name_ex(A2, valTermRef(av) PASS_LD) &&
       PL_put_term(av+1, A3) )
  { word new;
    int rc;

    if ( (rc = map_put(m1, 1, valTermRef(av), &new PASS_LD)) == TRUE )
    { term_t t = PL_new_term_ref();

      *valTermRef(t) = new;
      return PL_unify(A4, t);
    } else
    { assert(rc == GLOBAL_OVERFLOW);
      if ( ensureGlobalSpace(0, ALLOW_GC) == TRUE )
      { PL_rewind_foreign_frame(fid);
	goto retry;
      }
    }
  }

  return FALSE;
}




		 /*******************************
		 *      PUBLISH PREDICATES	*
		 *******************************/

BeginPredDefs(map)
  PRED_DEF("is_map",     2, is_map,     0)
  PRED_DEF("map_create", 3, map_create, 0)
  PRED_DEF("map_put",    3, map_put,    0)
  PRED_DEF("map_put",    4, map_put,    0)
  PRED_DEF("map_get",    3, map_get,    PL_FA_NONDETERMINISTIC)
EndPredDefs
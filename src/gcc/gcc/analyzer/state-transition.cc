/* Classes for tracking pertinent events that happen along
   an execution path.
   Copyright (C) 2026 Free Software Foundation, Inc.
   Contributed by David Malcolm <dmalcolm@redhat.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "analyzer/common.h"

#include "tree-diagnostic.h"

#include "gimple-pretty-print.h"
#include "gimple-iterator.h"
#include "tree-cfg.h"
#include "tree-dfa.h"
#include "fold-const.h"
#include "cgraph.h"
#include "text-art/dump.h"
#include "text-art/tree-widget.h"

#include "analyzer/ops.h"
#include "analyzer/call-details.h"
#include "analyzer/exploded-graph.h"
#include "analyzer/checker-path.h"
#include "analyzer/impl-sm-context.h"
#include "analyzer/constraint-manager.h"
#include "analyzer/call-summary.h"
#include "analyzer/call-info.h"
#include "analyzer/analysis-plan.h"
#include "analyzer/callsite-expr.h"
#include "analyzer/state-transition.h"

#if ENABLE_ANALYZER

namespace ana {

// class state_transition

DEBUG_FUNCTION void
state_transition::dump () const
{
  tree_dump_pretty_printer pp (stderr);
  dump_to_pp (&pp);
  pp_newline (&pp);
}

std::unique_ptr<state_transition>
state_transition::make (const region *src_reg,
			tree src_reg_expr,
			const region *dst_reg,
			tree dst_reg_expr)
{
  gcc_assert (src_reg != dst_reg);
  gcc_assert (dst_reg);

  if (!src_reg)
    return std::make_unique<state_transition_origin> (dst_reg_expr);

  if (src_reg->get_parent_region () ==  dst_reg->get_parent_region ())
    if (tree src_decl =  src_reg->maybe_get_decl ())
      if (tree dst_decl =  dst_reg->maybe_get_decl ())
	{
	  if (TREE_CODE (src_decl) == SSA_NAME
	      && TREE_CODE (dst_decl) == SSA_NAME
	      && SSA_NAME_VAR (src_decl)
	      && SSA_NAME_VAR (src_decl) == SSA_NAME_VAR (dst_decl))
	    {
	      /* Avoid printing "copying value from 'y' to 'y'.  */
	      return nullptr;
	    }
	}

  if (printable_expr_p (src_reg_expr))
    {
      if (printable_expr_p (dst_reg_expr))
	return std::make_unique<state_transition_copy> (src_reg_expr,
							dst_reg_expr);
      else
	return std::make_unique<state_transition_use> (src_reg_expr);
    }
  else
    return nullptr;
}

diagnostics::paths::event_id_t
state_transition::get_src_event_id () const
{
  if (!m_prev_state_transition)
    return diagnostics::paths::event_id_t ();
  return m_prev_state_transition->m_event_id;
}
// class state_transition_origin : public state_transition

std::unique_ptr<state_transition>
state_transition_origin::clone () const
{
  return std::make_unique<state_transition_origin> (m_dst_reg_expr);
}

void
state_transition_origin::dump_to_pp (pretty_printer *pp) const
{
  pp_printf (pp, "state_transition_origin (dst: %qE)",
	     m_dst_reg_expr);
}

// class state_transition_at_call : public state_transition

std::unique_ptr<state_transition>
state_transition_at_call::clone () const
{
  return std::make_unique<state_transition_at_call> (m_expr);
}

void
state_transition_at_call::dump_to_pp (pretty_printer *pp) const
{
  callsite_expr_element e (m_expr);
  pp_printf (pp, "state_transition_at_call (callsite_expr: %e)", &e);
}

// class state_transition_at_return : public state_transition

std::unique_ptr<state_transition>
state_transition_at_return::clone () const
{
  return std::make_unique<state_transition_at_return> ();
}

void
state_transition_at_return::dump_to_pp (pretty_printer *pp) const
{
  pp_printf (pp, "state_transition_at_return");
}

// class state_transition_copy : public state_transition

std::unique_ptr<state_transition>
state_transition_copy::clone () const
{
  return std::make_unique<state_transition_copy> (m_src_reg_expr,
						  m_dst_reg_expr);
}

void
state_transition_copy::dump_to_pp (pretty_printer *pp) const
{
  pp_printf (pp, "state_transition_copy (src: %qE, dst: %qE)",
	     m_src_reg_expr,
	     m_dst_reg_expr);
}

// class state_transition_use : public state_transition

std::unique_ptr<state_transition>
state_transition_use::clone () const
{
  return std::make_unique<state_transition_use> (m_src_reg_expr);
}

void
state_transition_use::dump_to_pp (pretty_printer *pp) const
{
  pp_printf (pp, "state_transition_use (src: %qE)",
	     m_src_reg_expr);
}

} // namespace ana

#endif /* #if ENABLE_ANALYZER */

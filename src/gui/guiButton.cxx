// Filename: guiButton.cxx
// Created by:  cary (30Oct00)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) 2001, Disney Enterprises, Inc.  All rights reserved
//
// All use of this software is subject to the terms of the Panda 3d
// Software license.  You should have received a copy of this license
// along with this source code; you will also find a current copy of
// the license at http://www.panda3d.org/license.txt .
//
// To contact the maintainers of this program write to
// panda3d@yahoogroups.com .
//
////////////////////////////////////////////////////////////////////

#include "guiButton.h"
#include "config_gui.h"

#include <throw_event.h>

#include <map>

typedef map<const MouseWatcherRegion*, GuiButton*> ButtonMap;
static ButtonMap buttons;
static bool bAddedHooks = false;

TypeHandle GuiButton::_type_handle;

static inline GuiButton *
find_in_buttons_map(const MouseWatcherRegion* rgn) {
  ButtonMap::iterator i = buttons.find(rgn);
  if (i == buttons.end())
    return (GuiButton*)0L;
  return (*i).second;
}

inline void GetExtents(GuiLabel* v, GuiLabel* w, GuiLabel* x, GuiLabel* y,
               GuiLabel* z, float& l, float& r, float& b, float& t) {
  float l1, l2, r1, r2, b1, b2, t1, t2;
  v->get_extents(l1, r1, b1, t1);
  w->get_extents(l2, r2, b2, t2);
  l1 = (l1<l2)?l1:l2;
  r1 = (r1<r2)?r2:r1;
  b1 = (b1<b2)?b1:b2;
  t1 = (t1<t2)?t2:t1;
  if (x != (GuiLabel*)0L) {
    x->get_extents(l2, r2, b2, t2);
    l1 = (l1<l2)?l1:l2;
    r1 = (r1<r2)?r2:r1;
    b1 = (b1<b2)?b1:b2;
    t1 = (t1<t2)?t2:t1;
  }
  if (y != (GuiLabel*)0L) {
    y->get_extents(l2, r2, b2, t2);
    l1 = (l1<l2)?l1:l2;
    r1 = (r1<r2)?r2:r1;
    b1 = (b1<b2)?b1:b2;
    t1 = (t1<t2)?t2:t1;
  }
  if (z != (GuiLabel*)0L) {
    z->get_extents(l2, r2, b2, t2);
    l = (l1<l2)?l1:l2;
    r = (r1<r2)?r2:r1;
    b = (b1<b2)?b1:b2;
    t = (t1<t2)?t2:t1;
  }
}

inline void my_throw(GuiManager* mgr, const string& name,
             const EventParameter& p) {
  throw_event(name, p);
  if (mgr != (GuiManager*)0L)
    throw_event_directly(*(mgr->get_private_handler()), name, p);
}

inline void my_throw(GuiManager* mgr, const string& name,
             const EventParameter& p1,
             const EventParameter& p2) {
  throw_event(name, p1, p2);
  if (mgr != (GuiManager*)0L)
    throw_event_directly(*(mgr->get_private_handler()), name, p1, p2);
}

static void enter_button(CPT_Event e) {
  const MouseWatcherRegion* rgn = DCAST(MouseWatcherRegion, e->get_parameter(0).get_ptr());
  GuiButton* val = find_in_buttons_map(rgn);
  if (val == (GuiButton*)0L)
    return;  // this one wasn't for us
  val->test_ref_count_integrity();
  val->enter();
}

static void exit_button(CPT_Event e) {
  const MouseWatcherRegion* rgn = DCAST(MouseWatcherRegion, e->get_parameter(0).get_ptr());
  GuiButton* val = find_in_buttons_map(rgn);
  if (val == (GuiButton *)0L)
    return;  // this one wasn't for us
  val->test_ref_count_integrity();
  val->exit();
}

static void click_button_down(CPT_Event e) {
  const MouseWatcherRegion* rgn = DCAST(MouseWatcherRegion, e->get_parameter(0).get_ptr());
  string button = e->get_parameter(1).get_string_value();
  if (!(button == "mouse1" || button == "mouse2" || button == "mouse3"))
    return;
  GuiButton* val = find_in_buttons_map(rgn);
  if (val == (GuiButton *)0L)
    return;  // this one wasn't for us
  val->test_ref_count_integrity();
  val->down();
}

static void click_button_up(CPT_Event e) {
  const MouseWatcherRegion* rgn = DCAST(MouseWatcherRegion, e->get_parameter(0).get_ptr());
  string button = e->get_parameter(1).get_string_value();
  if (!(button == "mouse1" || button == "mouse2" || button == "mouse3"))
    return;
  GuiButton* val = find_in_buttons_map(rgn);
  if (val == (GuiButton *)0L)
    return;  // this one wasn't for us
  val->test_ref_count_integrity();
  val->up();
}

void GuiButton::switch_state(GuiButton::States nstate) {
  if (_mgr == (GuiManager*)0L) {
    _state = nstate;
    return;
  }

  test_ref_count_integrity();
  States ostate = _state;
  // cleanup old state
  switch (_state) {
  case NONE:
    break;
  case UP:
    if (_mgr->has_label(_up))
      _mgr->remove_label(_up);
    break;
  case UP_ROLLOVER:
    if (_mgr->has_label(_up_rollover))
      _mgr->remove_label(_up_rollover);
    break;
  case DOWN:
    if (_mgr->has_label(_down))
      _mgr->remove_label(_down);
    break;
  case DOWN_ROLLOVER:
    if (_mgr->has_label(_down_rollover))
      _mgr->remove_label(_down_rollover);
    break;
  case INACTIVE:
    if (_inactive != (GuiLabel*)0L)
      if (_mgr->has_label(_inactive))
    _mgr->remove_label(_inactive);
    break;
  case INACTIVE_ROLLOVER:
    if (_inactive != (GuiLabel*)0L)
      if (_mgr->has_label(_inactive))
    _mgr->remove_label(_inactive);
    break;
  default:
    gui_cat->warning() << "switching away from invalid state (" << (int)_state
               << ")" << endl;
  }
  _state = nstate;
  const EventParameter paramthis = EventParameter(this);
  // deal with new state
  switch (_state) {
  case NONE:
    if (_mgr->has_region(_rgn))
      _mgr->remove_region(_rgn);
    _rgn->set_suppress_below(false);
    break;
  case UP:
    if (!(_mgr->has_region(_rgn)))
      _mgr->add_region(_rgn);
    if (_alt_root.is_null()) {
      if (!(_mgr->has_label(_up)))
    _mgr->add_label(_up);
    } else {
      if (!(_mgr->has_label(_up)))
    _mgr->add_label(_up, _alt_root);
    }
    if (!_up_event.empty()) {
#ifdef _DEBUG
      if (gui_cat.is_debug())
    gui_cat->debug() << "throwing _up_event '" << _up_event << "'" << endl;
#endif /* _DEBUG */
      my_throw(_mgr, _up_event, paramthis);
#ifdef _DEBUG
    } else if (gui_cat.is_debug())
    gui_cat->debug() << "_up_event is empty!" << endl;
#else /* _DEBUG */
    }
#endif /* _DEBUG */
    _rgn->set_suppress_below(true);
    break;
  case UP_ROLLOVER:
    if (!(_mgr->has_region(_rgn)))
      _mgr->add_region(_rgn);
    if (_up_rollover != (GuiLabel*)0L) {
      if (_alt_root.is_null()) {
    if (!(_mgr->has_label(_up_rollover)))
      _mgr->add_label(_up_rollover);
      } else {
    if (!(_mgr->has_label(_up_rollover)))
      _mgr->add_label(_up_rollover, _alt_root);
      }
      if (!_up_rollover_event.empty()) {
#ifdef _DEBUG
    if (gui_cat.is_debug())
      gui_cat->debug() << "throwing _up_rollover_event '"
               << _up_rollover_event << "'" << endl;
#endif /* _DEBUG */
    my_throw(_mgr, _up_rollover_event, paramthis);
#ifdef _DEBUG
      } else if (gui_cat.is_debug())
      gui_cat->debug() << "_up_rollover_event is empty!" << endl;
#else /* _DEBUG */
      }
#endif /* _DEBUG */
    } else {
      if (_alt_root.is_null()) {
    if (!(_mgr->has_label(_up)))
      _mgr->add_label(_up);
      } else {
    if (!(_mgr->has_label(_up)))
      _mgr->add_label(_up, _alt_root);
      }
      if (!_up_event.empty()) {
#ifdef _DEBUG
    if (gui_cat.is_debug())
      gui_cat->debug() << "throwing _up_event '" << _up_event << "'"
               << endl;
#endif /* _DEBUG */
    my_throw(_mgr, _up_event, paramthis);
#ifdef _DEBUG
      } else if (gui_cat.is_debug())
    gui_cat->debug() << "_up_event is empty!" << endl;
#else /* _DEBUG */
      }
#endif /* _DEBUG */
      _state = UP;
    }
    _rgn->set_suppress_below(true);
    if ((ostate == UP) &&
    (_rollover_functor != (GuiBehavior::BehaviorFunctor*)0L))
      _rollover_functor->doit(this);
    break;
  case DOWN:
    if (!(_mgr->has_region(_rgn)))
      _mgr->add_region(_rgn);
    if (_alt_root.is_null()) {
      if (!(_mgr->has_label(_down)))
    _mgr->add_label(_down);
    } else {
      if (!(_mgr->has_label(_down)))
    _mgr->add_label(_down, _alt_root);
    }
    if (!_down_event.empty()) {
#ifdef _DEBUG
      if (gui_cat.is_debug())
    gui_cat->debug() << "throwing _down_event '" << _down_event << "'"
             << endl;
#endif /* _DEBUG */
      my_throw(_mgr, _down_event, paramthis);
#ifdef _DEBUG
    } else if (gui_cat.is_debug())
      gui_cat->debug() << "_down_event is empty!" << endl;
#else /* _DEBUG */
    }
#endif /* _DEBUG */
    _rgn->set_suppress_below(true);
    break;
  case DOWN_ROLLOVER:
    if (!(_mgr->has_region(_rgn)))
      _mgr->add_region(_rgn);
    if (_down_rollover != (GuiLabel*)0L) {
      if (_alt_root.is_null()) {
    if (!(_mgr->has_label(_down_rollover)))
      _mgr->add_label(_down_rollover);
      } else {
    if (!(_mgr->has_label(_down_rollover)))
      _mgr->add_label(_down_rollover, _alt_root);
      }
      if (!_down_rollover_event.empty()) {
#ifdef _DEBUG
    if (gui_cat.is_debug())
      gui_cat->debug() << "throwing _down_rollover_event '"
               << _down_rollover_event << "'" << endl;
#endif /* _DEBUG */
    my_throw(_mgr, _down_rollover_event, paramthis);
#ifdef _DEBUG
      } else if (gui_cat.is_debug())
    gui_cat->debug() << "_down_rollover_event is empty!" << endl;
#else /* _DEBUG */
      }
#endif /* _DEBUG */
    } else {
      if (_alt_root.is_null()) {
    if (!(_mgr->has_label(_down)))
      _mgr->add_label(_down);
      } else {
    if (!(_mgr->has_label(_down)))
      _mgr->add_label(_down, _alt_root);
      }
      if (!_down_event.empty()) {
#ifdef _DEBUG
    if (gui_cat.is_debug())
      gui_cat->debug() << "throwing _down_event '" << _down_event << "'"
               << endl;
#endif /* _DEBUG */
    my_throw(_mgr, _down_event, paramthis);
#ifdef _DEBUG
      } else if (gui_cat.is_debug())
    gui_cat->debug() << "_down_event is empty!" << endl;
#else /* _DEBUG */
      }
#endif /* _DEBUG */
      _state = DOWN;
    }
    _rgn->set_suppress_below(true);
    break;
  case INACTIVE:
    if (_mgr->has_region(_rgn))
      _mgr->remove_region(_rgn);
    if (_inactive != (GuiLabel*)0L) {
      if (_alt_root.is_null()) {
    if (!(_mgr->has_label(_inactive)))
      _mgr->add_label(_inactive);
      } else {
    if (!(_mgr->has_label(_inactive)))
      _mgr->add_label(_inactive, _alt_root);
      }
      if (!_inactive_event.empty()) {
#ifdef _DEBUG
    if (gui_cat.is_debug())
      gui_cat->debug() << "throwing _inactive_event '" << _inactive_event
               << "'" << endl;
#endif /* _DEBUG */
    my_throw(_mgr, _inactive_event, paramthis);
      }
    }
    _rgn->set_suppress_below(false);
    break;
  case INACTIVE_ROLLOVER:
    if (_mgr->has_region(_rgn))
      _mgr->remove_region(_rgn);
    if (_inactive != (GuiLabel*)0L) {
      if (_alt_root.is_null()) {
    if (!(_mgr->has_label(_inactive)))
      _mgr->add_label(_inactive);
      } else {
    if (!(_mgr->has_label(_inactive)))
      _mgr->add_label(_inactive, _alt_root);
      }
      if (!_inactive_event.empty()) {
#ifdef _DEBUG
    if (gui_cat.is_debug())
      gui_cat->debug() << "throwing _inactive_event '" << _inactive_event
               << "'" << endl;
#endif /* _DEBUG */
    my_throw(_mgr, _inactive_event, paramthis);
      }
    }
    _rgn->set_suppress_below(false);
    break;
  default:
    gui_cat->warning() << "switched to invalid state (" << (int)_state << ")"
               << endl;
  }
  if (_state != NONE)
    _mgr->recompute_priorities();
}

void GuiButton::recompute_frame(void) {
  GuiBehavior::recompute_frame();
  _up->recompute();
  _down->recompute();
  if (_up_rollover != (GuiLabel*)0L)
    _up_rollover->recompute();
  if (_down_rollover != (GuiLabel*)0L)
    _down_rollover->recompute();
  if (_inactive != (GuiLabel*)0L)
    _inactive->recompute();
  this->adjust_region();
}

void GuiButton::adjust_region(void) {
  GetExtents(_up, _down, _up_rollover, _down_rollover, _inactive, _left,
         _right, _bottom, _top);
  GuiBehavior::adjust_region();
  _rgn->set_frame(_left, _right, _bottom, _top);
}

void GuiButton::set_priority(GuiLabel* l, GuiItem::Priority p) {
  _up->set_priority(l, ((p==P_Low)?GuiLabel::P_LOWER:GuiLabel::P_HIGHER));
  _down->set_priority(l, ((p==P_Low)?GuiLabel::P_LOWER:GuiLabel::P_HIGHER));
  if (_up_rollover != (GuiLabel*)0L)
    _up_rollover->set_priority(l, ((p==P_Low)?GuiLabel::P_LOWER:
                   GuiLabel::P_HIGHER));
  if (_down_rollover != (GuiLabel*)0L)
    _down_rollover->set_priority(l, ((p==P_Low)?GuiLabel::P_LOWER:
                     GuiLabel::P_HIGHER));
  if (_inactive != (GuiLabel*)0L)
    _inactive->set_priority(l, ((p==P_Low)?GuiLabel::P_LOWER:
                GuiLabel::P_HIGHER));
  GuiItem::set_priority(l, p);
}

void GuiButton::behavior_up(CPT_Event e) {
  const GuiButton* button = DCAST(GuiButton, e->get_parameter(0).get_ptr());
#ifdef _DEBUG
  if (gui_cat.is_debug())
    gui_cat->debug() << "behavior_up (0x" << (void*)button << ")" << endl;
#endif /* _DEBUG */
  button->run_button_up();
}

void GuiButton::behavior_down(CPT_Event e) {
  const GuiButton* button = DCAST(GuiButton, e->get_parameter(0).get_ptr());
#ifdef _DEBUG
  if (gui_cat.is_debug())
    gui_cat->debug() << "behavior_down (0x" << (void*)button << ")" << endl;
#endif /* _DEBUG */
  button->run_button_down();
}

void GuiButton::run_button_up(void) const {
#ifdef _DEBUG
  if (gui_cat.is_debug())
    gui_cat->debug() << "run_button_up (0x" << (void*)this << " '"
             << this->get_name() << "')" << endl;
#endif /* _DEBUG */
  if ((_eh == (EventHandler*)0L) || (!(this->_behavior_running)))
    return;
#ifdef _DEBUG
  if (gui_cat.is_debug())
    gui_cat->debug() << "doing work" << endl;
#endif /* _DEBUG */
  _mgr->get_private_handler()->remove_hook(_up_event, GuiButton::behavior_up);
  _mgr->get_private_handler()->remove_hook(_up_rollover_event,
                                           GuiButton::behavior_up);
  const EventParameter paramthis = EventParameter(this);
  if (!_behavior_event.empty()) {
    if (_have_event_param) {
#ifdef _DEBUG
      if (gui_cat.is_debug())
    gui_cat->debug() << "throwing behavior event '" << _behavior_event
             << "' with parameter (" << _event_param << ")"
             << endl;
#endif /* _DEBUG */
      const EventParameter paramparam = EventParameter(_event_param);
      my_throw(_mgr, _behavior_event, paramthis, paramparam);
    } else {
#ifdef _DEBUG
      if (gui_cat.is_debug())
    gui_cat->debug() << "throwing behavior event '" << _behavior_event
             << "'" << endl;
#endif /* _DEBUG */
      my_throw(_mgr, _behavior_event, paramthis);
    }
  }
  if (_behavior_functor != (GuiBehavior::BehaviorFunctor*)0L)
    _behavior_functor->doit((GuiBehavior*)this);
}

void GuiButton::run_button_down(void) const {
#ifdef _DEBUG
  if (gui_cat.is_debug())
    gui_cat->debug() << "run_button_down (0x" << (void*)this << " '"
             << this->get_name() << "')" << endl;
#endif /* _DEBUG */
  if ((_eh == (EventHandler*)0L) || (!(this->_behavior_running)))
    return;
#ifdef _DEBUG
  if (gui_cat.is_debug())
    gui_cat->debug() << "doing work, up_event is '" << _up_event << "' '"
             << _up_rollover_event << "'" << endl;
#endif /* _DEBUG */
  _mgr->get_private_handler()->add_hook(_up_event, GuiButton::behavior_up);
  _mgr->get_private_handler()->add_hook(_up_rollover_event,
                                        GuiButton::behavior_up);
}

GuiButton::GuiButton(const string& name, GuiLabel* up, GuiLabel* down)
  : GuiBehavior(name), _up(up), _up_rollover((GuiLabel*)0L), _down(down),
    _down_rollover((GuiLabel*)0L), _inactive((GuiLabel*)0L),
    _up_event("GuiButton-up"), _up_rollover_event(""),
    _down_event("GuiButton-down"), _down_rollover_event(""),
    _inactive_event(""), _up_scale(up->get_scale()), _upr_scale(1.),
    _down_scale(down->get_scale()), _downr_scale(1.), _inactive_scale(1.),
    _state(GuiButton::NONE), _have_event_param(false), _event_param(0),
    _behavior_functor((GuiBehavior::BehaviorFunctor*)0L),
    _rollover_functor((GuiBehavior::BehaviorFunctor*)0L) {
  GetExtents(up, down, _up_rollover, _down_rollover, _inactive, _left, _right,
         _bottom, _top);
  _rgn = new MouseWatcherRegion("button-" + name, _left, _right, _bottom,
                _top);
  _rgn->set_suppress_below(true);
  buttons[this->_rgn.p()] = this;
}

GuiButton::GuiButton(const string& name, GuiLabel* up, GuiLabel* down,
             GuiLabel* inactive)
  : GuiBehavior(name), _up(up), _up_rollover((GuiLabel*)0L), _down(down),
    _down_rollover((GuiLabel*)0L), _inactive(inactive),
    _up_event("GuiButton-up"), _up_rollover_event(""),
    _down_event("GuiButton-down"), _down_rollover_event(""),
    _inactive_event("GuiButton-inactive"), _up_scale(up->get_scale()),
    _upr_scale(1.), _down_scale(down->get_scale()), _downr_scale(1.),
    _inactive_scale(inactive->get_scale()), _state(GuiButton::NONE),
    _have_event_param(false), _event_param(0),
    _behavior_functor((GuiBehavior::BehaviorFunctor*)0L),
    _rollover_functor((GuiBehavior::BehaviorFunctor*)0L) {
  GetExtents(up, down, _up_rollover, _down_rollover, inactive, _left, _right,
         _bottom, _top);
  _rgn = new MouseWatcherRegion("button-" + name, _left, _right, _bottom,
                _top);
  _rgn->set_suppress_below(true);
  buttons[this->_rgn.p()] = this;
}

GuiButton::GuiButton(const string& name, GuiLabel* up, GuiLabel* up_roll,
             GuiLabel* down, GuiLabel* down_roll, GuiLabel* inactive)
  : GuiBehavior(name), _up(up), _up_rollover(up_roll), _down(down),
    _down_rollover(down_roll), _inactive(inactive), _up_event("GuiButton-up"),
    _up_rollover_event("GuiButton-up-rollover"), _down_event("GuiButton-down"),
    _down_rollover_event("GuiButton-down-rollover"),
    _inactive_event("GuiButton-inactive"), _up_scale(up->get_scale()),
    _upr_scale(up_roll->get_scale()), _down_scale(down->get_scale()),
    _downr_scale(down_roll->get_scale()),
    _inactive_scale(inactive->get_scale()), _state(GuiButton::NONE),
    _have_event_param(false), _event_param(0),
    _behavior_functor((GuiBehavior::BehaviorFunctor*)0L),
    _rollover_functor((GuiBehavior::BehaviorFunctor*)0L) {
  GetExtents(up, down, up_roll, down_roll, inactive, _left, _right, _bottom,
         _top);
  _rgn = new MouseWatcherRegion("button-" + name, _left, _right, _bottom,
                _top);
  _rgn->set_suppress_below(true);
  buttons[this->_rgn.p()] = this;
}

GuiButton::~GuiButton(void) {
  this->unmanage();

  // Remove the names from the buttons map, so we don't end up with
  // an invalid pointer.
  buttons.erase(this->_rgn.p());
  if (gui_cat.is_debug())
    gui_cat->debug() << "erased from button map" << endl;
  if ((buttons.size() == 0) && bAddedHooks) {
    /*
    _eh->remove_hook("gui-enter", enter_button);
    _eh->remove_hook("gui-exit" + get_name(), exit_button);
    _eh->remove_hook("gui-button-press", click_button_down);
    _eh->remove_hook("gui-button-release", click_button_up);
    bAddedHooks = false;
    */
  }

  if (_behavior_functor != (GuiBehavior::BehaviorFunctor*)0L)
    _behavior_functor.clear();
  if (gui_cat.is_debug())
    gui_cat->debug() << "cleared behavior functor" << endl;
  if (_rollover_functor != (GuiBehavior::BehaviorFunctor*)0L)
    _rollover_functor.clear();
  if (gui_cat.is_debug())
    gui_cat->debug() << "cleared rollover functor" << endl;
}

void GuiButton::manage(GuiManager* mgr, EventHandler& eh) {
  if (!bAddedHooks) {
    mgr->get_private_handler()->add_hook("gui-enter", enter_button);
    mgr->get_private_handler()->add_hook("gui-exit", exit_button);
    mgr->get_private_handler()->add_hook("gui-button-press",
                                         click_button_down);
    mgr->get_private_handler()->add_hook("gui-button-release",
                                         click_button_up);
    bAddedHooks = true;
  }
  if (_mgr == (GuiManager*)0L) {
    GuiBehavior::manage(mgr, eh);
    if (_behavior_running)
      this->start_behavior();
    switch_state(UP);
  } else
    gui_cat->warning() << "tried to manage button (0x" << (void*)this
               << ") that is already managed" << endl;
}

void GuiButton::manage(GuiManager* mgr, EventHandler& eh, Node* n) {
  if (!bAddedHooks) {
    mgr->get_private_handler()->add_hook("gui-enter", enter_button);
    mgr->get_private_handler()->add_hook("gui-exit", exit_button);
    mgr->get_private_handler()->add_hook("gui-button-press",
                                         click_button_down);
    mgr->get_private_handler()->add_hook("gui-button-release",
                                         click_button_up);
    bAddedHooks = true;
  }
  if (_mgr == (GuiManager*)0L) {
    GuiBehavior::manage(mgr, eh, n);
    if (_behavior_running)
      this->start_behavior();
    switch_state(UP);
  } else
    gui_cat->warning() << "tried to manage button (0x" << (void*)this
               << ") that is already managed" << endl;
}

void GuiButton::unmanage(void) {
  if (gui_cat.is_debug())
    gui_cat->debug() << "in GuiButton::unmanage(0x" << (void*)this << ")"
                     << endl;
  if (_mgr != (GuiManager*)0L)
    if (_mgr->has_region(_rgn)) {
      _mgr->remove_region(_rgn);
      if (gui_cat.is_debug())
    gui_cat->debug() << "removed region" << endl;
    }
  if (_behavior_running) {
    this->stop_behavior();
    if (gui_cat.is_debug())
      gui_cat->debug() << "behavior stopped" << endl;
  }
  if (gui_cat.is_debug())
    gui_cat->debug() << "switching state to NONE" << endl;
  _state = NONE;
  if (_mgr != (GuiManager*)0L) {
    if (_mgr->has_label(_up))
      _mgr->remove_label(_up);
    if (_mgr->has_label(_up_rollover))
      _mgr->remove_label(_up_rollover);
    if (_mgr->has_label(_down))
      _mgr->remove_label(_down);
    if (_mgr->has_label(_down_rollover))
      _mgr->remove_label(_down_rollover);
    if (_mgr->has_label(_inactive))
      _mgr->remove_label(_inactive);
  }
  if (gui_cat.is_debug())
    gui_cat->debug() << "back from switching state to NONE" << endl;
  GuiBehavior::unmanage();
  if (gui_cat.is_debug())
    gui_cat->debug() << "back from parent unmanage" << endl;
}

int GuiButton::freeze() {
  _up->freeze();
  _down->freeze();
  if (_up_rollover != (GuiLabel*)0L)
    _up_rollover->freeze();
  if (_down_rollover != (GuiLabel*)0L)
    _down_rollover->freeze();
  if (_inactive != (GuiLabel*)0L)
    _inactive->freeze();

  return 0;
}

int GuiButton::thaw() {
  _up->thaw();
  _down->thaw();
  if (_up_rollover != (GuiLabel*)0L)
    _up_rollover->thaw();
  if (_down_rollover != (GuiLabel*)0L)
    _down_rollover->thaw();
  if (_inactive != (GuiLabel*)0L)
    _inactive->thaw();

  return 0;
}

void GuiButton::set_scale(float f) {
  _up->set_scale(f * _up_scale);
  _down->set_scale(f * _down_scale);
  if (_up_rollover != (GuiLabel*)0L)
    _up_rollover->set_scale(f * _upr_scale);
  if (_down_rollover != (GuiLabel*)0L)
    _down_rollover->set_scale(f * _downr_scale);
  if (_inactive != (GuiLabel*)0L)
    _inactive->set_scale(f * _inactive_scale);
  GuiBehavior::set_scale(f);
  this->recompute_frame();
}

void GuiButton::set_scale(float x, float y, float z) {
  _up->set_scale(x, y, z);
  _down->set_scale(x, y, z);
  if (_up_rollover != (GuiLabel*)0L)
    _up_rollover->set_scale(x, y, z);
  if (_down_rollover != (GuiLabel*)0L)
    _down_rollover->set_scale(x, y, z);
  if (_inactive != (GuiLabel*)0L)
    _inactive->set_scale(x, y, z);
  GuiBehavior::set_scale(x, y, z);
  this->recompute_frame();
}

void GuiButton::set_pos(const LVector3f& p) {
  _up->set_pos(p);
  _down->set_pos(p);
  if (_up_rollover != (GuiLabel*)0L)
    _up_rollover->set_pos(p);
  if (_down_rollover != (GuiLabel*)0L)
    _down_rollover->set_pos(p);
  if (_inactive != (GuiLabel*)0L)
    _inactive->set_pos(p);
  GuiBehavior::set_pos(p);
  this->recompute_frame();
}

void GuiButton::start_behavior(void) {
  GuiBehavior::start_behavior();
  if (_mgr == (GuiManager*)0L)
    return;
  if (!this->is_active())
    return;
  _mgr->get_private_handler()->add_hook(_down_event, GuiButton::behavior_down);
  _mgr->get_private_handler()->add_hook(_down_rollover_event,
                                        GuiButton::behavior_down);
}

void GuiButton::stop_behavior(void) {
  GuiBehavior::stop_behavior();
  if (_mgr == (GuiManager*)0L)
    return;
  _mgr->get_private_handler()->remove_hook(_up_event, GuiButton::behavior_up);
  _mgr->get_private_handler()->remove_hook(_up_rollover_event,
                                           GuiButton::behavior_up);
}

void GuiButton::reset_behavior(void) {
  GuiBehavior::reset_behavior();
  if (_mgr == (GuiManager*)0L)
    return;
  this->start_behavior();
  _mgr->get_private_handler()->remove_hook(_up_event, GuiButton::behavior_up);
  _mgr->get_private_handler()->remove_hook(_up_rollover_event,
                                           GuiButton::behavior_up);
}

void GuiButton::set_priority(GuiItem* i, const GuiItem::Priority p) {
  if (p == P_Highest) {
    _up->set_priority(_up, GuiLabel::P_HIGHEST);
    _down->set_priority(_up, GuiLabel::P_HIGHEST);
    if (_up_rollover != (GuiLabel*)0L)
      _up_rollover->set_priority(_up, GuiLabel::P_HIGHEST);
    if (_down_rollover != (GuiLabel*)0L)
      _down_rollover->set_priority(_up, GuiLabel::P_HIGHEST);
    if (_inactive != (GuiLabel*)0L)
      _inactive->set_priority(_up, GuiLabel::P_HIGHEST);
  } else if (p == P_Lowest) {
    _up->set_priority(_up, GuiLabel::P_LOWEST);
    _down->set_priority(_up, GuiLabel::P_LOWEST);
    if (_up_rollover != (GuiLabel*)0L)
      _up_rollover->set_priority(_up, GuiLabel::P_LOWEST);
    if (_down_rollover != (GuiLabel*)0L)
      _down_rollover->set_priority(_up, GuiLabel::P_LOWEST);
    if (_inactive != (GuiLabel*)0L)
      _inactive->set_priority(_up, GuiLabel::P_LOWEST);
  } else {
    i->set_priority(_up, ((p==P_Low)?P_High:P_Low));
    i->set_priority(_down, ((p==P_Low)?P_High:P_Low));
    if (_up_rollover != (GuiLabel*)0L)
      i->set_priority(_up_rollover, ((p==P_Low)?P_High:P_Low));
    if (_down_rollover != (GuiLabel*)0L)
      i->set_priority(_down_rollover, ((p==P_Low)?P_High:P_Low));
    if (_inactive != (GuiLabel*)0L)
      i->set_priority(_inactive, ((p==P_Low)?P_High:P_Low));
  }
  GuiBehavior::set_priority(i, p);
}

int GuiButton::set_draw_order(int v) {
  // No two of these labels will ever be drawn simultaneously, so
  // there's no need to cascade the draw orders.  They can each be
  // assigned the same value, and the value we return is the maximum
  // of any of the values returned by the labels.
  _rgn->set_sort(v);
  int o = v+1;
  int o1 = _up->set_draw_order(v);
  o = max(o, o1);
  o1 = _down->set_draw_order(v);
  o = max(o, o1);
  if (_up_rollover != (GuiLabel*)0L) {
    o1 = _up_rollover->set_draw_order(v);
    o = max(o, o1);
  }
  if (_down_rollover != (GuiLabel*)0L) {
    o1 = _down_rollover->set_draw_order(v);
    o = max(o, o1);
  }
  if (_inactive != (GuiLabel*)0L) {
    o1 = _inactive->set_draw_order(v);
    o = max(o, o1);
  }
  o1 = GuiBehavior::set_draw_order(v);
  o = max(o, o1);
  return o;
}

void GuiButton::output(ostream& os) const {
  GuiBehavior::output(os);
  os << "  Button data:" << endl;
  os << "    up - 0x" << (void*)_up << endl;
  os << "    up_rollover - 0x" << (void*)_up_rollover << endl;
  os << "    down - 0x" << (void*)_down << endl;
  os << "    down_rollover - 0x" << (void*)_down_rollover << endl;
  os << "    inactive - 0x" << (void*)_inactive << endl;
  os << "    up event - '" << _up_event << "'" << endl;
  os << "    up_rollover event - '" << _up_rollover_event << "'" << endl;
  os << "    down event - '" << _down_event << "'" << endl;
  os << "    down_rollover event - '" << _down_rollover_event << "'" << endl;
  os << "    inactive event - '" << _inactive_event << "'" << endl;
  os << "    behavior event - '" << _behavior_event << "'" << endl;
  os << "    behavior param - " << _event_param << "'" << endl;
  os << "    rgn - 0x" << (void*)_rgn << " (" << *_rgn << ")" << endl;
  os << "      frame - " << _rgn->get_frame() << endl;
  os << "    state - " << (int)_state << endl;
}

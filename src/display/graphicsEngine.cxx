// Filename: graphicsEngine.cxx
// Created by:  drose (24Feb02)
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

#include "graphicsEngine.h"
#include "graphicsPipe.h"
#include "config_display.h"
#include "pipeline.h"
#include "drawCullHandler.h"
#include "binCullHandler.h"
#include "cullResult.h"
#include "cullTraverser.h"
#include "clockObject.h"
#include "pStatTimer.h"
#include "pStatClient.h"
#include "mutexHolder.h"
#include "string_utils.h"

#ifndef CPPPARSER
PStatCollector GraphicsEngine::_cull_pcollector("Cull");
PStatCollector GraphicsEngine::_draw_pcollector("Draw");
#endif  // CPPPARSER

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::Constructor
//       Access: Published
//  Description: Creates a new GraphicsEngine object.  The Pipeline is
//               normally left to default to NULL, which indicates the
//               global render pipeline, but it may be any Pipeline
//               you choose.
////////////////////////////////////////////////////////////////////
GraphicsEngine::
GraphicsEngine(Pipeline *pipeline) :
  _pipeline(pipeline)
{
  if (_pipeline == (Pipeline *)NULL) {
    _pipeline = Pipeline::get_render_pipeline();
  }
  set_threading_model(threading_model);
  if (!_threading_model.empty()) {
    display_cat.info()
      << "Using threading model " << _threading_model << "\n";
  }
  _needs_sync = false;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::Destructor
//       Access: Published
//  Description: Gracefully cleans up the graphics engine and its
//               related threads and windows.
////////////////////////////////////////////////////////////////////
GraphicsEngine::
~GraphicsEngine() {
  remove_all_windows();
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::set_threading_model
//       Access: Published
//  Description: Specifies how windows created using future calls to
//               the one-parameter version of make_window() will be
//               threaded.  (The two-parameter flavor of make_window()
//               allows this to be specified on a per-window basis.)
//
//               The threading model is a string representing the
//               names of the two threads that will process cull and
//               draw for the given window, separated by a slash.  The
//               names are completely arbitrary and are used only to
//               differentiate threads.  The two names may be the
//               same, meaning the same thread, or each may be the
//               empty string, which represents the previous thread.
//
//               Thus, for example, "cull/draw" indicates that the
//               window will be culled in a thread called "cull", and
//               drawn in a separate thread called "draw".
//               "draw/draw" or simply "draw/" indicates the window
//               will be culled and drawn in the same thread, "draw".
//               On the other hand, "/draw" indicates the thread will
//               be culled in the main, or app thread, and drawn in a
//               separate thread named "draw".  The empty string, ""
//               or "/", indicates the thread will be culled and drawn
//               in the main thread; that is to say, a single-process
//               model.
//
//               Finally, if the threading model begins with a "-"
//               character, then cull and draw are run simultaneously,
//               in the same thread, with no binning or state sorting.
//               It simplifies the cull process but it forces the
//               scene to render in scene graph order; state sorting
//               and alpha sorting is lost.
//
//               You can create as many different threads as you like;
//               each thread is uniquified based on its name, so
//               multiple windows may easily be handled by the same or
//               different threads.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
set_threading_model(const string &threading_model) {
  if (!threading_model.empty() && !Thread::is_threading_supported()) {
    display_cat.warning()
      << "Threading model " << threading_model
      << " requested but threading not supported.\n";
    return;
  }
  MutexHolder holder(_lock);
  _threading_model = threading_model;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::get_threading_model
//       Access: Published
//  Description: Returns the current default threading model.  See
//               set_threading_model().
////////////////////////////////////////////////////////////////////
string GraphicsEngine::
get_threading_model() const {
  string result;
  {
    MutexHolder holder(_lock);
    result = _threading_model;
  }
  return result;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::make_window
//       Access: Published
//  Description: Creates a new window using the indicated GraphicsPipe
//               and returns it.  The GraphicsEngine becomes the owner
//               of the window; it will persist at least until
//               remove_window() is called later.
////////////////////////////////////////////////////////////////////
GraphicsWindow *GraphicsEngine::
make_window(GraphicsPipe *pipe, const string &threading_model) {
  PT(GraphicsWindow) window = pipe->make_window();
  if (window != (GraphicsWindow *)NULL) {
    MutexHolder holder(_lock);
    _windows.insert(window);

    // Now figure out the threading model.
    string cull_name;
    string draw_name;
    bool cull_sorting = true;
    size_t start = 0;
    if (!threading_model.empty() && threading_model[0] == '-') {
      start = 1;
      cull_sorting = false;
    }

    size_t slash = threading_model.find('/', start);
    if (slash == string::npos) {
      cull_name = threading_model;
    } else {
      cull_name = threading_model.substr(start, slash - start);
      draw_name = threading_model.substr(slash + 1);
    }
    if (!cull_sorting || draw_name.empty()) {
      draw_name = cull_name;
    }

    /*
    cerr << "cull_name = " << cull_name << " draw_name = " << draw_name
         << " cull_sorting = " << cull_sorting << "\n";
    */

    WindowRenderer *cull = get_window_renderer(cull_name);
    WindowRenderer *draw = get_window_renderer(draw_name);

    if (cull_sorting) {
      cull->add_window(cull->_cull, window);
      draw->add_window(draw->_draw, window);
    } else {
      cull->add_window(cull->_cdraw, window);
    }

    // We should ask the pipe which thread it prefers to run its
    // windowing commands in (the "window thread").  This is the
    // thread that handles the commands to open, resize, etc. the
    // window.  X requires this to be done in the app thread, but some
    // pipes might prefer this to be done in draw, for instance.  For
    // now, we assume this is the app thread.
    _app.add_window(_app._window, window);

    display_cat.info()
      << "Created " << window->get_type() << "\n";
  }
  return window;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::remove_window
//       Access: Published
//  Description: Removes the indicated window from the set of windows
//               that will be processed when render_frame() is called.
//               This also closes the window if it is open, and
//               removes the window from its GraphicsPipe, allowing
//               the window to be destructed if there are no other
//               references to it.  (However, the window may not be
//               actually closed until next frame, if it is controlled
//               by a sub-thread.)
//
//               The return value is true if the window was removed,
//               false if it was not found.
//
//               Unlike remove_all_windows(), this function does not
//               terminate any of the threads that may have been
//               started to service this window; they are left running
//               (since you might open a new window later on these
//               threads).  If your intention is to clean up before
//               shutting down, it is better to call
//               remove_all_windows() then to call remove_window() one
//               at a time.
////////////////////////////////////////////////////////////////////
bool GraphicsEngine::
remove_window(GraphicsWindow *window) {
  // First, make sure we know what this window is.
  PT(GraphicsWindow) ptwin = window;
  size_t count;
  {
    MutexHolder holder(_lock);
    count = _windows.erase(ptwin);
  }
  if (count == 0) {
    // Never heard of this window.  Do nothing.
    return false;
  }

  do_remove_window(window);
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::remove_all_windows
//       Access: Published
//  Description: Removes and closes all windows from the engine.  This
//               also cleans up and terminates any threads that have
//               been started to service those windows.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
remove_all_windows() {
  Windows::iterator wi;
  for (wi = _windows.begin(); wi != _windows.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    do_remove_window(win);
  }

  _windows.clear();

  _app.do_release(this);
  _app.do_close(this);
  terminate_threads();
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::render_frame
//       Access: Published
//  Description: Renders the next frame in all the registered windows,
//               and flips all of the frame buffers.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
render_frame() {
  // We hold the GraphicsEngine mutex while we wait for all of the
  // threads.  Doing this puts us at risk for deadlock if any of the
  // threads tries to call any methods on the GraphicsEngine.  So
  // don't do that.
  MutexHolder holder(_lock);

  if (_needs_sync) {
    // Flip the windows from the previous frame, if necessary.
    do_sync_frame();
  }
  
  // Grab each thread's mutex again after all windows have flipped.
  Threads::const_iterator ti;
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    thread->_cv_mutex.lock();
  }
  
  // Now cycle the pipeline and officially begin the next frame.
  _pipeline->cycle();
  ClockObject::get_global_clock()->tick();
  PStatClient::main_tick();
  
  // Now signal all of our threads to begin their next frame.
  _app.do_frame(this);
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    if (thread->_thread_state == TS_wait) {
      thread->_thread_state = TS_do_frame;
      thread->_cv.signal();
    }
    thread->_cv_mutex.release();
  }

  // Some threads may still be drawing, so indicate that we have to
  // wait for those threads before we can flip.
  _needs_sync = true;

  // But if we don't have any threads, go ahead and flip the frame
  // now.  No point in waiting if we're single-threaded.
  if (_threads.empty()) {
    do_sync_frame();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::sync_frame
//       Access: Published
//  Description: Waits for all the threads that started drawing their
//               last frame to finish drawing, and then flips all the
//               windows.  It is not usually necessary to call this
//               explicitly, unless you need to see the previous frame
//               right away.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
sync_frame() {
  // We hold the GraphicsEngine mutex while we wait for all of the
  // threads.  Doing this puts us at risk for deadlock if any of the
  // threads tries to call any methods on the GraphicsEngine.  So
  // don't do that.
  MutexHolder holder(_lock);
  if (_needs_sync) {
    do_sync_frame();
  }
}


////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::render_subframe
//       Access: Published
//  Description: Performs a complete cull and draw pass for one
//               particular display region.  This is normally useful
//               only for special effects, like shaders, that require
//               a complete offscreen render pass before they can
//               complete.
//
//               This always executes completely within the calling
//               thread, regardless of the threading model in use.
//               Thus, it must always be called from the draw thread,
//               whichever thread that may be.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
render_subframe(GraphicsStateGuardian *gsg, DisplayRegion *dr,
                bool cull_sorting) {
  if (cull_sorting) {
    cull_bin_draw(gsg, dr);
  } else {
    cull_and_draw_together(gsg, dr);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::cull_and_draw_together
//       Access: Private
//  Description: This is called in the cull+draw thread by individual
//               RenderThread objects during the frame rendering.  It
//               culls the geometry and immediately draws it, without
//               first collecting it into bins.  This is used when the
//               threading model begins with the "-" character.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
cull_and_draw_together(const GraphicsEngine::Windows &wlist) {
  Windows::const_iterator wi;
  for (wi = wlist.begin(); wi != wlist.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    if (win->is_active()) {
      if (win->begin_frame()) {
        win->clear();
      
        int num_display_regions = win->get_num_display_regions();
        for (int i = 0; i < num_display_regions; i++) {
          DisplayRegion *dr = win->get_display_region(i);
          cull_and_draw_together(win->get_gsg(), dr);
        }
        win->end_frame();
      }
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::cull_and_draw_together
//       Access: Private
//  Description: This variant of cull_and_draw_together() is called
//               only by render_subframe().
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
cull_and_draw_together(GraphicsStateGuardian *gsg, DisplayRegion *dr) {
  nassertv(gsg != (GraphicsStateGuardian *)NULL);

  PT(SceneSetup) scene_setup = setup_scene(dr->get_camera(), gsg);
  if (setup_gsg(gsg, scene_setup)) {
    DisplayRegionStack old_dr = gsg->push_display_region(dr);
    gsg->prepare_display_region();
    if (dr->is_any_clear_active()) {
      gsg->clear(dr);
    }

    DrawCullHandler cull_handler(gsg);
    if (gsg->begin_scene()) {
      do_cull(&cull_handler, scene_setup, gsg);
      gsg->end_scene();
    }
    
    gsg->pop_display_region(old_dr);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::cull_bin_draw
//       Access: Private
//  Description: This is called in the cull thread by individual
//               RenderThread objects during the frame rendering.  It
//               collects the geometry into bins in preparation for
//               drawing.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
cull_bin_draw(const GraphicsEngine::Windows &wlist) {
  Windows::const_iterator wi;
  for (wi = wlist.begin(); wi != wlist.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    if (win->is_active()) {
      // This should be done in the draw thread, not here.
      if (win->begin_frame()) {
        win->clear();
      
        int num_display_regions = win->get_num_display_regions();
        for (int i = 0; i < num_display_regions; i++) {
          DisplayRegion *dr = win->get_display_region(i);
          cull_bin_draw(win->get_gsg(), dr);
        }

        win->end_frame();
      }
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::cull_bin_draw
//       Access: Private
//  Description: This variant of cull_bin_draw() is called
//               by render_subframe(), as well as within the
//               implementation of cull_bin_draw(), above.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
cull_bin_draw(GraphicsStateGuardian *gsg, DisplayRegion *dr) {
  nassertv(gsg != (GraphicsStateGuardian *)NULL);

  PT(CullResult) cull_result = dr->_cull_result;
  if (cull_result != (CullResult *)NULL) {
    cull_result = cull_result->make_next();
  } else {
    cull_result = new CullResult(gsg);
  }

  PT(SceneSetup) scene_setup = setup_scene(dr->get_camera(), gsg);
  if (scene_setup != (SceneSetup *)NULL) {
    BinCullHandler cull_handler(cull_result);
    do_cull(&cull_handler, scene_setup, gsg);
    
    cull_result->finish_cull();
    
    // Save the results for next frame.
    dr->_cull_result = cull_result;
    
    // Now draw.
    // This should get deferred into the next pipeline stage.
    do_draw(cull_result, scene_setup, gsg, dr);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::process_events
//       Access: Private
//  Description: This is called by the RenderThread object to process
//               all the windows events (resize, etc.) for the given
//               list of windows.  This is run in the window thread.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
process_events(const GraphicsEngine::Windows &wlist) {
  Windows::const_iterator wi;
  for (wi = wlist.begin(); wi != wlist.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    win->process_events();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::flip_windows
//       Access: Private
//  Description: This is called by the RenderThread object to flip the
//               buffers (resize, etc.) for the given list of windows.
//               This is run in the draw thread.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
flip_windows(const GraphicsEngine::Windows &wlist) {
  Windows::const_iterator wi;
  for (wi = wlist.begin(); wi != wlist.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    win->begin_flip();
  }
  for (wi = wlist.begin(); wi != wlist.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    win->end_flip();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::do_sync_frame
//       Access: Private
//  Description: The implementation of sync_frame().  We assume _lock
//               is already held before this method is called.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
do_sync_frame() {
  // First, wait for all the threads to finish their current frame.
  // Grabbing the mutex should achieve that.
  Threads::const_iterator ti;
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    thread->_cv_mutex.lock();
  }
  
  // Now signal all of our threads to flip the windows.
  _app.do_flip(this);
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    if (thread->_thread_state == TS_wait) {
      thread->_thread_state = TS_do_flip;
      thread->_cv.signal();
    }
    thread->_cv_mutex.release();
  }

  _needs_sync = false;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::setup_scene
//       Access: Private
//  Description: Returns a new SceneSetup object appropriate for
//               rendering the scene from the indicated camera, or
//               NULL if the scene should not be rendered for some
//               reason.
////////////////////////////////////////////////////////////////////
PT(SceneSetup) GraphicsEngine::
setup_scene(const NodePath &camera, GraphicsStateGuardian *gsg) {
  if (camera.is_empty()) {
    // No camera, no draw.
    return NULL;
  }

  Camera *camera_node;
  DCAST_INTO_R(camera_node, camera.node(), NULL);

  if (!camera_node->is_active()) {
    // Camera inactive, no draw.
    return NULL;
  }

  Lens *lens = camera_node->get_lens();
  if (lens == (Lens *)NULL) {
    // No lens, no draw.
    return NULL;
  }

  NodePath scene_root = camera_node->get_scene();
  if (scene_root.is_empty()) {
    // No scene, no draw.
    return NULL;
  }

  PT(SceneSetup) scene_setup = new SceneSetup;

  // We will need both the camera transform (the net transform to the
  // camera from the scene) and the world transform (the camera
  // transform inverse, or the net transform to the scene from the
  // camera).
  CPT(TransformState) camera_transform = camera.get_transform(scene_root);
  CPT(TransformState) world_transform = scene_root.get_transform(camera);

  // The render transform is the same as the world transform, except
  // it is converted into the GSG's internal coordinate system.  This
  // is the transform that the GSG will apply to all of its vertices.
  CPT(TransformState) cs_transform = TransformState::make_identity();
  CoordinateSystem external_cs = gsg->get_coordinate_system();
  CoordinateSystem internal_cs = gsg->get_internal_coordinate_system();
  if (internal_cs != CS_default && internal_cs != external_cs) {
    cs_transform = 
      TransformState::make_mat(LMatrix4f::convert_mat(external_cs, internal_cs));
  }

  scene_setup->set_scene_root(scene_root);
  scene_setup->set_camera_path(camera);
  scene_setup->set_camera_node(camera_node);
  scene_setup->set_lens(lens);
  scene_setup->set_camera_transform(camera_transform);
  scene_setup->set_world_transform(world_transform);
  scene_setup->set_cs_transform(cs_transform);

  return scene_setup;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::do_cull
//       Access: Private
//  Description: Fires off a cull traversal using the indicated camera.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
do_cull(CullHandler *cull_handler, SceneSetup *scene_setup,
        GraphicsStateGuardian *gsg) {
  // Statistics
  PStatTimer timer(_cull_pcollector);

  CullTraverser trav;
  trav.set_cull_handler(cull_handler);
  trav.set_depth_offset_decals(gsg->depth_offset_decals());
  trav.set_scene(scene_setup);
  trav.set_camera_mask(scene_setup->get_camera_node()->get_camera_mask());

  if (view_frustum_cull) {
    // If we're to be performing view-frustum culling, determine the
    // bounding volume associated with the current viewing frustum.

    // First, we have to get the current viewing frustum, which comes
    // from the lens.
    PT(BoundingVolume) bv = scene_setup->get_lens()->make_bounds();

    if (bv != (BoundingVolume *)NULL &&
        bv->is_of_type(GeometricBoundingVolume::get_class_type())) {
      // Transform it into the appropriate coordinate space.
      PT(GeometricBoundingVolume) local_frustum;
      local_frustum = DCAST(GeometricBoundingVolume, bv->make_copy());
      local_frustum->xform(scene_setup->get_camera_transform()->get_mat());

      trav.set_view_frustum(local_frustum);
    }
  }
  
  trav.traverse(scene_setup->get_scene_root());
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::do_draw
//       Access: Private
//  Description: Draws the previously-culled scene.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
do_draw(CullResult *cull_result, SceneSetup *scene_setup,
        GraphicsStateGuardian *gsg, DisplayRegion *dr) {
  // Statistics
  PStatTimer timer(_draw_pcollector);

  if (setup_gsg(gsg, scene_setup)) {
    DisplayRegionStack old_dr = gsg->push_display_region(dr);
    gsg->prepare_display_region();
    if (dr->is_any_clear_active()) {
      gsg->clear(dr);
    }
    if (gsg->begin_scene()) {
      cull_result->draw();
      gsg->end_scene();
    }
    gsg->pop_display_region(old_dr);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::setup_gsg
//       Access: Private
//  Description: Sets up the GSG to draw the indicated scene.  Returns
//               true if the scene (and its lens) is acceptable, false
//               otherwise.
////////////////////////////////////////////////////////////////////
bool GraphicsEngine::
setup_gsg(GraphicsStateGuardian *gsg, SceneSetup *scene_setup) {
  if (scene_setup == (SceneSetup *)NULL) {
    // No scene, no draw.
    return false;
  }

  const Lens *lens = scene_setup->get_lens();
  if (lens == (const Lens *)NULL) {
    // No lens, no draw.
    return false;
  }

  if (!gsg->set_lens(lens)) {
    // The lens is inappropriate somehow.
    display_cat.error()
      << gsg->get_type() << " cannot render with " << lens->get_type()
      << "\n";
    return false;
  }

  gsg->set_scene(scene_setup);

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::do_remove_window
//       Access: Private
//  Description: An internal function called by remove_window() and
//               remove_all_windows() to actually remove the indicated
//               window from all relevant structures, except the
//               _windows list itself.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
do_remove_window(GraphicsWindow *window) {
  PT(GraphicsPipe) pipe = window->get_pipe();
  if (pipe != (GraphicsPipe *)NULL) {
    pipe->remove_window(window);
    window->_pipe = (GraphicsPipe *)NULL;
  }

  // Now remove the window from all threads that know about it.
  _app.remove_window(window);
  Threads::const_iterator ti;
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    thread->remove_window(window);
  }

  // If the window happened to be controlled by the app thread, we
  // might as well close it now rather than waiting for next frame.
  _app.do_pending(this);
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::terminate_threads
//       Access: Private
//  Description: Signals our child threads to terminate and waits for
//               them to clean up.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::
terminate_threads() {
  MutexHolder holder(_lock);
  
  // First, wait for all the threads to finish their current frame.
  // Grabbing the mutex should achieve that.
  Threads::const_iterator ti;
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    thread->_cv_mutex.lock();
  }

  // Now tell them to release their windows' graphics contexts.
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    if (thread->_thread_state == TS_wait) {
      thread->_thread_state = TS_do_release;
      thread->_cv.signal();
    }
    thread->_cv_mutex.release();
  }

  // Grab the mutex again to wait for the above to complete.
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    thread->_cv_mutex.lock();
  }

  // Now tell them to close their windows and terminate.
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    MutexHolder cv_holder(thread->_cv_mutex);
    thread->_thread_state = TS_terminate;
    thread->_cv.signal();
    thread->_cv_mutex.release();
  }

  // Finally, wait for them all to finish cleaning up.
  for (ti = _threads.begin(); ti != _threads.end(); ++ti) {
    RenderThread *thread = (*ti).second;
    thread->join();
  }
  
  _threads.clear();
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::get_window_renderer
//       Access: Private
//  Description: Returns the WindowRenderer with the given name.
//               Creates a new RenderThread if there is no such thread
//               already.
////////////////////////////////////////////////////////////////////
GraphicsEngine::WindowRenderer *GraphicsEngine::
get_window_renderer(const string &name) {
  if (name.empty()) {
    return &_app;
  }

  MutexHolder holder(_lock);
  Threads::iterator ti = _threads.find(name);
  if (ti != _threads.end()) {
    return (*ti).second.p();
  }

  PT(RenderThread) thread = new RenderThread(name, this);
  thread->start(TP_normal, true, true);
  _threads[name] = thread;

  return thread.p();
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::WindowRenderer::add_window
//       Access: Public
//  Description: Adds a new window to the indicated list, which should
//               be a member of the WindowRenderer.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::WindowRenderer::
add_window(Windows &wlist, GraphicsWindow *window) {
  MutexHolder holder(_wl_lock);
  wlist.insert(window);
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::WindowRenderer::remove_window_now
//       Access: Public
//  Description: Immediately removes the indicated window from all
//               lists.  If the window is currently open and is
//               already on the _window list, moves it to the _pending_close
//               list for later closure.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::WindowRenderer::
remove_window(GraphicsWindow *window) {
  MutexHolder holder(_wl_lock);
  PT(GraphicsWindow) ptwin = window;

  _cull.erase(ptwin);

  Windows::iterator wi;

  wi = _cdraw.find(ptwin);
  if (wi != _cdraw.end()) {
    // The window is on our _cdraw list, meaning its GSG operations are
    // serviced by this thread (cull and draw in the same operation).
    
    // Move it to the pending release thread so we can release the GSG
    // when the thread next runs.  We can't do this immediately,
    // because we might not have been called from the subthread.
    _pending_release.insert(ptwin);
    _cdraw.erase(wi);
  }

  wi = _draw.find(ptwin);
  if (wi != _draw.end()) {
    // The window is on our _draw list, meaning its GSG operations are
    // serviced by this thread (draw performed on this thread).
    
    // Move it to the pending release thread so we can release the GSG
    // when the thread next runs.  We can't do this immediately,
    // because we might not have been called from the subthread.
    _pending_release.insert(ptwin);
    _draw.erase(wi);
  }

  wi = _window.find(ptwin);
  if (wi != _window.end()) {
    // The window is on our _window list, meaning its open/close
    // operations (among other window ops) are serviced by this
    // thread.

    // Make sure the window isn't about to request itself open.
    WindowProperties close_properties;
    close_properties.set_open(false);
    ptwin->request_properties(close_properties);

    // If the window is already open, move it to the _pending_close list so
    // it can be closed later.  We can't close it immediately, because
    // we might not have been called from the subthread.
    if (!ptwin->is_closed()) {
      _pending_close.insert(ptwin);
    }

    _window.erase(wi);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::WindowRenderer::do_frame
//       Access: Public
//  Description: Executes one stage of the pipeline for the current
//               thread: calls cull on all windows that are on the
//               cull list for this thread, draw on all the windows on
//               the draw list, etc.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::WindowRenderer::
do_frame(GraphicsEngine *engine) {
  MutexHolder holder(_wl_lock);
  engine->cull_bin_draw(_cull);
  engine->cull_and_draw_together(_cdraw);
  engine->process_events(_window);
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::WindowRenderer::do_flip
//       Access: Public
//  Description: Flips the windows as appropriate for the current
//               thread.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::WindowRenderer::
do_flip(GraphicsEngine *engine) {
  MutexHolder holder(_wl_lock);
  engine->flip_windows(_cdraw);
  engine->flip_windows(_draw);
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::WindowRenderer::do_release
//       Access: Public
//  Description: Releases the rendering contexts for all windows on
//               the _draw list.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::WindowRenderer::
do_release(GraphicsEngine *) {
  MutexHolder holder(_wl_lock);
  Windows::iterator wi;
  for (wi = _draw.begin(); wi != _draw.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    win->release_gsg();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::WindowRenderer::do_close
//       Access: Public
//  Description: Closes all the windows on the _window list.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::WindowRenderer::
do_close(GraphicsEngine *) {
  WindowProperties close_properties;
  close_properties.set_open(false);

  MutexHolder holder(_wl_lock);
  Windows::iterator wi;
  for (wi = _window.begin(); wi != _window.end(); ++wi) {
    GraphicsWindow *win = (*wi);
    win->set_properties_now(close_properties);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::WindowRenderer::do_pending
//       Access: Public
//  Description: Actually closes any windows that were recently
//               removed from the WindowRenderer.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::WindowRenderer::
do_pending(GraphicsEngine *engine) {
  MutexHolder holder(_wl_lock);

  if (!_pending_release.empty()) {
    // Release any GSG's that were waiting.
    Windows::iterator wi;
    for (wi = _pending_release.begin(); wi != _pending_release.end(); ++wi) {
      GraphicsWindow *win = (*wi);
      win->release_gsg();
    }
    _pending_release.clear();
  }

  if (!_pending_close.empty()) {
    WindowProperties close_properties;
    close_properties.set_open(false);

    // Close any windows that were pending closure, but only if their
    // associated GSG has already been released.
    Windows new_pending_close;
    Windows::iterator wi;
    for (wi = _pending_close.begin(); wi != _pending_close.end(); ++wi) {
      GraphicsWindow *win = (*wi);
      if (win->get_gsg() == (GraphicsStateGuardian *)NULL) {
        win->set_properties_now(close_properties);
      } else {
        // If the GSG hasn't been released yet, we have to save the
        // close operation for next frame.
        new_pending_close.insert(win);
      }
    }
    _pending_close.swap(new_pending_close);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::RenderThread::Constructor
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
GraphicsEngine::RenderThread::
RenderThread(const string &name, GraphicsEngine *engine) : 
  Thread(name),
  _engine(engine),
  _cv(_cv_mutex)
{
  _thread_state = TS_wait;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsEngine::RenderThread::thread_main
//       Access: Public, Virtual
//  Description: The main loop for a particular render thread.  The
//               thread will process whatever cull or draw windows it
//               has assigned to it.
////////////////////////////////////////////////////////////////////
void GraphicsEngine::RenderThread::
thread_main() {
  MutexHolder holder(_cv_mutex);
  while (true) {
    _cv.wait();
    switch (_thread_state) {
    case TS_wait:
      break;

    case TS_do_frame:
      do_pending(_engine);
      do_frame(_engine);
      _thread_state = TS_wait;
      break;

    case TS_do_flip:
      do_flip(_engine);
      _thread_state = TS_wait;
      break;

    case TS_do_release:
      do_pending(_engine);
      do_release(_engine);
      _thread_state = TS_wait;
      break;

    case TS_terminate:
      do_pending(_engine);
      do_close(_engine);
      return;
    }
  }
}

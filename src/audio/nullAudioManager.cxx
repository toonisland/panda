// Filename: nullAudioManager.cxx
// Created by:  skyler (June 6, 2001)
// Prior system by: cary
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) 2001 - 2004, Disney Enterprises, Inc.  All rights reserved
//
// All use of this software is subject to the terms of the Panda 3d
// Software license.  You should have received a copy of this license
// along with this source code; you will also find a current copy of
// the license at http://etc.cmu.edu/panda3d/docs/license/ .
//
// To contact the maintainers of this program write to
// panda3d-general@lists.sourceforge.net .
//
////////////////////////////////////////////////////////////////////

#include "nullAudioManager.h"


////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::NullAudioManager
//       Access: 
//  Description: 
////////////////////////////////////////////////////////////////////
NullAudioManager::
NullAudioManager() {
  audio_info("NullAudioManager");
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::~NullAudioManager
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
NullAudioManager::
~NullAudioManager() {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::is_valid
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
bool NullAudioManager::
is_valid() {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::get_sound
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
PT(AudioSound) NullAudioManager::
get_sound(const string&) {
  return get_null_sound();
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::uncache_sound
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
uncache_sound(const string&) {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::uncache_all_sounds
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
clear_cache() {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::set_cache_limit
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
set_cache_limit(unsigned int) {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::get_cache_limit
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
unsigned int NullAudioManager::
get_cache_limit() const {
  // intentionally blank.
  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::set_volume
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
set_volume(float) {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::get_volume
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
float NullAudioManager::
get_volume() const {
  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::set_active
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
set_active(bool) {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::get_active
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
bool NullAudioManager::
get_active() const {
  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::set_concurrent_sound_limit
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
set_concurrent_sound_limit(unsigned int) {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::get_concurrent_sound_limit
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
unsigned int NullAudioManager::
get_concurrent_sound_limit() const {
  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::reduce_sounds_playing_to
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
reduce_sounds_playing_to(unsigned int) {
  // intentionally blank.
}

////////////////////////////////////////////////////////////////////
//     Function: NullAudioManager::stop_all_sounds
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void NullAudioManager::
stop_all_sounds() {
  // intentionally blank.
}

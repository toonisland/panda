// Filename: qpdialNode.cxx
// Created by:  drose (12Mar02)
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

#include "qpdialNode.h"
#include "config_device.h"
#include "dataNodeTransmit.h"

TypeHandle qpDialNode::_type_handle;

////////////////////////////////////////////////////////////////////
//     Function: qpDialNode::Constructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
qpDialNode::
qpDialNode(ClientBase *client, const string &device_name) :
  qpDataNode(device_name)
{
  nassertv(client != (ClientBase *)NULL);
  PT(ClientDevice) device =
    client->get_device(ClientDialDevice::get_class_type(), device_name);

  if (device == (ClientDevice *)NULL) {
    device_cat.warning()
      << "Unable to open dial device " << device_name << "\n";
    return;
  }

  if (!device->is_of_type(ClientDialDevice::get_class_type())) {
    device_cat.error()
      << "Inappropriate device type " << device->get_type()
      << " created; expected a ClientDialDevice.\n";
    return;
  }

  _dial = DCAST(ClientDialDevice, device);
}

////////////////////////////////////////////////////////////////////
//     Function: qpDialNode::Destructor
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
qpDialNode::
~qpDialNode() {
  // When the _dial pointer destructs, the ClientDialDevice
  // disconnects itself from the ClientBase, and everything that needs
  // to get turned off does.  Magic.
}

////////////////////////////////////////////////////////////////////
//     Function: qpDialNode::do_transmit_data
//       Access: Protected, Virtual
//  Description: The virtual implementation of transmit_data().  This
//               function receives an array of input parameters and
//               should generate an array of output parameters.  The
//               input parameters may be accessed with the index
//               numbers returned by the define_input() calls that
//               were made earlier (presumably in the constructor);
//               likewise, the output parameters should be set with
//               the index numbers returned by the define_output()
//               calls.
////////////////////////////////////////////////////////////////////
void qpDialNode::
do_transmit_data(const DataNodeTransmit &, DataNodeTransmit &output) {
  if (is_valid()) {
    _dial->poll();

    // Not clear yet what we should be transmitting.
  }
}

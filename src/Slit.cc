// -*- mode: C++; indent-tabs-mode: nil; -*-
// Slit.cc for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2002 Sean 'Shaleh' Perry <shaleh@debian.org>
// Copyright (c) 1997 - 2000 Brad Hughes (bhughes@tcac.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

extern "C" {
#include <X11/keysym.h>
}

#include "i18n.hh"
#include "blackbox.hh"
#include "Image.hh"
#include "Screen.hh"
#include "Slit.hh"
#include "Toolbar.hh"


Slit::Slit(BScreen *scr) {
  screen = scr;
  blackbox = screen->getBlackbox();

  on_top = screen->isSlitOnTop();
  hidden = do_auto_hide = screen->doSlitAutoHide();

  display = screen->getBaseDisplay()->getXDisplay();
  frame.window = frame.pixmap = None;

  timer = new BTimer(blackbox, this);
  timer->setTimeout(blackbox->getAutoRaiseDelay());

  slitmenu = new Slitmenu(this);

  XSetWindowAttributes attrib;
  unsigned long create_mask = CWBackPixmap | CWBackPixel | CWBorderPixel |
                              CWColormap | CWOverrideRedirect | CWEventMask;
  attrib.background_pixmap = None;
  attrib.background_pixel = attrib.border_pixel =
    screen->getBorderColor()->pixel();
  attrib.colormap = screen->getColormap();
  attrib.override_redirect = True;
  attrib.event_mask = SubstructureRedirectMask | ButtonPressMask |
                      EnterWindowMask | LeaveWindowMask;

  frame.x = frame.y = 0;
  frame.width = frame.height = 1;

  frame.window =
    XCreateWindow(display, screen->getRootWindow(), frame.x, frame.y,
                  frame.width, frame.height, screen->getBorderWidth(),
                  screen->getDepth(), InputOutput, screen->getVisual(),
                  create_mask, &attrib);
  blackbox->saveSlitSearch(frame.window, this);

  screen->addStrut(&strut);

  reconfigure();
}


Slit::~Slit() {
  if (timer->isTiming()) timer->stop();
  delete timer;

  delete slitmenu;

  screen->getImageControl()->removeImage(frame.pixmap);

  blackbox->removeSlitSearch(frame.window);

  XDestroyWindow(display, frame.window);
}


void Slit::addClient(Window w) {
  if (! blackbox->validateWindow(w))
    return;

  SlitClient *client = new SlitClient;
  client->client_window = w;

  XWMHints *wmhints = XGetWMHints(display, w);

  if (wmhints) {
    if ((wmhints->flags & IconWindowHint) &&
        (wmhints->icon_window != None)) {
      XMoveWindow(display, client->client_window, screen->getWidth() + 10,
                  screen->getHeight() + 10);
      XMapWindow(display, client->client_window);

      client->icon_window = wmhints->icon_window;
      client->window = client->icon_window;
    } else {
      client->icon_window = None;
      client->window = client->client_window;
    }

    XFree(wmhints);
  } else {
    client->icon_window = None;
    client->window = client->client_window;
  }

  XWindowAttributes attrib;
  if (XGetWindowAttributes(display, client->window, &attrib)) {
    client->width = attrib.width;
    client->height = attrib.height;
  } else {
    client->width = client->height = 64;
  }

  XSetWindowBorderWidth(display, client->window, 0);

  XSelectInput(display, frame.window, NoEventMask);
  XSelectInput(display, client->window, NoEventMask);

  XReparentWindow(display, client->window, frame.window, 0, 0);
  XMapRaised(display, client->window);
  XChangeSaveSet(display, client->window, SetModeInsert);

  XSelectInput(display, frame.window, SubstructureRedirectMask |
               ButtonPressMask | EnterWindowMask | LeaveWindowMask);
  XSelectInput(display, client->window, StructureNotifyMask |
               SubstructureNotifyMask | EnterWindowMask);
  XFlush(display);

  clientList.push_back(client);

  blackbox->saveSlitSearch(client->client_window, this);
  blackbox->saveSlitSearch(client->icon_window, this);
  reconfigure();
}


void Slit::removeClient(SlitClient *client, Bool remap) {
  blackbox->removeSlitSearch(client->client_window);
  blackbox->removeSlitSearch(client->icon_window);
  clientList.remove(client);

  screen->removeNetizen(client->window);

  if (remap && blackbox->validateWindow(client->window)) {
    XSelectInput(display, frame.window, NoEventMask);
    XSelectInput(display, client->window, NoEventMask);
    XReparentWindow(display, client->window, screen->getRootWindow(),
                    client->x, client->y);
    XChangeSaveSet(display, client->window, SetModeDelete);
    XSelectInput(display, frame.window, SubstructureRedirectMask |
                 ButtonPressMask | EnterWindowMask | LeaveWindowMask);
    XFlush(display);
  }

  delete client;
  client = (SlitClient *) 0;
}


struct SlitClientMatch {
  Window window;
  SlitClientMatch(Window w): window(w) {}
  inline Bool operator()(const Slit::SlitClient* client) const {
    return (client->window == window);
  }
};


void Slit::removeClient(Window w, Bool remap) {
  SlitClientList::iterator it;
  const SlitClientList::iterator end = clientList.end();

  it = std::find_if(clientList.begin(), end, SlitClientMatch(w));
  if (it != end) {
    removeClient(*it, remap);
    reconfigure();
  }
}


void Slit::reconfigure(void) {
  SlitClientList::iterator it = clientList.begin();
  const SlitClientList::iterator end = clientList.end();
  SlitClient *client;

  frame.width = 0;
  frame.height = 0;

  switch (screen->getSlitDirection()) {
  case Vertical:
    for (; it != end; ++it) {
      client = *it;
      frame.height += client->height + screen->getBevelWidth();

      if (frame.width < client->width)
        frame.width = client->width;
    }

    if (frame.width < 1)
      frame.width = 1;
    else
      frame.width += (screen->getBevelWidth() * 2);

    if (frame.height < 1)
      frame.height = 1;
    else
      frame.height += screen->getBevelWidth();

    break;

  case Horizontal:
    for (; it != end; ++it) {
      client = *it;
      frame.width += client->width + screen->getBevelWidth();

      if (frame.height < client->height)
        frame.height = client->height;
    }

    if (frame.width < 1)
      frame.width = 1;
    else
      frame.width += screen->getBevelWidth();

    if (frame.height < 1)
      frame.height = 1;
    else
      frame.height += (screen->getBevelWidth() * 2);

    break;
  }

  reposition();

  XSetWindowBorderWidth(display ,frame.window, screen->getBorderWidth());
  XSetWindowBorder(display, frame.window,
                   screen->getBorderColor()->pixel());

  if (clientList.empty())
    XUnmapWindow(display, frame.window);
  else
    XMapWindow(display, frame.window);

  Pixmap tmp = frame.pixmap;
  BImageControl *image_ctrl = screen->getImageControl();
  BTexture *texture = &(screen->getToolbarStyle()->toolbar);
  if (texture->texture() == (BTexture::Flat | BTexture::Solid)) {
    frame.pixmap = None;
    XSetWindowBackground(display, frame.window,
                         texture->color().pixel());
  } else {
    frame.pixmap = image_ctrl->renderImage(frame.width, frame.height,
                                           *texture);
    XSetWindowBackgroundPixmap(display, frame.window, frame.pixmap);
  }
  if (tmp) image_ctrl->removeImage(tmp);
  XClearWindow(display, frame.window);

  strut.top = strut.bottom = strut.left = strut.right = 0;
  it = clientList.begin();

  int x, y;

  switch (screen->getSlitDirection()) {
  case Vertical:
    x = 0;
    y = screen->getBevelWidth();

    for (; it != end; ++it) {
      client = *it;
      x = (frame.width - client->width) / 2;

      XMoveResizeWindow(display, client->window, x, y,
                        client->width, client->height);
      XMapWindow(display, client->window);

      // for ICCCM compliance
      client->x = x;
      client->y = y;

      XEvent event;
      event.type = ConfigureNotify;

      event.xconfigure.display = display;
      event.xconfigure.event = client->window;
      event.xconfigure.window = client->window;
      event.xconfigure.x = x;
      event.xconfigure.y = y;
      event.xconfigure.width = client->width;
      event.xconfigure.height = client->height;
      event.xconfigure.border_width = 0;
      event.xconfigure.above = frame.window;
      event.xconfigure.override_redirect = False;

      XSendEvent(display, client->window, False, StructureNotifyMask, &event);

      y += client->height + screen->getBevelWidth();
    }
    switch (screen->getSlitPlacement()) {
    case TopCenter:
      strut.top = getHeight() + 1;
      break;
    case BottomCenter:
      strut.bottom = (screen->getHeight() - getY()) - 1;
      break;
    case TopLeft:
    case CenterLeft:
    case BottomLeft:
      strut.left = getWidth() + 1;
      break;
    case TopRight:
    case CenterRight:
    case BottomRight:
      strut.right = (screen->getWidth() - getX()) - 1;
      break;
    }
    break;

  case Horizontal:
    x = screen->getBevelWidth();
    y = 0;

    for (; it != end; ++it) {
      client = *it;
      y = (frame.height - client->height) / 2;

      XMoveResizeWindow(display, client->window, x, y,
                        client->width, client->height);
      XMapWindow(display, client->window);

      // for ICCCM compliance
      client->x = x;
      client->y = y;

      XEvent event;
      event.type = ConfigureNotify;

      event.xconfigure.display = display;
      event.xconfigure.event = client->window;
      event.xconfigure.window = client->window;
      event.xconfigure.x = x;
      event.xconfigure.y = y;
      event.xconfigure.width = client->width;
      event.xconfigure.height = client->height;
      event.xconfigure.border_width = 0;
      event.xconfigure.above = frame.window;
      event.xconfigure.override_redirect = False;

      XSendEvent(display, client->window, False, StructureNotifyMask, &event);

      x += client->width + screen->getBevelWidth();
    }
    switch (screen->getSlitPlacement()) {
    case TopCenter:
    case TopLeft:
    case TopRight:
      strut.top = getHeight() + 1;
      break;
    case BottomCenter:
    case BottomLeft:
    case BottomRight:
      strut.bottom = (screen->getHeight() - getY()) - 1;
      break;
    case CenterLeft:
      strut.left = getWidth() + 1;
      break;
    case CenterRight:
      strut.right = (screen->getWidth() - getX()) - 1;
      break;
    }
    break;
  }

  // update area with new Strut info
  screen->updateAvailableArea();

  slitmenu->reconfigure();
}


void Slit::reposition(void) {
  // place the slit in the appropriate place
  switch (screen->getSlitPlacement()) {
  case TopLeft:
    frame.x = 0;
    frame.y = 0;
    if (screen->getSlitDirection() == Vertical) {
      frame.x_hidden = screen->getBevelWidth() - screen->getBorderWidth()
                       - frame.width;
      frame.y_hidden = 0;
    } else {
      frame.x_hidden = 0;
      frame.y_hidden = screen->getBevelWidth() - screen->getBorderWidth()
                       - frame.height;
    }
    break;

  case CenterLeft:
    frame.x = 0;
    frame.y = (screen->getHeight() - frame.height) / 2;
    frame.x_hidden = screen->getBevelWidth() - screen->getBorderWidth()
                     - frame.width;
    frame.y_hidden = frame.y;
    break;

  case BottomLeft:
    frame.x = 0;
    frame.y = screen->getHeight() - frame.height
      - (screen->getBorderWidth() * 2);
    if (screen->getSlitDirection() == Vertical) {
      frame.x_hidden = screen->getBevelWidth() - screen->getBorderWidth()
                       - frame.width;
      frame.y_hidden = frame.y;
    } else {
      frame.x_hidden = 0;
      frame.y_hidden = screen->getHeight() - screen->getBevelWidth()
                       - screen->getBorderWidth();
    }
    break;

  case TopCenter:
    frame.x = (screen->getWidth() - frame.width) / 2;
    frame.y = 0;
    frame.x_hidden = frame.x;
    frame.y_hidden = screen->getBevelWidth() - screen->getBorderWidth()
                     - frame.height;
    break;

  case BottomCenter:
    frame.x = (screen->getWidth() - frame.width) / 2;
    frame.y = screen->getHeight() - frame.height
      - (screen->getBorderWidth() * 2);
    frame.x_hidden = frame.x;
    frame.y_hidden = screen->getHeight() - screen->getBevelWidth()
                     - screen->getBorderWidth();
    break;

  case TopRight:
    frame.x = screen->getWidth() - frame.width
      - (screen->getBorderWidth() * 2);
    frame.y = 0;
    if (screen->getSlitDirection() == Vertical) {
      frame.x_hidden = screen->getWidth() - screen->getBevelWidth()
                       - screen->getBorderWidth();
      frame.y_hidden = 0;
    } else {
      frame.x_hidden = frame.x;
      frame.y_hidden = screen->getBevelWidth() - screen->getBorderWidth()
                       - frame.height;
    }
    break;

  case CenterRight:
  default:
    frame.x = screen->getWidth() - frame.width
      - (screen->getBorderWidth() * 2);
    frame.y = (screen->getHeight() - frame.height) / 2;
    frame.x_hidden = screen->getWidth() - screen->getBevelWidth()
                     - screen->getBorderWidth();
    frame.y_hidden = frame.y;
    break;

  case BottomRight:
    frame.x = screen->getWidth() - frame.width
      - (screen->getBorderWidth() * 2);
    frame.y = screen->getHeight() - frame.height
      - (screen->getBorderWidth() * 2);
    if (screen->getSlitDirection() == Vertical) {
      frame.x_hidden = screen->getWidth() - screen->getBevelWidth()
                       - screen->getBorderWidth();
      frame.y_hidden = frame.y;
    } else {
      frame.x_hidden = frame.x;
      frame.y_hidden = screen->getHeight() - screen->getBevelWidth()
                       - screen->getBorderWidth();
    }
    break;
  }

  Toolbar *tbar = screen->getToolbar();
  int sw = frame.width + (screen->getBorderWidth() * 2),
      sh = frame.height + (screen->getBorderWidth() * 2),
      tw = tbar->getWidth() + screen->getBorderWidth(),
      th = tbar->getHeight() + screen->getBorderWidth();

  if (tbar->getX() < frame.x + sw && tbar->getX() + tw > frame.x &&
      tbar->getY() < frame.y + sh && tbar->getY() + th > frame.y) {
    if (frame.y < th) {
      frame.y += tbar->getExposedHeight();
      if (screen->getSlitDirection() == Vertical)
        frame.y_hidden += tbar->getExposedHeight();
      else
        frame.y_hidden = frame.y;
    } else {
      frame.y -= tbar->getExposedHeight();
      if (screen->getSlitDirection() == Vertical)
        frame.y_hidden -= tbar->getExposedHeight();
      else
        frame.y_hidden = frame.y;
    }
  }

  if (hidden)
    XMoveResizeWindow(display, frame.window, frame.x_hidden,
                      frame.y_hidden, frame.width, frame.height);
  else
    XMoveResizeWindow(display, frame.window, frame.x,
                      frame.y, frame.width, frame.height);
}


void Slit::shutdown(void) {
  while (! clientList.empty())
    removeClient(clientList.front());
}


void Slit::buttonPressEvent(XButtonEvent *e) {
  if (e->window != frame.window) return;

  if (e->button == Button1 && (! on_top)) {
    Window w[1] = { frame.window };
    screen->raiseWindows(w, 1);
  } else if (e->button == Button2 && (! on_top)) {
    XLowerWindow(display, frame.window);
  } else if (e->button == Button3) {
    if (! slitmenu->isVisible()) {
      int x, y;

      x = e->x_root - (slitmenu->getWidth() / 2);
      y = e->y_root - (slitmenu->getHeight() / 2);

      if (x < 0)
        x = 0;
      else if (x + slitmenu->getWidth() > screen->getWidth())
        x = screen->getWidth() - slitmenu->getWidth();

      if (y < 0)
        y = 0;
      else if (y + slitmenu->getHeight() > screen->getHeight())
        y = screen->getHeight() - slitmenu->getHeight();

      slitmenu->move(x, y);
      slitmenu->show();
    } else {
      slitmenu->hide();
    }
  }
}


void Slit::enterNotifyEvent(XCrossingEvent *) {
  if (! do_auto_hide)
    return;

  if (hidden) {
    if (! timer->isTiming()) timer->start();
  } else {
    if (timer->isTiming()) timer->stop();
  }
}


void Slit::leaveNotifyEvent(XCrossingEvent *) {
  if (! do_auto_hide)
    return;

  if (hidden) {
    if (timer->isTiming()) timer->stop();
  } else if (! slitmenu->isVisible()) {
    if (! timer->isTiming()) timer->start();
  }
}


void Slit::configureRequestEvent(XConfigureRequestEvent *e) {
  if (! blackbox->validateWindow(e->window))
    return;

  XWindowChanges xwc;

  xwc.x = e->x;
  xwc.y = e->y;
  xwc.width = e->width;
  xwc.height = e->height;
  xwc.border_width = 0;
  xwc.sibling = e->above;
  xwc.stack_mode = e->detail;

  XConfigureWindow(display, e->window, e->value_mask, &xwc);

  SlitClientList::iterator it = clientList.begin();
  const SlitClientList::iterator end = clientList.end();
  for (; it != end; ++it) {
    SlitClient *client = *it;
    if (client->window == e->window &&
        (client->width != (unsigned) e->width ||
         client->height != (unsigned) e->height)) {
      client->width = (unsigned) e->width;
      client->height = (unsigned) e->height;

      reconfigure();
      return;
    }
  }
}


void Slit::timeout(void) {
  hidden = ! hidden;
  if (hidden)
    XMoveWindow(display, frame.window, frame.x_hidden, frame.y_hidden);
  else
    XMoveWindow(display, frame.window, frame.x, frame.y);
}


Slitmenu::Slitmenu(Slit *sl) : Basemenu(sl->screen) {
  slit = sl;

  setLabel(i18n(SlitSet, SlitSlitTitle, "Slit"));
  setInternalMenu();

  directionmenu = new Directionmenu(this);
  placementmenu = new Placementmenu(this);

  insert(i18n(CommonSet, CommonDirectionTitle, "Direction"),
         directionmenu);
  insert(i18n(CommonSet, CommonPlacementTitle, "Placement"),
         placementmenu);
  insert(i18n(CommonSet, CommonAlwaysOnTop, "Always on top"), 1);
  insert(i18n(CommonSet, CommonAutoHide, "Auto hide"), 2);

  update();

  if (slit->isOnTop()) setItemSelected(2, True);
  if (slit->doAutoHide()) setItemSelected(3, True);
}


Slitmenu::~Slitmenu(void) {
  delete directionmenu;
  delete placementmenu;
}


void Slitmenu::itemSelected(int button, int index) {
  if (button != 1)
    return;

  BasemenuItem *item = find(index);
  if (! item) return;

  switch (item->function()) {
  case 1: { // always on top
    Bool change = ((slit->isOnTop()) ?  False : True);
    slit->on_top = change;
    setItemSelected(2, change);

    if (slit->isOnTop()) slit->screen->raiseWindows((Window *) 0, 0);
    break;
  }

  case 2: { // auto hide
    Bool change = ((slit->doAutoHide()) ?  False : True);
    slit->do_auto_hide = change;
    setItemSelected(3, change);

    break;
  }
  } // switch
}


void Slitmenu::internal_hide(void) {
  Basemenu::internal_hide();
  if (slit->doAutoHide())
    slit->timeout();
}


void Slitmenu::reconfigure(void) {
  directionmenu->reconfigure();
  placementmenu->reconfigure();

  Basemenu::reconfigure();
}


Slitmenu::Directionmenu::Directionmenu(Slitmenu *sm)
  : Basemenu(sm->slit->screen) {
  slitmenu = sm;

  setLabel(i18n(SlitSet, SlitSlitDirection, "Slit Direction"));
  setInternalMenu();

  insert(i18n(CommonSet, CommonDirectionHoriz, "Horizontal"),
         Slit::Horizontal);
  insert(i18n(CommonSet, CommonDirectionVert, "Vertical"),
         Slit::Vertical);

  update();

  if (sm->slit->screen->getSlitDirection() == Slit::Horizontal)
    setItemSelected(0, True);
  else
    setItemSelected(1, True);
}


void Slitmenu::Directionmenu::itemSelected(int button, int index) {
  if (button != 1)
    return;

  BasemenuItem *item = find(index);
  if (! item) return;

  slitmenu->slit->screen->saveSlitDirection(item->function());

  if (item->function() == Slit::Horizontal) {
    setItemSelected(0, True);
    setItemSelected(1, False);
  } else {
    setItemSelected(0, False);
    setItemSelected(1, True);
  }

  hide();
  slitmenu->slit->reconfigure();
}


Slitmenu::Placementmenu::Placementmenu(Slitmenu *sm)
  : Basemenu(sm->slit->screen) {
  slitmenu = sm;

  setLabel(i18n(SlitSet, SlitSlitPlacement, "Slit Placement"));
  setMinimumSublevels(3);
  setInternalMenu();

  insert(i18n(CommonSet, CommonPlacementTopLeft, "Top Left"),
         Slit::TopLeft);
  insert(i18n(CommonSet, CommonPlacementCenterLeft, "Center Left"),
         Slit::CenterLeft);
  insert(i18n(CommonSet, CommonPlacementBottomLeft, "Bottom Left"),
         Slit::BottomLeft);
  insert(i18n(CommonSet, CommonPlacementTopCenter, "Top Center"),
         Slit::TopCenter);
  insert("");
  insert(i18n(CommonSet, CommonPlacementBottomCenter, "Bottom Center"),
         Slit::BottomCenter);
  insert(i18n(CommonSet, CommonPlacementTopRight, "Top Right"),
         Slit::TopRight);
  insert(i18n(CommonSet, CommonPlacementCenterRight, "Center Right"),
         Slit::CenterRight);
  insert(i18n(CommonSet, CommonPlacementBottomRight, "Bottom Right"),
         Slit::BottomRight);

  update();
}


void Slitmenu::Placementmenu::itemSelected(int button, int index) {
  if (button != 1)
    return;

  BasemenuItem *item = find(index);
  if (! (item && item->function())) return;

  slitmenu->slit->screen->saveSlitPlacement(item->function());
  hide();
  slitmenu->slit->reconfigure();
}


// Window.cc for Blackbox - an X11 Window manager
// Copyright (c) 1997 - 1999 by Brad Hughes, bhughes@tcac.net
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// (See the included file COPYING / GPL-2.0)
//

#ifndef   _GNU_SOURCE
#define   _GNU_SOURCE
#endif // _GNU_SOURCE

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

#include "blackbox.hh"
#include "Icon.hh"
#include "Screen.hh"
#include "Toolbar.hh"
#include "Window.hh"
#include "Windowmenu.hh"
#include "Workspace.hh"

#include <X11/Xatom.h>
#include <X11/keysym.h>

#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef    STDC_HEADERS
#  include <string.h>
#endif // STDC_HEADERS


BlackboxWindow::BlackboxWindow(Blackbox *ctrl, BScreen *scrn, Window window) {
  focusable = True;
  moving = resizing = shaded = maximized = visible = iconic = transient =
    focused = stuck = False;

  window_number = -1;
  
  blackbox = ctrl;
  screen = scrn;
  display = blackbox->getDisplay();
  image_ctrl = screen->getImageControl();
  client.window = window;

  frame.window = frame.plate = frame.title = frame.handle = frame.border =
    frame.close_button = frame.iconify_button = frame.maximize_button =
    frame.resize_handle = None;

  frame.utitle = frame.ftitle = frame.uhandle = frame.fhandle =
    frame.ubutton = frame.fbutton = frame.pbutton = frame.uborder =
    frame.fborder = frame.ruhandle = frame.rfhandle = None;

  decorations.titlebar = decorations.border = decorations.handle =
    decorations.iconify = decorations.maximize = decorations.menu = True;
  functions.resize = functions.move = functions.iconify = functions.maximize =
    True;
  functions.close = decorations.close = False;
  
  client.wm_hint_flags = client.normal_hint_flags = 0;
  client.transient_for = client.transient = 0;
  client.pre_icccm = False;
  client.title = 0;
  client.title_len = 0;
  icon = 0;
  client.mwm_hint = 0;
  windowmenu = 0;
  lastButtonPressTime = 0;

  blackbox->grab();
  if (! validateClient()) return;

  // fetch client size and placement
  XWindowAttributes wattrib;
  if (XGetWindowAttributes(display, client.window, &wattrib))  { 
    if (! wattrib.override_redirect) { 
      client.x = wattrib.x;
      client.y = wattrib.y;
      client.width = wattrib.width;
      client.height = wattrib.height;
      client.old_bw = wattrib.border_width;
      
      blackbox->saveWindowSearch(client.window, this);
      
      // get size, aspect, minimum/maximum size and other hints set by the
      // client
      getWMHints();
      getWMNormalHints();
      
      // determine if this is a transient window
      Window win;
      if (XGetTransientForHint(display, client.window, &win))
	if (win && (win != client.window))
	  if ((client.transient_for = blackbox->searchWindow(win))) {
	    client.transient_for->client.transient = this;	  
	    stuck = client.transient_for->stuck;
	    transient = True;
	  } else if (win == client.window_group) {
	    if ((client.transient_for = blackbox->searchGroup(win, this))) {
	      client.transient_for->client.transient = this;
	      stuck = client.transient_for->stuck;
	      transient = True;
	    }
	  } else if (win == screen->getRootWindow()) {
	    client.transient_for = (BlackboxWindow *) 0;
	    transient = True;
	  }

      // edit the window decorations for the transient window
      if (! transient) {
	if ((client.normal_hint_flags & PMinSize) &&
	    (client.normal_hint_flags & PMaxSize))
	  if (client.max_width == client.min_width &&
	      client.max_height == client.min_height)
	    decorations.maximize = decorations.handle =
	      functions.resize = False;
      } else {
	decorations.border = decorations.handle = decorations.maximize =
	  functions.resize = False;
      }
      
      // check for mwm hints and change the window decorations if necessary
      int format;
      Atom atom_return;
      unsigned long num, len;
      
      if (XGetWindowProperty(display, client.window,
                         blackbox->getMotifWMHintsAtom(), 0,
                         PropMwmHintsElements, False,
                         blackbox->getMotifWMHintsAtom(), &atom_return,
                         &format, &num, &len,
                         (unsigned char **) &client.mwm_hint) == Success &&
          client.mwm_hint)
	if (num == PropMwmHintsElements) {
	  if (client.mwm_hint->flags & MwmHintsDecorations)
	    if (client.mwm_hint->decorations & MwmDecorAll)
	      decorations.titlebar = decorations.handle = decorations.border =
		decorations.iconify = decorations.maximize =
		decorations.close = decorations.menu = True;
	    else {
	      decorations.titlebar = decorations.handle = decorations.border =
		decorations.iconify = decorations.maximize =
		decorations.close = decorations.menu = False;
	      
	      if (client.mwm_hint->decorations & MwmDecorBorder)
		decorations.border = True;
	      if (client.mwm_hint->decorations & MwmDecorHandle)
		decorations.handle = True;
	      if (client.mwm_hint->decorations & MwmDecorTitle)
		decorations.titlebar = True;
	      if (client.mwm_hint->decorations & MwmDecorMenu)
		decorations.menu = True;
	      if (client.mwm_hint->decorations & MwmDecorIconify)
		decorations.iconify = True;
	      if (client.mwm_hint->decorations & MwmDecorMaximize)
		decorations.maximize = True;
	    }
	  
	  if (client.mwm_hint->flags & MwmHintsFunctions)
	    if (client.mwm_hint->functions & MwmFuncAll)
	      functions.resize = functions.move = functions.iconify =
		functions.maximize = functions.close = True;
	    else {
	      functions.resize = functions.move = functions.iconify =
		functions.maximize = functions.close = False;
	      
	      if (client.mwm_hint->functions & MwmFuncResize)
		functions.resize = True;
	      if (client.mwm_hint->functions & MwmFuncMove)
		functions.move = True;
	      if (client.mwm_hint->functions & MwmFuncIconify)
		functions.iconify = True;
	      if (client.mwm_hint->functions & MwmFuncMaximize)
		functions.maximize = True;
	      if (client.mwm_hint->functions & MwmFuncClose)
	    functions.close = True;
	    }
	}
      
      frame.bevel_w = screen->getBevelWidth();
      frame.border_w = (client.width + (frame.bevel_w * 2)) *
        decorations.border;
      frame.border_h = (client.height + (frame.bevel_w * 2)) *
        decorations.border;
      frame.title_h = (screen->getTitleFont()->ascent +
	               screen->getTitleFont()->descent +
                       (frame.bevel_w * 2)) * decorations.titlebar;
      frame.button_h = frame.title_h - 4 - (screen->getBorderWidth() * 2);
      frame.button_w = frame.button_h;
      frame.rh_w = decorations.handle * screen->getHandleWidth();
      frame.rh_h = decorations.handle * frame.button_h;
      frame.handle_w = decorations.handle * screen->getHandleWidth();
      frame.handle_h =
       (((decorations.border) ? frame.border_h : client.height) -
        frame.rh_h - screen->getBorderWidth()) * decorations.handle;
      frame.title_w = ((decorations.border) ? frame.border_w : client.width) +
	((frame.handle_w + screen->getBorderWidth()) * decorations.handle);
      frame.y_border = (frame.title_h + screen->getBorderWidth()) *
        decorations.titlebar;
      frame.x_handle = ((decorations.border) ?
                        frame.border_w + screen->getBorderWidth() :
			client.width + screen->getBorderWidth());
      frame.width = frame.title_w;
      frame.height = ((decorations.border) ? frame.border_h : client.height) +
	frame.y_border;
      
      Bool place_window = True;
      if (blackbox->isStartup() || transient ||
	  client.normal_hint_flags & (PPosition|USPosition)) {
	setGravityOffsets();
	
	if ((blackbox->isStartup()) ||
	    (frame.x >= 0 &&
             (signed) (frame.y + frame.y_border) >= 0 &&
	     frame.x <= (signed) screen->getWidth() &&
	     frame.y <= (signed) screen->getHeight()))
	  place_window = False;
      }
      
      frame.window =
        createToplevelWindow(frame.x, frame.y, frame.width,
			     frame.height, screen->getBorderWidth());
      blackbox->saveWindowSearch(frame.window, this);

      frame.plate =
        createChildWindow(frame.window, (frame.bevel_w * decorations.border),
                          frame.y_border +
                          (frame.bevel_w * decorations.border),
                          client.width, client.height, 0l);
      blackbox->saveWindowSearch(frame.plate, this);
      
      if (decorations.titlebar) {
	frame.title = createChildWindow(frame.window, 0, 0, frame.title_w,
					frame.title_h, 0l);
	blackbox->saveWindowSearch(frame.title, this);
      }
      
      if (decorations.border) {
	frame.border = createChildWindow(frame.window, 0, frame.y_border,
					 frame.border_w, frame.border_h, 0l);
	blackbox->saveWindowSearch(frame.border, this);
      }
      
      if (decorations.handle) {
	frame.handle = createChildWindow(frame.window, frame.x_handle,
					 frame.y_border, frame.handle_w,
					 frame.handle_h, 0l);
	blackbox->saveWindowSearch(frame.handle, this);
	
	frame.resize_handle =
	  createChildWindow(frame.window, frame.x_handle, frame.y_border +
			    frame.handle_h + screen->getBorderWidth(),
                            frame.rh_w, frame.rh_h, 0l);
	blackbox->saveWindowSearch(frame.resize_handle, this);
      }
      
      getWMProtocols();
      associateClientWindow();
      positionButtons();
      
      XGrabButton(display, Button1, Mod1Mask, frame.window, True,
		  ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
		  GrabModeAsync, frame.window, blackbox->getMoveCursor());
      
      XRaiseWindow(display, frame.plate);
      XMapSubwindows(display, frame.plate);
      if (decorations.titlebar) XMapSubwindows(display, frame.title);
      XMapSubwindows(display, frame.window);
      
      if (decorations.menu)
	windowmenu = new Windowmenu(this, blackbox);
      
      createDecorations();

#ifdef    KDE
      Atom ajunk;
      
      int ijunk;
      unsigned long uljunk, *ret = 0;
      
      if (XGetWindowProperty(display, client.window,
			     blackbox->getKWMWinStickyAtom(), 0l, 1l, False,
			     blackbox->getKWMWinStickyAtom(), &ajunk, &ijunk,
			     &uljunk, &uljunk,
			     (unsigned char **) &ret) == Success && ret) {
	stuck = *ret;
	XFree((char *) ret);
      }
#endif // KDE

      screen->getCurrentWorkspace()->addWindow(this, place_window);

      configure(frame.x, frame.y, frame.width, frame.height); 

#ifdef    KDE
      screen->addKWMWindow(client.window);
#endif // KDE

      setFocusFlag(False); 
    }
  }
  
  blackbox->ungrab();
}  


BlackboxWindow::~BlackboxWindow(void) {
  blackbox->grab();
  
  if (moving || resizing) XUngrabPointer(display, CurrentTime);

#ifdef    KDE
  screen->removeKWMWindow(client.window);
#endif // KDE

  screen->getWorkspace(workspace_number)->removeWindow(this);

  if (blackbox->getFocusedWindow() == this)
    // the workspace didn't set the focus window to zero when we removed
    // ourself... can't send a boy to do a man's job
    blackbox->setFocusedWindow((BlackboxWindow *) 0);

  
  if (windowmenu) delete windowmenu;
  if (icon) delete icon;

  if (client.title)
    if (strcmp(client.title, "Unnamed")) {
      XFree(client.title);
      client.title = 0;
    }
  
  if (client.mwm_hint) {
    XFree(client.mwm_hint);
    client.mwm_hint = 0;
  }
  
  if (client.window_group)
    blackbox->removeGroupSearch(client.window_group);
  
  if (transient && client.transient_for)
    client.transient_for->client.transient = 0;
  
  if (frame.close_button != None) {
    blackbox->removeWindowSearch(frame.close_button);
    XDestroyWindow(display, frame.close_button);
  }
  
  if (frame.iconify_button != None) {
    blackbox->removeWindowSearch(frame.iconify_button);
    XDestroyWindow(display, frame.iconify_button);
  }
  
  if (frame.maximize_button != None) {
    blackbox->removeWindowSearch(frame.maximize_button);
    XDestroyWindow(display, frame.maximize_button);
  }
  
  if (frame.title != None) {
    if (frame.ftitle) {
      image_ctrl->removeImage(frame.ftitle);
      frame.ftitle = None;
    }
    
    if (frame.utitle) {
      image_ctrl->removeImage(frame.utitle);
      frame.utitle = None;
    }
    
    blackbox->removeWindowSearch(frame.title);
    XDestroyWindow(display, frame.title);
  }
  
  if (frame.handle != None) {
    if (frame.fhandle) {
      image_ctrl->removeImage(frame.fhandle);
      frame.fhandle = None;
    }
    
    if (frame.uhandle) {
      image_ctrl->removeImage(frame.uhandle);
      frame.uhandle = None;
    }

    if (frame.rfhandle) {
      image_ctrl->removeImage(frame.rfhandle);
      frame.rfhandle = None;
    }

    if (frame.ruhandle) {
      image_ctrl->removeImage(frame.ruhandle);
      frame.ruhandle = None;
    }

    blackbox->removeWindowSearch(frame.handle);
    blackbox->removeWindowSearch(frame.resize_handle);
    XDestroyWindow(display, frame.resize_handle);
    XDestroyWindow(display, frame.handle);
  }
  
  if (frame.border != None) {
    if (frame.fborder) {
      image_ctrl->removeImage(frame.fborder);
      frame.fborder = None;
    }

    if (frame.uborder) {
      image_ctrl->removeImage(frame.uborder);
      frame.uborder = None;
    }
    
    blackbox->removeWindowSearch(frame.border);
    XDestroyWindow(display, frame.border);
  }
  
  if (frame.fbutton != None) {
    image_ctrl->removeImage(frame.fbutton);
    frame.fbutton = None;
  }

  if (frame.ubutton != None) {
    image_ctrl->removeImage(frame.ubutton);
    frame.ubutton = None;
  }

  if (frame.pbutton != None) {
    image_ctrl->removeImage(frame.pbutton);
    frame.pbutton = None;
  }

  blackbox->removeWindowSearch(client.window);
  
  blackbox->removeWindowSearch(frame.plate);
  blackbox->removeWindowSearch(frame.window);
  XDestroyWindow(display, frame.plate);
  XDestroyWindow(display, frame.window);
  
  blackbox->ungrab();
}


Window BlackboxWindow::createToplevelWindow(int x, int y, unsigned int width,
					    unsigned int height,
					    unsigned int borderwidth)
{
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWBackPixmap | CWBackPixel | CWBorderPixel |
    CWOverrideRedirect | CWEventMask; 
  
  attrib_create.background_pixmap = None;
  attrib_create.background_pixel = attrib_create.border_pixel =
    screen->getBorderColor()->getPixel();
  attrib_create.override_redirect = True;
  attrib_create.event_mask = ButtonPressMask | EnterWindowMask;
  
  return (XCreateWindow(display, screen->getRootWindow(), x, y, width, height,
			borderwidth, screen->getDepth(), InputOutput,
			screen->getVisual(), create_mask,
			&attrib_create));
}


Window BlackboxWindow::createChildWindow(Window parent, int x, int y,
					 unsigned int width,
					 unsigned int height,
					 unsigned int borderwidth)
{
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWBackPixmap | CWBackPixel | CWBorderPixel |
    CWEventMask;
  
  attrib_create.background_pixmap = None;
  attrib_create.background_pixel = attrib_create.border_pixel = 
    screen->getBorderColor()->getPixel();
  attrib_create.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask |
    ButtonReleaseMask | ExposureMask | EnterWindowMask | LeaveWindowMask |
    ButtonMotionMask;
  
  return (XCreateWindow(display, parent, x, y, width, height, borderwidth,
			screen->getDepth(), InputOutput, screen->getVisual(),
			create_mask, &attrib_create));
}


void BlackboxWindow::associateClientWindow(void) {
  XSetWindowBorderWidth(display, client.window, 0);
  if (! XFetchName(display, client.window, &client.title))
    client.title = "Unnamed";
  client.title_len = strlen(client.title);
  client.title_text_w = XTextWidth(screen->getTitleFont(), client.title,
				   client.title_len);

  XChangeSaveSet(display, client.window, SetModeInsert);
  XSetWindowAttributes attrib_set;

  XSelectInput(display, frame.plate, NoEventMask);
  XReparentWindow(display, client.window, frame.plate, 0, 0);
  XSelectInput(display, frame.plate, SubstructureRedirectMask);
  
  XFlush(display);

  attrib_set.event_mask = PropertyChangeMask | StructureNotifyMask;
  attrib_set.do_not_propagate_mask = ButtonPressMask | ButtonReleaseMask |
    ButtonMotionMask;

  XChangeWindowAttributes(display, client.window, CWEventMask|CWDontPropagate,
                          &attrib_set);

#ifdef    SHAPE
  if (blackbox->hasShapeExtensions()) {
    XShapeSelectInput(display, client.window, ShapeNotifyMask);
    
    int foo;
    unsigned int ufoo;

    XShapeQueryExtents(display, client.window, &frame.shaped, &foo, &foo,
		       &ufoo, &ufoo, &foo, &foo, &foo, &ufoo, &ufoo);
    
    if (frame.shaped) {
      XShapeCombineShape(display, frame.window, ShapeBounding,
			 (frame.bevel_w * decorations.border),
			 frame.y_border + (frame.bevel_w * decorations.border),
			 client.window, ShapeBounding, ShapeSet);
      
      int num = 1;
      XRectangle xrect[2];
      xrect[0].x = xrect[0].y = 0;
      xrect[0].width = frame.width;
      xrect[0].height = frame.y_border;
      
      if (decorations.handle) {
	xrect[1].x = frame.x_handle;
	xrect[1].y = frame.y_border;
	xrect[1].width = frame.handle_w;
	xrect[1].height = frame.handle_h + frame.rh_h +
          screen->getBorderWidth();
	num++;
      }
      
      XShapeCombineRectangles(display, frame.window, ShapeBounding, 0, 0,
			      xrect, num, ShapeUnion, Unsorted);
    }
  }
#endif // SHAPE
  
  if (functions.iconify) createIconifyButton();
  if (functions.maximize) createMaximizeButton();
  
  if (frame.ubutton && frame.close_button)
    XSetWindowBackgroundPixmap(display, frame.close_button, frame.ubutton);
  if (frame.ubutton && frame.maximize_button)
    XSetWindowBackgroundPixmap(display, frame.maximize_button, frame.ubutton);
  if (frame.ubutton && frame.iconify_button)
    XSetWindowBackgroundPixmap(display, frame.iconify_button, frame.ubutton);
}


void BlackboxWindow::createDecorations(void) {
  if (decorations.titlebar) {
    frame.ftitle =
      image_ctrl->renderImage(frame.title_w, frame.title_h,
			      &(screen->getWResource()->
				decoration.focusTexture));
    frame.utitle =
      image_ctrl->renderImage(frame.title_w, frame.title_h,
			      &(screen->getWResource()->
				decoration.unfocusTexture));
  }

  if (decorations.border) {
    frame.fborder =
      image_ctrl->renderImage(frame.border_w, frame.border_h,
			      &(screen->getWResource()->frame.texture));
    XSetWindowBackgroundPixmap(display, frame.border, frame.fborder);
    XClearWindow(display, frame.border);
  }

  if (decorations.handle) {
    frame.fhandle =
      image_ctrl->renderImage(frame.handle_w, frame.handle_h,
			      &(screen->getWResource()->
				decoration.focusTexture));
    frame.uhandle =
      image_ctrl->renderImage(frame.handle_w, frame.handle_h,
			      &(screen->getWResource()->
				decoration.unfocusTexture));
    
    frame.rfhandle =
      image_ctrl->renderImage(frame.rh_w, frame.rh_h,
			      &(screen->getWResource()->button.focusTexture));
    frame.ruhandle =
      image_ctrl->renderImage(frame.rh_w, frame.rh_h,
			      &(screen->getWResource()->
				button.unfocusTexture));
  }
  
  frame.fbutton =
    image_ctrl->renderImage(frame.button_w, frame.button_h,
			    &(screen->getWResource()->button.focusTexture));
  frame.ubutton =
    image_ctrl->renderImage(frame.button_w, frame.button_h,
			    &(screen->getWResource()->button.unfocusTexture));
  frame.pbutton =
    image_ctrl->renderImage(frame.button_w, frame.button_h,
			    &(screen->getWResource()->button.pressedTexture));
}


void BlackboxWindow::createCloseButton(void) {
  if (decorations.close && frame.title != None &&
      frame.close_button == None) {
    frame.close_button =
      createChildWindow(frame.title, 0, 0, frame.button_w, frame.button_h,
                        screen->getBorderWidth());
    if (frame.ubutton != None)
      XSetWindowBackgroundPixmap(display, frame.close_button, frame.ubutton);
    blackbox->saveWindowSearch(frame.close_button, this);
  }
}


void BlackboxWindow::createIconifyButton(void) {
  if (decorations.iconify && frame.title != None) {
    frame.iconify_button =
      createChildWindow(frame.title, 0, 0, frame.button_w, frame.button_h,
                        screen->getBorderWidth());
    blackbox->saveWindowSearch(frame.iconify_button, this);
  }
}


void BlackboxWindow::createMaximizeButton(void) {
  if (decorations.maximize && frame.title != None) {
    frame.maximize_button =
      createChildWindow(frame.title, 0, 0, frame.button_w, frame.button_h,
                        screen->getBorderWidth());
    blackbox->saveWindowSearch(frame.maximize_button, this);
  }
}


void BlackboxWindow::positionButtons(void) {
  unsigned int bw = (frame.button_w + 2 + (screen->getBorderWidth() * 2));

  if ((frame.title_w > (bw * 6)) &&
      (client.title_text_w + 4 + ((bw * 3) + 2) < frame.title_w)) {
    if (decorations.iconify && frame.iconify_button != None) {
      XMoveResizeWindow(display, frame.iconify_button, 2, 2, frame.button_w,
			frame.button_h);
      XMapWindow(display, frame.iconify_button);
      XClearWindow(display, frame.iconify_button);
    }

    int bx = frame.title_w - bw;

    if (decorations.close && frame.close_button != None) {
      XMoveResizeWindow(display, frame.close_button, bx, 2, frame.button_w,
			frame.button_h);
      XMapWindow(display, frame.close_button);
      XClearWindow(display, frame.close_button);

      bx -= bw;
    }

    if (decorations.maximize && frame.maximize_button != None) {
      XMoveResizeWindow(display, frame.maximize_button, bx, 2, frame.button_w,
                        frame.button_h);
      XMapWindow(display, frame.maximize_button);
      XClearWindow(display, frame.maximize_button);
    }

    drawAllButtons();
  } else {
    if (frame.iconify_button) XUnmapWindow(display, frame.iconify_button);
    if (frame.maximize_button) XUnmapWindow(display, frame.maximize_button);
    if (frame.close_button) XUnmapWindow(display, frame.close_button);
  }
}


void BlackboxWindow::reconfigure(void) {
  frame.bevel_w = screen->getBevelWidth();
  frame.border_w = (client.width + (frame.bevel_w * 2)) *
    decorations.border;
  frame.border_h = (client.height + (frame.bevel_w * 2)) *
    decorations.border;
  frame.title_h = (screen->getTitleFont()->ascent +
		   screen->getTitleFont()->descent +
		   (frame.bevel_w * 2)) * decorations.titlebar;
  frame.button_h = frame.title_h - 4 - (screen->getBorderWidth() * 2);
  frame.button_w = frame.button_h;
  frame.rh_w = screen->getHandleWidth() * decorations.handle;
  frame.rh_h = frame.button_h  * decorations.handle;
  frame.handle_w = screen->getHandleWidth() * decorations.handle;
  frame.handle_h =
    (((decorations.border) ? frame.border_h : client.height) -
     frame.rh_h - screen->getBorderWidth()) * decorations.handle;
  frame.title_w = ((decorations.border) ? frame.border_w : client.width) +
    ((frame.handle_w + screen->getBorderWidth()) * decorations.handle);
  frame.y_border = (frame.title_h + screen->getBorderWidth()) *
    decorations.titlebar;
  frame.x_handle = ((decorations.border) ?
                    frame.border_w + screen->getBorderWidth() :
                    client.width + screen->getBorderWidth());
  frame.width = frame.title_w;
  frame.height = ((decorations.border) ? frame.border_h : client.height) +
    frame.y_border;
 
  client.x = frame.x + (decorations.border * frame.bevel_w) +
    screen->getBorderWidth();
  client.y = frame.y + frame.y_border + (decorations.border * frame.bevel_w) +
    screen->getBorderWidth();

  if (client.title)
    client.title_text_w = XTextWidth(screen->getTitleFont(), client.title,
                                     client.title_len);

  XResizeWindow(display, frame.window, frame.width,
                ((shaded) ? frame.title_h : frame.height));
  XSetWindowBorderWidth(display, frame.window, screen->getBorderWidth());

  XMoveResizeWindow(display, frame.plate, (frame.bevel_w * decorations.border),
                    frame.y_border + (frame.bevel_w * decorations.border),
                    client.width, client.height);
  XMoveResizeWindow(display, client.window, 0, 0, client.width, client.height); 

  if (decorations.titlebar)
    XResizeWindow(display, frame.title, frame.title_w, frame.title_h);
  
  if (decorations.border)
    XMoveResizeWindow(display, frame.border, 0, frame.y_border,
		      frame.border_w, frame.border_h);  

  if (decorations.handle) {
    XMoveResizeWindow(display, frame.handle, frame.x_handle, frame.y_border,
		      frame.handle_w, frame.handle_h);
    
    XMoveResizeWindow(display, frame.resize_handle, frame.x_handle,
                      frame.y_border + frame.handle_h +
                        screen->getBorderWidth(),
		      frame.rh_w, frame.rh_h);
  }

#ifdef    SHAPE
  if (blackbox->hasShapeExtensions()) {
    if (frame.shaped) {
      XShapeCombineShape(display, frame.window, ShapeBounding,
			 (frame.bevel_w * decorations.border),
			 frame.y_border + (frame.bevel_w * decorations.border),
			 client.window, ShapeBounding, ShapeSet);
      
      int num = 1;
      XRectangle xrect[2];
      xrect[0].x = xrect[0].y = 0;
      xrect[0].width = frame.width;
      xrect[0].height = frame.y_border;
      
      if (decorations.handle) {
	xrect[1].x = frame.x_handle;
	xrect[1].y = frame.y_border;
	xrect[1].width = frame.handle_w;
	xrect[1].height = frame.handle_h + frame.rh_h +
          screen->getBorderWidth();
	num++;
      }
      
      XShapeCombineRectangles(display, frame.window, ShapeBounding, 0, 0,
			      xrect, num, ShapeUnion, Unsorted);
    }
  }
#endif // SHAPE

  Pixmap tmp = frame.fbutton;
  
  frame.fbutton =
    image_ctrl->renderImage(frame.button_w, frame.button_h,
			    &(screen->getWResource()->button.focusTexture));
  if (tmp) image_ctrl->removeImage(tmp);
  
  tmp = frame.ubutton;
  frame.ubutton =
    image_ctrl->renderImage(frame.button_w, frame.button_h,
			    &(screen->getWResource()->button.unfocusTexture));
  if (tmp) image_ctrl->removeImage(tmp);

  tmp = frame.pbutton;
  frame.pbutton =
    image_ctrl->renderImage(frame.button_w, frame.button_h,
			    &(screen->getWResource()->button.pressedTexture));
  if (tmp) image_ctrl->removeImage(tmp);
  
  if (frame.iconify_button) {
    XSetWindowBorder(display, frame.iconify_button,
		     screen->getBorderColor()->getPixel());
    XSetWindowBorderWidth(display, frame.iconify_button,
                          screen->getBorderWidth());
  }

  if (frame.maximize_button) {
    XSetWindowBorder(display, frame.maximize_button,
		     screen->getBorderColor()->getPixel());
    XSetWindowBorderWidth(display, frame.maximize_button,
                          screen->getBorderWidth());
  }

  if (frame.close_button) {
    XSetWindowBorder(display, frame.close_button,
		     screen->getBorderColor()->getPixel());
    XSetWindowBorderWidth(display, frame.close_button,
                          screen->getBorderWidth());
  }

  positionButtons();
  
  if (decorations.titlebar) {
    tmp = frame.ftitle;
    frame.ftitle =
      image_ctrl->renderImage(frame.title_w, frame.title_h,
			      &(screen->getWResource()->
				decoration.focusTexture));
    if (tmp) image_ctrl->removeImage(tmp);

    tmp = frame.utitle;
    frame.utitle =
      image_ctrl->renderImage(frame.title_w, frame.title_h,
			      &(screen->getWResource()->
				decoration.unfocusTexture));
    if (tmp) image_ctrl->removeImage(tmp);
  }

  if (decorations.border) {
    tmp = frame.fborder;
    frame.fborder =
      image_ctrl->renderImage(frame.border_w, frame.border_h,
			      &(screen->getWResource()->frame.texture));
    if (tmp) image_ctrl->removeImage(tmp);
    XSetWindowBackgroundPixmap(display, frame.border, frame.fborder);
    XClearWindow(display, frame.border);
  }

  if (decorations.handle) {
    tmp = frame.fhandle;
    frame.fhandle =
      image_ctrl->renderImage(frame.handle_w, frame.handle_h,
			      &(screen->getWResource()->
				decoration.focusTexture));
    if (tmp) image_ctrl->removeImage(tmp);
    
    tmp = frame.uhandle;
    frame.uhandle =
      image_ctrl->renderImage(frame.handle_w, frame.handle_h,
			      &(screen->getWResource()->
				decoration.unfocusTexture));
    if (tmp) image_ctrl->removeImage(tmp);
    
    tmp = frame.rfhandle;
    frame.rfhandle =
      image_ctrl->renderImage(frame.rh_w, frame.rh_h,
			      &(screen->getWResource()->button.focusTexture));
    if (tmp) image_ctrl->removeImage(tmp);
    
    tmp = frame.ruhandle;
    frame.ruhandle =
      image_ctrl->renderImage(frame.rh_w, frame.rh_h,
			      &(screen->getWResource()->
				button.unfocusTexture));
    if (tmp) image_ctrl->removeImage(tmp);
  }
  
  XSetWindowBorder(display, frame.window,
		   screen->getBorderColor()->getPixel());
  XSetWindowBackground(display, frame.window,
		       screen->getBorderColor()->getPixel());
  XSetWindowBackground(display, frame.plate, 
                       screen->getBorderColor()->getPixel());
  XClearWindow(display, frame.window);
  setFocusFlag(focused);
  if (decorations.titlebar) drawTitleWin();
  drawAllButtons();

  configure(frame.x, frame.y, frame.width, frame.height);
  
  if (windowmenu) {
    windowmenu->move(windowmenu->getX(), frame.y + frame.title_h);
    windowmenu->reconfigure();
  }
}


void BlackboxWindow::getWMProtocols(void) {
  Atom *proto;
  int num_return = 0;

  blackbox->grab();
  if (! validateClient()) return;
  
  if (XGetWMProtocols(display, client.window, &proto, &num_return)) {
    for (int i = 0; i < num_return; ++i) {
      if (proto[i] == blackbox->getWMDeleteAtom()) {
	functions.close = decorations.close = True;
	createCloseButton();
	positionButtons();
	if (windowmenu) windowmenu->setClosable();
      } else if (proto[i] ==  blackbox->getWMFocusAtom()) {
	focusable = True;
      } else if (proto[i] ==  blackbox->getWMStateAtom()) {
	unsigned long state = 0;
	getState(&state);
	
	if (state) {
	  switch (state) {
	  case WithdrawnState:
	    withdraw();
	    setFocusFlag(False);
	    break;
	    
	  case IconicState:
	    iconify();
	    break;
	    
	  case NormalState:
	  default:
	    deiconify();
	    setFocusFlag(False);
	    break;
	  }
	}
      } else  if (proto[i] ==  blackbox->getWMColormapAtom()) {
      }
    }
    
    XFree(proto);
  }
  
  blackbox->ungrab();
}


void BlackboxWindow::getWMHints(void) {
  blackbox->grab();
  if (! validateClient()) return;
  
  XWMHints *wmhint = XGetWMHints(display, client.window);
  if (! wmhint) {
    visible = True;
    iconic = False;
    focus_mode = F_Passive;
    client.window_group = None;
    client.initial_state = NormalState;
  } else {
    client.wm_hint_flags = wmhint->flags;
    if (wmhint->flags & InputHint) {
      if (wmhint->input == True) {
	if (focusable)
	  focus_mode = F_LocallyActive;
	else
	  focus_mode = F_Passive;
      } else {
	if (focusable)
	  focus_mode = F_GloballyActive;
	else
	  focus_mode = F_NoInput;
      }
    } else
      focus_mode = F_Passive;
    
    if (wmhint->flags & StateHint)
      client.initial_state = wmhint->initial_state;
    else
      client.initial_state = NormalState;
    
    if (wmhint->flags & WindowGroupHint) {
      if (! client.window_group) {
	client.window_group = wmhint->window_group;
	blackbox->saveGroupSearch(client.window_group, this);
      }
    } else
      client.window_group = None;

    XFree(wmhint);
  }
  
  blackbox->ungrab();
}


void BlackboxWindow::getWMNormalHints(void) {
  blackbox->grab();
  if (! validateClient()) return;
  
  long icccm_mask;
  XSizeHints sizehint;
  if (! XGetWMNormalHints(display, client.window, &sizehint, &icccm_mask)) {
    client.min_width = client.min_height =
      client.base_width = client.base_height =
      client.width_inc = client.height_inc = 1;
    client.max_width = screen->getWidth();
    client.max_height = screen->getHeight();
    client.min_aspect_x = client.min_aspect_y =
      client.max_aspect_x = client.max_aspect_y = 1;
    client.win_gravity = NorthWestGravity;
  } else {
    if (icccm_mask == (USPosition | USSize | PPosition | PSize | PMinSize |
                       PMaxSize | PResizeInc | PAspect))
      client.pre_icccm = True;

    client.normal_hint_flags = sizehint.flags;

    if (sizehint.flags & PMinSize) {
      client.min_width = sizehint.min_width;
      client.min_height = sizehint.min_height;
    } else
      client.min_width = client.min_height = 1;
    
    if (sizehint.flags & PMaxSize) {
      client.max_width = sizehint.max_width;
      client.max_height = sizehint.max_height;
    } else {
      client.max_width = screen->getWidth();
      client.max_height = screen->getHeight();
    }
    
    if (sizehint.flags & PResizeInc) {
      client.width_inc = sizehint.width_inc;
      client.height_inc = sizehint.height_inc;
    } else
      client.width_inc = client.height_inc = 1;
    
    if (sizehint.flags & PAspect) {
      client.min_aspect_x = sizehint.min_aspect.x;
      client.min_aspect_y = sizehint.min_aspect.y;
      client.max_aspect_x = sizehint.max_aspect.x;
      client.max_aspect_y = sizehint.max_aspect.y;
    } else
      client.min_aspect_x = client.min_aspect_y =
	client.max_aspect_x = client.max_aspect_y = 1;    
    
    if (sizehint.flags & PBaseSize) {
      client.base_width = sizehint.base_width;
      client.base_height = sizehint.base_height;
    } else
      client.base_width = client.base_height = 0;
    
    if (sizehint.flags & PWinGravity)
      client.win_gravity = sizehint.win_gravity;
    else
      client.win_gravity = NorthWestGravity;
  }

  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
    client.gravx_offset = client.gravy_offset = -1;
    break;

  case NorthGravity:
    client.gravx_offset = 0;
    client.gravy_offset = -1;
    break;

  case NorthEastGravity:
    client.gravx_offset = 1;
    client.gravy_offset = -1;
    break;

  case WestGravity:
    client.gravx_offset = -1;
    client.gravy_offset = 0;
    break;

  case EastGravity:
    client.gravx_offset = 1;
    client.gravy_offset = 0;
    break;

  case SouthWestGravity:
    client.gravx_offset = -1;
    client.gravy_offset = 1;
    break;

  case SouthGravity:
    client.gravx_offset = 0;
    client.gravy_offset = 1;
    break;

  case SouthEastGravity:
    client.gravx_offset = 1;
    client.gravy_offset = 1;
    break;

  case StaticGravity:
  case ForgetGravity:
  case CenterGravity:
    client.gravx_offset = client.gravy_offset = 0;
    break;
  }

  blackbox->ungrab();
}


void BlackboxWindow::configure(int dx, int dy,
                               unsigned int dw, unsigned int dh)
{
  blackbox->grab();
  if (! validateClient()) return;

  if ((dw != frame.width) || (dh != frame.height)) {
    if ((((signed) frame.width) + dx) < 0) dx = 0;
    if ((((signed) frame.height) + dy) < 0) dy = 0;

    frame.x = dx;
    frame.y = dy;
    frame.width = dw;
    frame.height = dh;
    
    frame.border_w = (frame.width -
      ((frame.handle_w + screen->getBorderWidth()) * decorations.handle)) *
      decorations.border;
    frame.border_h = (frame.height - frame.y_border) * decorations.border;
    frame.title_w = frame.width;
    frame.handle_h = (dh - frame.y_border - frame.rh_h -
                      screen->getBorderWidth()) * decorations.handle;
    client.x = dx + (decorations.border * frame.bevel_w) +
      screen->getBorderWidth();
    client.y = dy + frame.y_border + (decorations.border * frame.bevel_w) +
      screen->getBorderWidth();
    client.width =
      ((decorations.border) ? frame.border_w - (frame.bevel_w * 2) :
       frame.width - ((frame.handle_w + screen->getBorderWidth()) *
                      decorations.handle));
    client.height = ((decorations.border) ?
		     frame.border_h - (frame.bevel_w * 2) :
		     frame.height - frame.y_border);
    frame.x_handle = ((decorations.border) ?
                      frame.border_w + screen->getBorderWidth() :
		      client.width + screen->getBorderWidth());

    XMoveResizeWindow(display, frame.window, frame.x, frame.y, frame.width,
                      frame.height);
    XMoveResizeWindow(display, frame.plate,
                      (frame.bevel_w * decorations.border),
                      frame.y_border + (frame.bevel_w * decorations.border),
                      client.width, client.height);
    XMoveResizeWindow(display, client.window, 0, 0,
                      client.width, client.height);

    if (decorations.titlebar)
      XResizeWindow(display, frame.title, frame.title_w, frame.title_h);

    positionButtons();
    
    if (decorations.handle) {
      XMoveResizeWindow(display, frame.handle, frame.x_handle,
			frame.y_border, frame.handle_w, frame.handle_h);
      
      XMoveWindow(display, frame.resize_handle, frame.x_handle,
		  frame.y_border + frame.handle_h + screen->getBorderWidth());
    }
    
    if (decorations.border)
      XMoveResizeWindow(display, frame.border, 0, frame.y_border,
                        frame.border_w, frame.border_h);
    Pixmap tmp;
    
    if (decorations.titlebar) {
      tmp = frame.ftitle;
      frame.ftitle =
	image_ctrl->renderImage(frame.title_w, frame.title_h,
				&(screen->getWResource()->
				  decoration.focusTexture));
      if (tmp) image_ctrl->removeImage(tmp);
      
      tmp = frame.utitle;
      frame.utitle =
	image_ctrl->renderImage(frame.title_w, frame.title_h,
				&(screen->getWResource()->
				  decoration.unfocusTexture));
      if (tmp) image_ctrl->removeImage(tmp);
    }
    
    if (decorations.handle) {
      tmp = frame.fhandle;
      frame.fhandle =
	image_ctrl->renderImage(frame.handle_w, frame.handle_h,
				&(screen->getWResource()->
				  decoration.focusTexture));
      if (tmp) image_ctrl->removeImage(tmp);
      
      tmp = frame.uhandle;
      frame.uhandle =
	image_ctrl->renderImage(frame.handle_w, frame.handle_h,
				&(screen->getWResource()->
				  decoration.unfocusTexture));
      if (tmp) image_ctrl->removeImage(tmp);
    }
    
    setFocusFlag(focused);
    
    if (decorations.border) {
      tmp = frame.fborder;
      frame.fborder =
	image_ctrl->renderImage(frame.border_w, frame.border_h,
			        &(screen->getWResource()->frame.texture));
      if (tmp) image_ctrl->removeImage(tmp);
      XSetWindowBackgroundPixmap(display, frame.border, frame.fborder);
      XClearWindow(display, frame.border);
    }
    
    if (decorations.titlebar) drawTitleWin();
    drawAllButtons();
  } else {
    frame.x = dx;
    frame.y = dy;
    
    client.x = dx + (frame.bevel_w * decorations.border) +
      screen->getBorderWidth();
    client.y = dy + frame.y_border + (frame.bevel_w * decorations.border) + 
      screen->getBorderWidth();
    
    XMoveWindow(display, frame.window, frame.x, frame.y);

    if (! moving) {
      XEvent event;               
      event.type = ConfigureNotify;

      event.xconfigure.display = display;
      event.xconfigure.event = client.window;
      event.xconfigure.window = client.window;
      event.xconfigure.x = client.x;
      event.xconfigure.y = client.y;
      event.xconfigure.width = client.width;
      event.xconfigure.height = client.height;
      event.xconfigure.border_width = client.old_bw;
      event.xconfigure.above = frame.window;
      event.xconfigure.override_redirect = False;

      XSendEvent(display, client.window, False, StructureNotifyMask, &event);
    }
  }

  blackbox->ungrab();
}


Bool BlackboxWindow::setInputFocus(void) {
  if (((signed) (frame.x + frame.width)) < 0) {
    if (((signed) (frame.y + frame.y_border)) < 0)
      configure(screen->getBorderWidth(), screen->getBorderWidth(),
                frame.width, frame.height);
    else if (frame.y > (signed) screen->getHeight())
      configure(screen->getBorderWidth(), screen->getHeight() - frame.height,
                frame.width, frame.height);
    else
      configure(screen->getBorderWidth(), frame.y + screen->getBorderWidth(),
                frame.width, frame.height);
  } else if (frame.x > (signed) screen->getWidth()) {
    if (((signed) (frame.y + frame.y_border)) < 0)
      configure(screen->getWidth() - frame.width, screen->getBorderWidth(),
                frame.width, frame.height);
    else if (frame.y > (signed) screen->getHeight())
      configure(screen->getWidth() - frame.width,
	        screen->getHeight() - frame.height, frame.width, frame.height);
    else
      configure(screen->getWidth() - frame.width,
                frame.y + screen->getBorderWidth(), frame.width, frame.height);
  }
  
  
  blackbox->grab();
  if (! validateClient()) return False;

  Bool ret = False;

  switch (focus_mode) {
  case F_NoInput:
  case F_GloballyActive:
    if (decorations.titlebar) drawTitleWin();
    break;
    
  case F_LocallyActive:
  case F_Passive:
    if (client.transient)
      ret = client.transient->setInputFocus();
    else {
      if (! focused)
	if (XSetInputFocus(display, client.window, RevertToPointerRoot,
			   CurrentTime)) {
          screen->getWorkspace(workspace_number)->
            setFocusWindow(window_number);
          setFocusFlag(True);

	  ret = True;
        }
      else
	ret = True;
    }
 
    break;
  }

  blackbox->ungrab();
  
  return ret;
}


void BlackboxWindow::iconify(void) {
  blackbox->grab();
  if (! validateClient()) return;

  if (windowmenu) windowmenu->hide();

  setState(IconicState);
  XUnmapWindow(display, frame.window);
  visible = False;
  iconic = True;
  setFocusFlag(False);
  
  if (transient && client.transient_for) {
    if (! client.transient_for->iconic)
      client.transient_for->iconify();
  } else
    icon = new BlackboxIcon(blackbox, this);
  
  if (client.transient)
    if (! client.transient->iconic)
      client.transient->iconify();
  
#ifdef    KDE
  unsigned long data = ((iconic) ? 1 : 0);

  XChangeProperty(display, client.window, blackbox->getKWMWinIconifiedAtom(),
                  blackbox->getKWMWinIconifiedAtom(), 32, PropModeReplace,
                  (unsigned char *) &data, 1);
  screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			   client.window);
#endif // KDE

  blackbox->ungrab();
}


void BlackboxWindow::deiconify(Bool raise) {
  blackbox->grab();
  if (! validateClient()) return;

  screen->reassociateWindow(this);
  
  setState(NormalState);
  XMapWindow(display, frame.window);
    
  XMapSubwindows(display, frame.window);
  visible = True;
  iconic = False;
  
  if (client.transient) client.transient->deiconify();
  
  if (icon) {
    delete icon;
    removeIcon();
  }

#ifdef    KDE
  unsigned long data = ((iconic) ? 1 : 0);

  XChangeProperty(display, client.window, blackbox->getKWMWinIconifiedAtom(),
                  blackbox->getKWMWinIconifiedAtom(), 32, PropModeReplace,
                  (unsigned char *) &data, 1);
  screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			   client.window);
#endif // KDE

  if (raise)
    screen->getWorkspace(workspace_number)->raiseWindow(this);
  
  blackbox->ungrab();
}


void BlackboxWindow::close(void) {
  blackbox->grab();
  if (! validateClient()) return;

  XEvent ce;
  ce.xclient.type = ClientMessage;
  ce.xclient.message_type = blackbox->getWMProtocolsAtom();
  ce.xclient.display = display;
  ce.xclient.window = client.window;
  ce.xclient.format = 32;
  ce.xclient.data.l[0] = blackbox->getWMDeleteAtom();
  ce.xclient.data.l[1] = CurrentTime;
  ce.xclient.data.l[2] = 0l;
  ce.xclient.data.l[3] = 0l;
  ce.xclient.data.l[4] = 0l;
  XSendEvent(display, client.window, False, NoEventMask, &ce);

  blackbox->ungrab();
}


void BlackboxWindow::withdraw(void) {
  blackbox->grab();
  if (! validateClient()) return;

  setFocusFlag(False);
  visible = False;
  
  setState(WithdrawnState);
  XUnmapWindow(display, frame.window);

  if (windowmenu) windowmenu->hide();
  
#ifdef    KDE
  screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			   client.window);
#endif // KDE

  blackbox->ungrab();
}


void BlackboxWindow::maximize(unsigned int button) {
  if (! maximized) {
    int dx, dy;
    unsigned int dw, dh;

    frame.x_maximize = frame.x;
    frame.y_maximize = frame.y;
    frame.w_maximize = frame.width;
    frame.h_maximize = frame.height;

#ifdef    KDE
    int x1 = 0, x2 = screen->getWidth(), y1 = 0, y2 = screen->getHeight();

    screen->getCurrentWorkspace()->getKWMWindowRegion(&x1, &y1, &x2, &y2);

    dw = x2 - ((frame.handle_w + screen->getBorderWidth()) *
               decorations.handle) -
      client.base_width - ((frame.bevel_w * 2) * decorations.border);
    dh = y2 - screen->getToolbar()->getHeight() - frame.y_border -
      client.base_height - ((frame.bevel_w * 2) * decorations.border);

    unsigned long data = 1;
    XChangeProperty(display, client.window, blackbox->getKWMWinMaximizedAtom(),
                    blackbox->getKWMWinMaximizedAtom(), 32, PropModeReplace,
                    (unsigned char *) &data, 1);
    screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
                             client.window);
#else //  KDE
    dw = screen->getWidth() - ((frame.handle_w + screen->getBorderWidth())  *
                              decorations.handle) -
      client.base_width - ((frame.bevel_w * 2) * decorations.border);
    dh = screen->getHeight() - screen->getToolbar()->getHeight() -
      frame.y_border - client.base_height -
      ((frame.bevel_w * 2) * decorations.border);
#endif // KDE

    if (dw < client.min_width) dw = client.min_width;
    if (dh < client.min_height) dh = client.min_height;
    if (dw > client.max_width) dw = client.max_width;
    if (dh > client.max_height) dh = client.max_height;
    
    dw /= client.width_inc;
    dh /= client.height_inc;
    
    dw = (dw * client.width_inc) + client.base_width;
    dh = (dh * client.height_inc) + client.base_height;

    dw += ((frame.handle_w + screen->getBorderWidth()) * decorations.handle) +
      ((frame.bevel_w * 2) * decorations.border);
    dh += frame.y_border + ((frame.bevel_w * 2) * decorations.border);
    
#ifdef    KDE
    dx = x1 + ((x2 - dw) / 2);
    dy = y1 + ((y2 - screen->getToolbar()->getHeight() - dh) / 2);
#else //  KDE
    dx = ((screen->getWidth() - dw) / 2);
    dy = (((screen->getHeight() - screen->getToolbar()->getHeight()) - dh) / 2);
#endif // KDE

    if (button == 2) {
      dw = frame.width;
      dx = frame.x;
    } else if (button == 3) {
      dh = frame.height;
      dy = frame.y;
    }

    maximized = True;
    shaded = False;
    configure(dx, dy, dw, dh);
    screen->getWorkspace(workspace_number)->raiseWindow(this);
  } else {
    maximized = False;

    configure(frame.x_maximize, frame.y_maximize, frame.w_maximize,
	      frame.h_maximize);
    drawAllButtons();

#ifdef    KDE
    unsigned long data = 0;
    XChangeProperty(display, client.window, blackbox->getKWMWinMaximizedAtom(),
                    blackbox->getKWMWinMaximizedAtom(), 32, PropModeReplace,
                    (unsigned char *) &data, 1);
    screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
                             client.window);
#endif // KDE
    
  }
}


void BlackboxWindow::shade(void) {
  if (decorations.titlebar)
    if (shaded) {
      XResizeWindow(display, frame.window, frame.width, frame.height);
      shaded = False;

      setState(NormalState);
    } else {
      XResizeWindow(display, frame.window, frame.width, frame.title_h);
      shaded = True;
      
      setState(IconicState);
    }
}


void BlackboxWindow::stick(void) {
  if (stuck) {
    stuck = False;
    
    if (workspace_number != screen->getCurrentWorkspace()->getWorkspaceID()) {
      screen->getWorkspace(workspace_number)->removeWindow(this);
      screen->getCurrentWorkspace()->addWindow(this);
    }
  } else
    stuck = True;
  
#ifdef    KDE
  {
    unsigned long data = stuck;
    
    XChangeProperty(display, client.window, blackbox->getKWMWinStickyAtom(),
		    blackbox->getKWMWinStickyAtom(), 32, PropModeReplace,
		    (unsigned char *) &data, 1);
    screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			     client.window);
  }
#endif // KDE
  
}


int BlackboxWindow::setWindowNumber(int n) {
  window_number = n;
  return window_number;
}


int BlackboxWindow::setWorkspace(int n) {
  workspace_number = n;

#ifdef    KDE
  unsigned long data = (unsigned long) workspace_number + 1;
  XChangeProperty(display, client.window, blackbox->getKWMWinDesktopAtom(),
                  blackbox->getKWMWinDesktopAtom(), 32, PropModeReplace,
                  (unsigned char *) &data, 1);
  screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
                           client.window);
#endif // KDE
  
  return workspace_number;
}


void BlackboxWindow::setFocusFlag(Bool focus) {
  focused = focus;

  if (decorations.titlebar) {
    XSetWindowBackgroundPixmap(display, frame.title,
			       (focused) ? frame.ftitle : frame.utitle);
    XClearWindow(display, frame.title);
  }

  if (decorations.handle) {
    XSetWindowBackgroundPixmap(display, frame.handle,
			       (focused) ? frame.fhandle : frame.uhandle);
    XSetWindowBackgroundPixmap(display, frame.resize_handle,
			       (focused) ? frame.rfhandle : frame.ruhandle);
    XClearWindow(display, frame.handle);
    XClearWindow(display, frame.resize_handle);
  }
  
  if (decorations.titlebar) drawTitleWin();
  drawAllButtons();
}


void BlackboxWindow::installColormap(Bool install) {
  blackbox->grab();
  if (! validateClient()) return;
  
  int i = 0, ncmap = 0;
  Colormap *cmaps = XListInstalledColormaps(display, client.window, &ncmap);  
  XWindowAttributes wattrib;
  if (cmaps) {
    if (XGetWindowAttributes(display, client.window, &wattrib)) {
      if (install) {
	// install the window's colormap
	for (i = 0; i < ncmap; i++)
	  if (*(cmaps + i) == wattrib.colormap)
	    // this window is using an installed color map... do not install
	    install = False;
	
	// otherwise, install the window's colormap
	if (install)
	  XInstallColormap(display, wattrib.colormap);
      } else {
	// uninstall the window's colormap
	for (i = 0; i < ncmap; i++)
	  if (*(cmaps + i) == wattrib.colormap)
	    // we found the colormap to uninstall
	    XUninstallColormap(display, wattrib.colormap);
      }
    }
    
    XFree(cmaps);
  }
  
  blackbox->ungrab();
}


void BlackboxWindow::setState(unsigned long new_state) {
  unsigned long state[3];
  state[0] = (unsigned long) new_state;
  state[1] = (unsigned long) None;
  state[2] = (unsigned long) workspace_number;
  XChangeProperty(display, client.window, blackbox->getWMStateAtom(),
		  blackbox->getWMStateAtom(), 32, PropModeReplace,
		  (unsigned char *) state, 3);
}


Bool BlackboxWindow::getState(unsigned long *state_return,
			      unsigned long *workspace_return) {
  blackbox->grab();
  if (! validateClient()) return False;
  
  *state_return = 0;
  if (workspace_return)
    *workspace_return = (unsigned long) workspace_number;
  
  Atom atom_return;
  Bool ret = False;
  int foo;
  unsigned long *state, ulfoo, nitems;

  if ((XGetWindowProperty(display, client.window, blackbox->getWMStateAtom(),
			  0l, 3l, False, blackbox->getWMStateAtom(),
			  &atom_return, &foo, &nitems, &ulfoo,
			  (unsigned char **) &state) != Success) ||
      (! state) || (! state_return)) {
    blackbox->ungrab();
    return False;
  }
  
  if (nitems >= 1) {
    *state_return = (unsigned long) state[0];

    if (nitems > 2 && workspace_return)
      *workspace_return = (unsigned long) state[2];
    
    ret = True;
  }
  
  XFree((void *) state);
  blackbox->ungrab();
  
  return ret;
}


void BlackboxWindow::setGravityOffsets(void) {
  // translate x coordinate
  switch (client.win_gravity) {
    // handle Westward gravity
  case NorthWestGravity:
  case WestGravity:
  case SouthWestGravity:
    frame.x = client.x;
      break;
    
    // handle Eastward gravity
  case NorthEastGravity:
  case EastGravity:
  case SouthEastGravity:
    frame.x = (client.x + client.width) - frame.width;
    break;
    
    // no x translation desired - default
  case StaticGravity:
  case ForgetGravity:
  case CenterGravity:
  default:
    frame.x = client.x - (frame.bevel_w * decorations.border) -
      screen->getBorderWidth();
  }

  // translate y coordinate
  switch (client.win_gravity) {
    // handle Northbound gravity
  case NorthWestGravity:
  case NorthGravity:
  case NorthEastGravity:
    frame.y = client.y;
    break;

    // handle Southbound gravity
  case SouthWestGravity:
  case SouthGravity:
  case SouthEastGravity:
    frame.y = (client.y + client.height) - frame.height;
    break;

    // no y translation desired - default
  case StaticGravity:
  case ForgetGravity:
  case CenterGravity:
  default:
    frame.y = client.y - frame.y_border -
      (frame.bevel_w * decorations.border) - screen->getBorderWidth();
    break;
  }
}


void BlackboxWindow::restoreGravity(void) {
  // restore x coordinate
  switch (client.win_gravity) {
    // handle Westward gravity
  case NorthWestGravity:
  case WestGravity:
  case SouthWestGravity:
    client.x = frame.x;
    break;

    // handle Eastward gravity
  case NorthEastGravity:
  case EastGravity:
  case SouthEastGravity:
    client.x = (frame.x + frame.width) - client.width;
    break;
  }
  
  // restore y coordinate
  switch (client.win_gravity) {
    // handle Northbound gravity
  case NorthWestGravity:
  case NorthGravity:
  case NorthEastGravity:
    client.y = frame.y;
    break;
   
    // handle Southbound gravity
  case SouthWestGravity:
  case SouthGravity:
  case SouthEastGravity:
    client.y = (frame.y + frame.height) - client.height;
    break;
  }
}


void BlackboxWindow::drawTitleWin(void) {  
  switch (screen->getJustification()) {
  case BScreen::LeftJustify: {
    int dx = frame.button_w + 6, dlen = -1;
    
    if (frame.title_w > ((frame.button_w + 4) * 6)) {
      if (client.title_text_w + 4 +
	  (((frame.button_w + 4) * 3) + 2) > frame.title_w) {
	dx = 4;
	dlen = 0;
      }
    } else if (client.title_text_w + 8 > frame.title_w) {
      dx = 4;
      dlen = 0;
    }
    
    if (dlen != -1) {
      unsigned int l;
      for (dlen = client.title_len; dlen >= 0; dlen--) {
	l = XTextWidth(screen->getTitleFont(), client.title, dlen) + 8;
	if (l < frame.title_w)
	  break;
      }
    } else
      dlen = client.title_len;
    
    XDrawString(display, frame.title,
		((focused) ? screen->getWindowFocusGC() :
		 screen->getWindowUnfocusGC()), dx,
		screen->getTitleFont()->ascent + frame.bevel_w, client.title,
		dlen);
    break; }
  
  case BScreen::RightJustify: {
    int dx = frame.title_w - (client.title_text_w +
			      ((frame.button_w + 4) * 2) + 4),
      dlen = -1;
    
    if (frame.title_w > ((frame.button_w + 4) * 6)) {
      if (client.title_text_w + 4 +
	  (((frame.button_w + 4) * 3) + 2) > frame.title_w) {
	dx = 4;
	dlen = 0;
      }
    } else if (client.title_text_w + 8 > frame.title_w) {
      dx = 4;
      dlen = 0;
    }
    
    if (dlen != -1) {
      unsigned int l;
      for (dlen = client.title_len; dlen >= 0; dlen--) {
	l = XTextWidth(screen->getTitleFont(), client.title, dlen) + 8;
	if (l < frame.title_w)
	  break;
      }
    } else
      dlen = client.title_len;
    
    XDrawString(display, frame.title,
		((focused) ? screen->getWindowFocusGC() :
		 screen->getWindowUnfocusGC()),dx,
		screen->getTitleFont()->ascent + frame.bevel_w, client.title,
		dlen);
    break;  }
  
  case BScreen::CenterJustify: {
    int dx = (frame.title_w  - (client.title_text_w + frame.button_w + 6)) / 2,
      dlen = -1;
    
    if (frame.title_w > ((frame.button_w + 4) * 6)) {
      if (client.title_text_w + 4 +
	  (((frame.button_w + 4) * 3) + 2) > frame.title_w) {
	dx = 4;
	dlen = 0;
      }
    } else if (client.title_text_w + 8 > frame.title_w) {
      dx = 4;
      dlen = 0;
    }
    
    if (dlen != -1) {
      unsigned int l;
      for (dlen = client.title_len; dlen >= 0; dlen--) {
	l = XTextWidth(screen->getTitleFont(), client.title, dlen) + 8;
	if (l < frame.title_w)
	  break;
      }
    } else
      dlen = client.title_len;
    
    XDrawString(display, frame.title,
		((focused) ? screen->getWindowFocusGC() :
		 screen->getWindowUnfocusGC()),dx,
		screen->getTitleFont()->ascent + frame.bevel_w, client.title,
		dlen);
    break; }
  }
}


void BlackboxWindow::drawAllButtons(void) {
  if (frame.iconify_button) drawIconifyButton(False);
  if (frame.maximize_button) drawMaximizeButton(maximized);
  if (frame.close_button) drawCloseButton(False);
}


void BlackboxWindow::drawIconifyButton(Bool pressed) {
  if (! pressed) {
    XSetWindowBackgroundPixmap(display, frame.iconify_button,
			       ((focused) ? frame.fbutton : frame.ubutton));
    XClearWindow(display, frame.iconify_button);
  } else if (frame.pbutton) {
    XSetWindowBackgroundPixmap(display, frame.iconify_button, frame.pbutton);
    XClearWindow(display, frame.iconify_button);
  }

  XDrawRectangle(display, frame.iconify_button,
		 ((focused) ? screen->getWindowFocusGC() :
		  screen->getWindowUnfocusGC()),
		 2, frame.button_h - 5, frame.button_w - 5, 2);
}


void BlackboxWindow::drawMaximizeButton(Bool pressed) {
  if (! pressed) {
    XSetWindowBackgroundPixmap(display, frame.maximize_button,
			       ((focused) ? frame.fbutton : frame.ubutton));
    XClearWindow(display, frame.maximize_button);
  } else if (frame.pbutton) {
    XSetWindowBackgroundPixmap(display, frame.maximize_button, frame.pbutton);
    XClearWindow(display, frame.maximize_button);
  }
  
  XDrawRectangle(display, frame.maximize_button,
		 ((focused) ? screen->getWindowFocusGC() :
		  screen->getWindowUnfocusGC()),
		 2, 2, frame.button_w - 5, frame.button_h - 5);
  XDrawLine(display, frame.maximize_button,
	    ((focused) ? screen->getWindowFocusGC() :
	     screen->getWindowUnfocusGC()),
	    2, 3, frame.button_w - 3, 3);
}


void BlackboxWindow::drawCloseButton(Bool pressed) {
  if (! pressed) {
    XSetWindowBackgroundPixmap(display, frame.close_button,
			       ((focused) ? frame.fbutton : frame.ubutton));
    XClearWindow(display, frame.close_button);
  } else if (frame.pbutton) {
    XSetWindowBackgroundPixmap(display, frame.close_button, frame.pbutton);
    XClearWindow(display, frame.close_button);
  }

  XDrawLine(display, frame.close_button,
	    ((focused) ? screen->getWindowFocusGC() :
	     screen->getWindowUnfocusGC()), 2, 2,
            frame.button_w - 3, frame.button_h - 3);
  XDrawLine(display, frame.close_button,
	    ((focused) ? screen->getWindowFocusGC() :
	     screen->getWindowUnfocusGC()), 2, frame.button_h - 3,
            frame.button_w - 3, 2);
}


void BlackboxWindow::mapRequestEvent(XMapRequestEvent *re) {
  if (re->window == client.window) {
    if ((! iconic) && (client.wm_hint_flags & StateHint)) {
      blackbox->grab();
      if (! validateClient()) return;

      unsigned long state, workspace_new = workspace_number;
      if (! (getState(&state, &workspace_new) &&
	     blackbox->isStartup() &&
	     (state == NormalState || state == IconicState)))
	state = client.initial_state;
      
      if (((int) workspace_new != screen->getCurrentWorkspaceID()) &&
	  ((int) workspace_new < screen->getCount()) &&
	  ( ! (stuck && workspace_new == 0))) {
	screen->getWorkspace(workspace_number)->removeWindow(this);
	screen->getWorkspace(workspace_new)->addWindow(this);
	
	state = WithdrawnState;
      }
      
      switch (state) {
      case IconicState:
        iconify();
	break;
	
      case WithdrawnState:
	withdraw();

	break;
	
      case NormalState:
      case InactiveState:
      case ZoomState:
      default:
	positionButtons();
	
	setState(NormalState);

	XMapWindow(display, client.window);
	XMapSubwindows(display, frame.window);
	XMapWindow(display, frame.window);	
	setFocusFlag(False);
	
	visible = True;
	iconic = False;
	
	break;
      }
      
      blackbox->ungrab();
    } else      
      deiconify();
  }
}


void BlackboxWindow::mapNotifyEvent(XMapEvent *ne) {
  if ((ne->window == client.window) && (! ne->override_redirect) &&
      (! iconic) && (visible)) {
    blackbox->grab();
    if (! validateClient()) return;
    
    positionButtons();
    
    setState(NormalState);
    
    XMapSubwindows(display, frame.window);
    XMapWindow(display, frame.window);

    drawAllButtons();
    
    if (! transient)
      setFocusFlag(False);
    else
      setInputFocus();
    
    visible = True;
    iconic = False;

    blackbox->ungrab();
  }
}


void BlackboxWindow::unmapNotifyEvent(XUnmapEvent *ue) {
  if (ue->window == client.window) {
    if (! iconic && ! visible) return;

    blackbox->grab();
    if (! validateClient()) return;

    setState(WithdrawnState);
    
    visible = False;
    iconic = False;
    XUnmapWindow(display, frame.window);
    
    XEvent dummy;
    if (! XCheckTypedWindowEvent(display, client.window, ReparentNotify,
				 &dummy)) {
      restoreGravity();
      XReparentWindow(display, client.window, screen->getRootWindow(),
		      client.x, client.y);
    }
    
    XChangeSaveSet(display, client.window, SetModeDelete);
    XSelectInput(display, client.window, NoEventMask);
    
    XSync(display, False);
    blackbox->ungrab();
    
    delete this;
  }
}


void BlackboxWindow::destroyNotifyEvent(XDestroyWindowEvent *de) {
  if (de->window == client.window) {
    XUnmapWindow(display, frame.window);

    delete this;
  }
}


void BlackboxWindow::propertyNotifyEvent(Atom atom) {
  blackbox->grab();
  if (! validateClient()) return;

  switch(atom) {
  case XA_WM_CLASS:
  case XA_WM_CLIENT_MACHINE:
  case XA_WM_COMMAND:   
  case XA_WM_TRANSIENT_FOR:
    break;
      
  case XA_WM_HINTS:
    getWMHints();
    break;
    
  case XA_WM_ICON_NAME:
    if (icon) icon->rereadLabel();

#ifdef    KDE
    screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			     client.window);
#endif // KDE

    break;
      
  case XA_WM_NAME:
    if (client.title)
      if (strcmp(client.title, "Unnamed")) {
	XFree(client.title);
	client.title = 0;
      }
    
    if (! XFetchName(display, client.window, &client.title))
      client.title = "Unnamed";
    client.title_len = strlen(client.title);
    client.title_text_w = XTextWidth(screen->getTitleFont(), client.title,
				     client.title_len);

    if (decorations.titlebar) {
      XClearWindow(display, frame.title);
      drawTitleWin();
    }
    screen->getWorkspace(workspace_number)->update();
    
#ifdef    KDE
    screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			     client.window);
#endif // KDE
    
    break;
      
  case XA_WM_NORMAL_HINTS:
    getWMNormalHints();
    break;
    
  default:
    if (atom == blackbox->getWMProtocolsAtom())
      getWMProtocols();

#ifdef    KDE
    else {
      Atom ajunk;
      
      int ijunk;
      unsigned long uljunk, *data = (unsigned long *) 0;

      if (XGetWindowProperty(display, client.window, atom, 0l, 1l, False, atom,
                             &ajunk, &ijunk, &uljunk, &uljunk,
                             (unsigned char **) &data) == Success && data) {
        if (atom == blackbox->getKWMWinDesktopAtom()) {
          if ((((int) *data) - 1) != workspace_number) {
            if (! stuck) {
              if (workspace_number ==
                  screen->getCurrentWorkspace()->getWorkspaceID())
                withdraw();
	      
              screen->getWorkspace(workspace_number)->removeWindow(this);
              screen->getWorkspace(((int) *data) - 1)->addWindow(this);
	      
              if (workspace_number ==
                  screen->getCurrentWorkspace()->getWorkspaceID())
                deiconify();
            } else {
              workspace_number = ((int) *data) - 1;
            }
          }
        } else if (atom == blackbox->getKWMWinMaximizedAtom()) {
          if (((int) *data) - maximized)
            maximize(1);
        } else if (atom == blackbox->getKWMWinStickyAtom()) {
	  if (((! stuck) && ((int) *data)) ||
	      ((! ((int) *data)) && stuck))
	    stick();
        } else if (atom == blackbox->getKWMWinIconifiedAtom()) {
          if (((int) *data) && ! iconic)
            iconify();
          else if (! ((int) *data) && iconic)
            deiconify();
        }

	screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
				 client.window);
	
        XFree((char *) data);
      }
    }
#endif // KDE
    
  }
  
  blackbox->ungrab();
}


void BlackboxWindow::exposeEvent(XExposeEvent *ee) {
  if (frame.title == ee->window) {
    if (decorations.titlebar)
      drawTitleWin();
  } else if (frame.close_button == ee->window) {
    drawCloseButton(False);
  } else if (frame.maximize_button == ee->window) {
    drawMaximizeButton(maximized);
  } else if (frame.iconify_button == ee->window) {
    drawIconifyButton(False);
  }
}


void BlackboxWindow::configureRequestEvent(XConfigureRequestEvent *cr) {  
  if (cr->window == client.window) {
    int cx = frame.x, cy = frame.y;
    unsigned int cw = frame.width, ch = frame.height;

    if (cr->value_mask & CWBorderWidth)
      client.old_bw = cr->border_width;

    if (cr->value_mask & CWX)
      cx = cr->x - (frame.bevel_w * decorations.border) -
        screen->getBorderWidth();

    if (cr->value_mask & CWY)
      cy = cr->y - frame.y_border - (frame.bevel_w * decorations.border) -
        screen->getBorderWidth();

    if (cr->value_mask & CWWidth)
      cw = cr->width + ((frame.bevel_w * 2) * decorations.border) +
	((frame.handle_w + screen->getBorderWidth()) * decorations.handle);

    if (cr->value_mask & CWHeight)
      ch = cr->height + frame.y_border +
	((frame.bevel_w * 2) * decorations.border);

    configure(cx, cy, cw, ch);
    
#ifdef    KDE
    screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			     client.window);
#endif // KDE
    
    if (cr->value_mask & CWStackMode) {
      switch (cr->detail) {
      case Above:
      case TopIf:
	if (iconic) deiconify();
	screen->getWorkspace(workspace_number)->raiseWindow(this);
	break;

      case Below:
      case BottomIf:
	if (iconic) deiconify();
	screen->getWorkspace(workspace_number)->lowerWindow(this);
	break;

      default:
	break;
      }
    }
  }
}


void BlackboxWindow::buttonPressEvent(XButtonEvent *be) {
  if (frame.maximize_button == be->window) {
    drawMaximizeButton(True);
  } else if (be->button == 1) {
    if ((! focused) && (! screen->isSloppyFocus()))
      setInputFocus();

    if (frame.title == be->window || frame.handle == be->window ||
	frame.resize_handle == be->window || frame.border == be->window ||
	frame.window == be->window) {
      if (frame.title == be->window) {
	if ((be->time - lastButtonPressTime) <=
	    blackbox->getDoubleClickInterval()) {
	  lastButtonPressTime = 0;
	  shade();
	} else
	  lastButtonPressTime = be->time;
      }
      
      frame.x_grab = be->x;
      frame.y_grab = be->y;

      if (windowmenu && windowmenu->isVisible()) windowmenu->hide();
      
      screen->getWorkspace(workspace_number)->raiseWindow(this);
    } else if (frame.iconify_button == be->window) {
      drawIconifyButton(True);
    } else if (frame.close_button == be->window) {
      drawCloseButton(True);
    }
  } else if (be->button == 2) {
    if (frame.title == be->window || frame.handle == be->window ||
        frame.resize_handle == be->window || frame.border == be->window) {
      screen->getWorkspace(workspace_number)->lowerWindow(this);
    }
  } else if (windowmenu && be->button == 3 && (frame.title == be->window ||
			  		       frame.border == be->window ||
				 	       frame.handle == be->window)) {
    int mx = 0, my = 0;
    
    if (frame.title == be->window) {
      mx = be->x_root - (windowmenu->getWidth() / 2);
      my = frame.y + frame.title_h;
    } else if (frame.border == be->window) {
      mx = be->x_root - (windowmenu->getWidth() / 2);

      if (be->y <= (signed) frame.bevel_w)
	my = frame.y + frame.y_border;
      else
	my = be->y_root - (windowmenu->getHeight() / 2);
    } else if (frame.handle == be->window) {
      mx = frame.x + frame.width - windowmenu->getWidth();
      my = be->y_root - (windowmenu->getHeight() / 2);
    }
    
    if (mx > (signed) (frame.x + frame.width - (frame.handle_w +
						windowmenu->getWidth())))
      mx = frame.x + frame.width - (frame.handle_w + windowmenu->getWidth());
    if (mx < frame.x)
      mx = frame.x;
    
    if (my > (signed) (frame.y + frame.height - windowmenu->getHeight()))
      my = frame.y + frame.height - windowmenu->getHeight();
    if (my < (signed) (frame.y + ((decorations.titlebar) ? frame.title_h :
			     	  frame.y_border)))
      my = frame.y +
	((decorations.titlebar) ? frame.title_h : frame.y_border);
    
    if (windowmenu)
      if (! windowmenu->isVisible()) {
	windowmenu->move(mx, my);
	windowmenu->show();
        XRaiseWindow(display, windowmenu->getWindowID());
        XRaiseWindow(display, windowmenu->getSendToMenu()->getWindowID());
      } else
	windowmenu->hide();
  }
}


void BlackboxWindow::buttonReleaseEvent(XButtonEvent *re) {
  if (re->window == frame.maximize_button) {
    if ((re->x >= 0) && ((unsigned) re->x <= frame.button_w) &&
        (re->y >= 0) && ((unsigned) re->y <= frame.button_h)) {
      maximize(re->button);
    } else
      drawMaximizeButton(maximized);
  } else if (re->button == 1) {
    if (re->window == frame.title || re->window == frame.handle ||
	re->window == frame.border || re->window == frame.window) {
      if (moving) {
        moving = False;

        if (! blackbox->doOpaqueMove()) {
	  if (shaded)
	    XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
			   frame.x_move, frame.y_move, frame.width,
			   frame.title_h);
	  else
	    XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
			   frame.x_move, frame.y_move, frame.width,
			   frame.height);

	  configure(frame.x_move, frame.y_move, frame.width, frame.height);
	  blackbox->ungrab();
	} else
	  configure(frame.x, frame.y, frame.width, frame.height);

        screen->hideGeometry();
	
#ifdef    KDE
	screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
				 client.window);
#endif // KDE
	XUngrabPointer(display, CurrentTime);
      } else if ((re->state & ControlMask))
	shade();
    } else if (resizing) {
      int dx, dy;
      
      XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
                     frame.x, frame.y, frame.x_resize, frame.y_resize);

      screen->hideGeometry();

      // calculate the size of the client window and conform it to the
      // size specified by the size hints of the client window...
      dx = frame.x_resize - frame.handle_w - client.base_width -
	((frame.bevel_w * 2) * decorations.border) - screen->getBorderWidth();
      dy = frame.y_resize - frame.y_border - client.base_height -
	((frame.bevel_w * 2) * decorations.border);
      
      if (dx < (signed) client.min_width) dx = client.min_width;
      if (dy < (signed) client.min_height) dy = client.min_height;
      if ((unsigned) dx > client.max_width) dx = client.max_width;
      if ((unsigned) dy > client.max_height) dy = client.max_height;
      
      dx /= client.width_inc;
      dy /= client.height_inc;
      
      dx = (dx * client.width_inc) + client.base_width;
      dy = (dy * client.height_inc) + client.base_height;
      
      frame.x_resize = dx + frame.handle_w +
	((frame.bevel_w * 2) * decorations.border) + screen->getBorderWidth();
      frame.y_resize = dy + frame.y_border +
	((frame.bevel_w * 2) * decorations.border);
      
      resizing = False;
      XUngrabPointer(display, CurrentTime);
      
      configure(frame.x, frame.y, frame.x_resize, frame.y_resize);

#ifdef    KDE
      screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			       client.window);
#endif // KDE
      
      blackbox->ungrab();
    } else if (re->window == frame.iconify_button) {
      if ((re->x >= 0) && ((unsigned) re->x <= frame.button_w) &&
	  (re->y >= 0) && ((unsigned) re->y <= frame.button_h)) {
	iconify();
      }	else
	drawIconifyButton(False);
    } else if (re->window == frame.close_button) {
      if ((re->x >= 0) && ((unsigned) re->x <= frame.button_w) &&
	  (re->y >= 0) && ((unsigned) re->y <= frame.button_h)) {
	close();
      } else
	drawCloseButton(False);
    }
  }
}


void BlackboxWindow::motionNotifyEvent(XMotionEvent *me) {
  if (frame.title == me->window || frame.handle == me->window ||
      frame.border == me->window || frame.window == me->window) {
    if ((me->state & Button1Mask) && functions.move) {
      if (! moving) {
	if (windowmenu && windowmenu->isVisible())
	    windowmenu->hide();
	
	if (XGrabPointer(display, me->window, False, ButtonMotionMask |
			 ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
			 None, blackbox->getMoveCursor(), CurrentTime)
	    == GrabSuccess) {
	  moving = True;

	  if (! blackbox->doOpaqueMove()) {
            blackbox->grab();
	    
	    frame.x_move = frame.x;
	    frame.y_move = frame.y;

	    screen->showPosition(frame.x, frame.y);

            if (shaded)
	      XDrawRectangle(display, screen->getRootWindow(),
			     screen->getOpGC(), frame.x_move,
			     frame.y_move, frame.width, frame.title_h);
	    else
	      XDrawRectangle(display, screen->getRootWindow(),
			     screen->getOpGC(), frame.x_move,
			     frame.y_move, frame.width, frame.height);
          }
	} else
	  moving = False;
      } else if (moving) {
	int dx, dy;
	if (frame.handle == me->window) {
	  dx = me->x_root - frame.x_grab + frame.handle_w - frame.width;
	  dy = me->y_root - frame.y_grab - frame.y_border;
	} else if (frame.border == me->window) {
	  dx = me->x_root - frame.x_grab;
	  dy = me->y_root - frame.y_grab - frame.y_border;
	} else {
	  dx = me->x_root - frame.x_grab;
	  dy = me->y_root - frame.y_grab;
	}

	if (! blackbox->doOpaqueMove()) {
	  if (shaded)
	    XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
			   frame.x_move, frame.y_move, frame.width,
			   frame.title_h);
	  else
	    XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
			   frame.x_move, frame.y_move, frame.width,
			   frame.height);
	  
	  frame.x_move = dx;
	  frame.y_move = dy;

	  if (shaded)
	    XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
			   frame.x_move, frame.y_move, frame.width,
			   frame.title_h);
	  else
	    XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
			   frame.x_move, frame.y_move, frame.width,
			   frame.height);
	} else
	  configure(dx, dy, frame.width, frame.height);

        screen->showPosition(dx, dy);
      }
    }
  } else if (me->window == frame.resize_handle) {
    if ((me->state & Button1Mask) && functions.resize) {
      if (! resizing) {
	if (XGrabPointer(display, frame.resize_handle, False,
			 ButtonMotionMask | ButtonReleaseMask, GrabModeAsync,
			 GrabModeAsync, None, None, CurrentTime)
	    == GrabSuccess) {
	  int dx, dy;
	  resizing = True;
	  
          blackbox->grab();

	  frame.x_resize = frame.width;
	  frame.y_resize = frame.height;
	  
	  // calculate the size of the client window and conform it to the
	  // size specified by the size hints of the client window...
	  dx = frame.x_resize - frame.handle_w - client.base_width -
	    ((frame.bevel_w * 2) * decorations.border) -
            screen->getBorderWidth();
	  dy = frame.y_resize - frame.y_border - client.base_height -
	    ((frame.bevel_w * 2) * decorations.border);
	  
	  if (dx < (signed) client.min_width) dx = client.min_width;
	  if (dy < (signed) client.min_height) dy = client.min_height;
	  if ((unsigned) dx > client.max_width) dx = client.max_width;
	  if ((unsigned) dy > client.max_height) dy = client.max_height;
	  
	  dx /= client.width_inc;
	  dy /= client.height_inc;
	  
	  int gx = dx, gy = dy;
	  
	  dx = (dx * client.width_inc) + client.base_width;
	  dy = (dy * client.height_inc) + client.base_height;
	  
	  frame.x_resize = dx + frame.handle_w +
	    ((frame.bevel_w * 2) * decorations.border) +
            screen->getBorderWidth();
	  frame.y_resize = dy + frame.y_border +
	    ((frame.bevel_w * 2) * decorations.border);
	  
          screen->showGeometry(gx, gy);

          XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
                         frame.x, frame.y, frame.x_resize, frame.y_resize);
	} else
	  resizing = False;
      } else if (resizing) {
	int dx, dy;

	XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
		       frame.x, frame.y, frame.x_resize, frame.y_resize);

	frame.x_resize = me->x_root - frame.x;
	if (frame.x_resize < 1) frame.x_resize = 1;
	frame.y_resize = me->y_root - frame.y;
	if (frame.y_resize < 1) frame.y_resize = 1;

	// calculate the size of the client window and conform it to the
	// size specified by the size hints of the client window...
	dx = frame.x_resize - frame.handle_w - client.base_width -
	  ((frame.bevel_w * 2) * decorations.border) -
          screen->getBorderWidth();
	dy = frame.y_resize - client.base_height - frame.y_border -
	  ((frame.bevel_w * 2) * decorations.border);

	if (dx < (signed) client.min_width) dx = client.min_width;
	if (dy < (signed) client.min_height) dy = client.min_height;
	if ((unsigned) dx > client.max_width) dx = client.max_width;
	if ((unsigned) dy > client.max_height) dy = client.max_height;
	
	dx /= client.width_inc;
	dy /= client.height_inc;

        int gx = dx, gy = dy;
	
	dx = (dx * client.width_inc) + client.base_width;
	dy = (dy * client.height_inc) + client.base_height;

	frame.x_resize = dx + frame.handle_w +
	  ((frame.bevel_w * 2) * decorations.border) +
          screen->getBorderWidth();
	frame.y_resize = dy + frame.y_border +
	  ((frame.bevel_w * 2) * decorations.border);
	
        XDrawRectangle(display, screen->getRootWindow(), screen->getOpGC(),
                       frame.x, frame.y, frame.x_resize, frame.y_resize);

        screen->showGeometry(gx, gy);
      }
    }
  }
}


#ifdef    SHAPE
void BlackboxWindow::shapeEvent(XShapeEvent *) {
  if (blackbox->hasShapeExtensions()) {
    if (frame.shaped) {
      blackbox->grab();
      if (! validateClient()) return;
      XShapeCombineShape(display, frame.window, ShapeBounding,
			 (frame.bevel_w * decorations.border),
			 frame.y_border + (frame.bevel_w * decorations.border),
			 client.window, ShapeBounding, ShapeSet);
      
      int num = 1;
      XRectangle xrect[2];
      xrect[0].x = xrect[0].y = 0;
      xrect[0].width = frame.width;
      xrect[0].height = frame.y_border; 
      
      if (decorations.handle) {
	xrect[1].x = frame.x_handle;
	xrect[1].y = frame.y_border;
	xrect[1].width = frame.handle_w;
	xrect[1].height = frame.handle_h + frame.rh_h +
          screen->getBorderWidth();
	num++;
      }
      
#ifdef    KDE
      screen->sendToKWMModules(blackbox->getKWMModuleWinChangeAtom(),
			       client.window);
#endif // KDE
      
      XShapeCombineRectangles(display, frame.window, ShapeBounding, 0, 0,
			      xrect, num, ShapeUnion, Unsorted);
      blackbox->ungrab();
    }
  }
}
#endif // SHAPE


Bool BlackboxWindow::validateClient(void) {
  XEvent e;
  if (XCheckTypedWindowEvent(display, client.window, DestroyNotify, &e) ||
      XCheckTypedWindowEvent(display, client.window, UnmapNotify, &e)) {
    XPutBackEvent(display, &e);
    blackbox->ungrab();

    return False;
  }

  return True;
}


void BlackboxWindow::restore(void) {
  blackbox->grab();

  XSelectInput(display, client.window, NoEventMask);

  restoreGravity();

  XReparentWindow(display, client.window, screen->getRootWindow(),
		  client.x, client.y);
  XSetWindowBorderWidth(display, client.window, client.old_bw);
  XMapWindow(display, client.window);
  
  XSync(display, False);
}

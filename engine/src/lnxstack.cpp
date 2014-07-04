/* Copyright (C) 2003-2013 Runtime Revolution Ltd.

This file is part of LiveCode.

LiveCode is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License v3 as published by the Free
Software Foundation.

LiveCode is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with LiveCode.  If not see <http://www.gnu.org/licenses/>.  */

//
// platform-specific MCStack class functions
//
#include "lnxprefix.h"

#include "globdefs.h"
#include "filedefs.h"
#include "objdefs.h"
#include "parsedef.h"

//#include "execpt.h"
#include "dispatch.h"
#include "stack.h"
#include "card.h"
#include "group.h"
#include "image.h"
#include "field.h"
#include "stacklst.h"
#include "cardlst.h"
#include "sellst.h"
#include "handler.h"
#include "mcerror.h"
#include "param.h"
#include "util.h"
#include "debug.h"
#include "mode.h"
#include "player.h"
#include "globals.h"
#include "region.h"
#include "redraw.h"

#include "lnxdc.h"
#include "graphicscontext.h"

#include "resolution.h"

#include "license.h"
#include "revbuild.h"

static uint2 calldepth;
static uint2 nwait;

////////////////////////////////////////////////////////////////////////////////

extern void surface_merge_with_alpha(void *p_pixels, uint4 p_pixel_stride, void *p_alpha, uint4 p_alpha_stride, uint4 p_width, uint4 p_height);

////////////////////////////////////////////////////////////////////////////////

static MCStackUpdateCallback s_update_callback = nil;
static void *s_update_context = nil;

////////////////////////////////////////////////////////////////////////////////

MCStack *MCStack::findstackd(Window w)
{
	if (w == DNULL)
		return NULL;
	
	if (w == window)
		return this;
	if (substacks != NULL)
	{
		MCStack *tptr = substacks;
		do
		{
			if (w == tptr->window)
				return tptr;
			tptr = (MCStack *)tptr->next();
		}
		while (tptr != substacks);
	}
	return NULL;
}


MCStack *MCStack::findchildstackd(Window w,uint2 &ccount,uint2 cindex)
{
	Window pwindow = getparentwindow();
	if (pwindow != DNULL && w == pwindow)
		if  (++ccount == cindex)
			return this;
	if (substacks != NULL)
	{
		MCStack *tptr = substacks;
		do
		{
			pwindow = tptr->getparentwindow();
			if (pwindow != DNULL && w == pwindow)
			{
				ccount++;
				if (ccount == cindex)
					return tptr;
			}
			tptr = (MCStack *)tptr->next();
		}
		while (tptr != substacks);
	}
	return NULL;
}

void MCStack::realize()
{
	if (MCnoui)
	{
		start_externals();
		return;
	}

	if (MCModeMakeLocalWindows())
	{
		MCScreenDC *screen = (MCScreenDC *)MCscreen;

		// IM-2013-10-08: [[ FullscreenMode ]] Don't change stack rect if fullscreen
		/* CODE DELETED */

		MCRectangle t_rect;
		// IM-2014-01-29: [[ HiDPI ]] Convert logical to screen coords
		t_rect = ((MCScreenDC*)MCscreen)->logicaltoscreenrect(view_getrect());

		if (t_rect.width == 0)
			t_rect.width = MCminsize << 4;
		if (t_rect.height == 0)
			t_rect.height = MCminsize << 3;
		
        GdkWindowAttr gdkwa;
        guint gdk_valid_wa;
        gdk_valid_wa = GDK_WA_X|GDK_WA_Y;
        gdkwa.x = t_rect.x;
        gdkwa.y = t_rect.y;
        gdkwa.width = t_rect.width;
        gdkwa.height = t_rect.height;
        gdkwa.wclass = GDK_INPUT_OUTPUT;
        gdkwa.window_type = GDK_WINDOW_TOPLEVEL;
        gdkwa.visual = gdk_visual_get_best();
        gdkwa.event_mask = GDK_ALL_EVENTS_MASK & ~GDK_POINTER_MOTION_HINT_MASK;
        
        window = gdk_window_new(screen->getroot(), &gdkwa, gdk_valid_wa);
        
        //fprintf(stderr, "Window %p - \"%s\"\n", window, MCNameGetCString(_name));
        
		// This is necessary to be able to receive drag-and-drop events
        gdk_window_register_dnd(window);
        
		if (screen -> get_backdrop() != DNULL)
            gdk_window_set_transient_for(window, screen->get_backdrop());

		loadwindowshape();
		if (m_window_shape != nil && m_window_shape -> is_sharp)
            gdk_window_shape_combine_mask(window, (GdkPixmap*)m_window_shape->handle, 0, 0);
        
        // At least one window has been created so startup is complete
        gdk_notify_startup_complete();
        
        // DEBUGGING
        //gdk_window_set_debug_updates(TRUE);
        //gdk_window_invalidate_rect(window, NULL, TRUE);
        //gdk_window_process_all_updates();

		gdk_display_sync(MCdpy);
	}

	start_externals();
}

void MCStack::setmodalhints()
{
	if (mode == WM_MODAL || mode == WM_SHEET)
	{
		if (mode == WM_SHEET)
            gdk_window_set_transient_for(window, (mode == WM_SHEET) ? parentwindow : NULL);
        gdk_window_set_modal_hint(window, TRUE);
	}
}

// IM-2013-10-08: [[ FullscreenMode ]] Separate out window sizing hints
void MCStack::setsizehints(void)
{
	if (!opened || MCnoui || window == DNULL)
		return;

	if (opened)
	{
		// IM-2013-08-12: [[ ResIndependence ]] Use device coordinates when setting WM hints
		GdkGeometry t_geo;
        gint t_flags = 0;
		if (flags & F_RESIZABLE)
		{
			// IM-2013-10-18: [[ FullscreenMode ]] Assume min/max sizes in view coords
			// for resizable stacks - transform to device coords
			MCRectangle t_minrect, t_maxrect;
			t_minrect = MCRectangleMake(0, 0, minwidth, minheight);
			t_maxrect = MCRectangleMake(0, 0, maxwidth, maxheight);
			
			// IM-2014-01-29: [[ HiDPI ]] Convert logical to screen coords
			t_minrect = ((MCScreenDC*)MCscreen)->logicaltoscreenrect(t_minrect);
			t_maxrect = ((MCScreenDC*)MCscreen)->logicaltoscreenrect(t_maxrect);
			
            t_geo.min_width = t_minrect.width;
            t_geo.max_width = t_maxrect.width;
            t_geo.min_height = t_minrect.height;
            t_geo.max_height = t_maxrect.height;
            t_flags |= GDK_HINT_MIN_SIZE|GDK_HINT_MAX_SIZE;
		}
		else
		{
			// IM-2014-01-29: [[ HiDPI ]] Convert logical to screen coords
			MCRectangle t_device_rect;
			t_device_rect = ((MCScreenDC*)MCscreen)->logicaltoscreenrect(view_getrect());
			
			t_geo.min_width = t_geo.max_width = t_device_rect.width;
            t_geo.min_height = t_geo.max_height = t_device_rect.height;
            t_flags |= GDK_HINT_MIN_SIZE|GDK_HINT_MAX_SIZE;
		}
		
        t_geo.win_gravity = GDK_GRAVITY_STATIC;
        t_flags |= GDK_HINT_WIN_GRAVITY;
        
        gdk_window_set_geometry_hints(window, &t_geo, GdkWindowHints(t_flags));
	}
	
	// Use the window manager to set to full screen.
	if (getextendedstate(ECS_FULLSCREEN))
	{
        gdk_window_fullscreen(window);
	}
}

void MCStack::sethints()
{
	if (!opened || MCnoui || window == DNULL)
		return;
		
    // Choose the appropriate type fint for the window
    GdkWindowTypeHint t_type_hint = GDK_WINDOW_TYPE_HINT_NORMAL;
    switch (mode)
    {
        case WM_CLOSED:
        case WM_TOP_LEVEL:
        case WM_TOP_LEVEL_LOCKED:
        case WM_MODELESS:
            break;
            
        case WM_PALETTE:
            t_type_hint = GDK_WINDOW_TYPE_HINT_UTILITY;
            break;
            
        case WM_MODAL:
        case WM_SHEET:
            t_type_hint = GDK_WINDOW_TYPE_HINT_DIALOG;
            break;
            
        case WM_PULLDOWN:
            t_type_hint = GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU;
            break;
            
        case WM_POPUP:
        case WM_OPTION:
        case WM_CASCADE:
            t_type_hint = GDK_WINDOW_TYPE_HINT_POPUP_MENU;
            break;
            
        case WM_COMBO:
            t_type_hint = GDK_WINDOW_TYPE_HINT_COMBO;
            break;
            
        case WM_ICONIC:
            break;
            
        case WM_DRAWER:
            t_type_hint = GDK_WINDOW_TYPE_HINT_DIALOG;
            break;
            
        case WM_TOOLTIP:
            t_type_hint = GDK_WINDOW_TYPE_HINT_TOOLTIP;
            break;
    }
    
    gdk_window_set_type_hint(window, t_type_hint);
    
    if (mode >= WM_PULLDOWN && mode <= WM_LICENSE)
    {
        gdk_window_set_override_redirect(window, TRUE);
    }
    else
    {
        gdk_window_set_override_redirect(window, FALSE);
    }
    
    // TODO: initial input focus and initial window state
	//whints.input = MCpointerfocus;
	//whints.initial_state = flags & F_START_UP_ICONIC ? IconicState:NormalState;

    gdk_window_set_group(window, ((MCScreenDC*)MCscreen)->GetNullWindow());
    
    // GDK does not provide an easy way to change the WM class properties after
    // window creation time. As a result, we have to do it manually.
    x11::XClassHint chints;
	chints.res_name = (char *)getname_cstring();

    // Build the class name
    MCAutoStringRef t_class_name;
    MCAutoStringRefAsCString t_class_name_cstr;
    bool t_community;
    t_community = MClicenseparameters.license_class == kMCLicenseClassCommunity;
    
    /* UNCHECKED */ MCStringCreateMutable(0, &t_class_name);
    /* UNCHECKED */ MCStringAppendFormat(*t_class_name, "%s%s_%s", MCapplicationstring, t_community ? "community" : "", MC_BUILD_ENGINE_SHORT_VERSION);
    /* UNCHECKED */ MCStringFindAndReplaceChar(*t_class_name, '.', '_', kMCStringOptionCompareExact);
    /* UNCHECKED */ MCStringFindAndReplaceChar(*t_class_name, '-', '_', kMCStringOptionCompareExact);
    /* UNCHECKED */ t_class_name_cstr.Lock(*t_class_name);
    
	chints.res_class = (char*)*t_class_name_cstr;
    x11::XSetClassHint(x11::gdk_x11_display_get_xdisplay(MCdpy), x11::gdk_x11_drawable_get_xid(window), &chints);

    // TODO: is this just another way of ensuring on-top-ness?
	//if (mode >= WM_PALETTE)
	//{
	//	uint4 data = 5;
	//	XChangeProperty(MCdpy, window, MClayeratom, XA_CARDINAL, 32,
	//	                PropModeReplace, (unsigned char *)&data, 1);
	//}
	
    // What decorations and modifications should the window have?
    gint t_decorations = 0;
    gint t_functions = 0;
	if (flags & F_RESIZABLE) // && mode != WM_PALETTE)
	{
        t_decorations = GDK_DECOR_RESIZEH | GDK_DECOR_MAXIMIZE | GDK_DECOR_TITLE;
        t_functions = GDK_FUNC_RESIZE | GDK_FUNC_MOVE | GDK_FUNC_MAXIMIZE | GDK_FUNC_CLOSE;
	}
	else
	{
		t_decorations = GDK_DECOR_TITLE | GDK_DECOR_BORDER;
        t_functions = GDK_FUNC_MOVE | GDK_FUNC_MINIMIZE | GDK_FUNC_CLOSE;
	}

    // TODO: input modality hints
    // (According to the GDK documentation, most WMs ignore the Motif hints anyway)
	switch (mode)
	{
        case WM_TOP_LEVEL:
        case WM_TOP_LEVEL_LOCKED:
        case WM_MODELESS:
        case WM_PALETTE:
        case WM_DRAWER:
            t_decorations |= GDK_DECOR_MENU;
            if (mode != WM_PALETTE && view_getrect().width > DECORATION_MINIMIZE_WIDTH)
            {
                t_decorations |= GDK_DECOR_MINIMIZE;
                t_functions |= GDK_FUNC_MINIMIZE;
            }

            //mwmhints.input_mode = MWM_INPUT_MODELESS;
            break;
        case WM_LICENSE:
            t_functions = 0;
            //mwmhints.input_mode = MWM_INPUT_SYSTEM_MODAL;
            break;
        default:
            //mwmhints.input_mode = MWM_INPUT_FULL_APPLICATION_MODAL;
            break;
	}
    
	if (flags & F_DECORATIONS)
	{
        // Set all of the decorations manually
        t_decorations = 0;
        t_functions = 0;
		
		if ( ( decorations & ( WD_TITLE | WD_MENU | WD_MINIMIZE | WD_MAXIMIZE | WD_CLOSE ) )  && flags & F_RESIZABLE ) //&& mode != WM_PALETTE)
		{
			t_decorations |= GDK_DECOR_RESIZEH;
            t_functions |= GDK_FUNC_RESIZE;
		}

		if (decorations & WD_TITLE)
		{
			t_decorations |= GDK_DECOR_TITLE | GDK_DECOR_BORDER;
		}

		if (decorations & WD_MENU)
		{
			t_decorations |= GDK_DECOR_MENU;
		}

		if (decorations & WD_MINIMIZE)
		{
			t_decorations |= GDK_DECOR_MINIMIZE;
            t_functions |= GDK_FUNC_MINIMIZE;
		}
        
		if (decorations & WD_MAXIMIZE)
		{
			t_decorations |= GDK_DECOR_MAXIMIZE;
            t_functions |= GDK_FUNC_MAXIMIZE;
		}
		
		//TS-2007-08-20 Added handler for WD_CLOSE
		if (decorations & WD_CLOSE)
		{
			t_functions |= GDK_FUNC_CLOSE;
		}
		
		if ( decorations != 0 ) 
			t_functions |= GDK_FUNC_MOVE;

		//TS 
		if ( decorations & WD_SHAPE )
		{
			t_decorations = 0;
		}
		
	}
    
    // TODO: test if this comment is still true
	// Gnome gets confused with these set
	//if (flags & F_DECORATIONS)
    gdk_window_set_decorations(window, GdkWMDecoration(t_decorations));
    gdk_window_set_functions(window, GdkWMFunction(t_functions));
	
	//TS 2007-11-08 : Adding in additional hint _NET_WM_STATE == _NET_WM_STATE_ABOVE if we have set WD_UTILITY (i.e. systemwindow == true)
	if (decorations & WD_UTILITY)
	{
		gdk_window_set_keep_above(window, TRUE);
	}
}

void MCStack::destroywindowshape()
{
	if (m_window_shape == nil)
		return;

	// Delete the data ptr (might be null).
	delete[] m_window_shape -> data;

	// If the mask is sharp, then 'handle' is a Pixmap used to set the window
	// shape. Otherwise it is nil.
	if (m_window_shape -> is_sharp)
	{
		GdkPixmap *t_pixmap;
		t_pixmap = (GdkPixmap*)m_window_shape -> handle;
		if (t_pixmap != nil)
			((MCScreenDC*)MCscreen) -> freepixmap(t_pixmap);
	}

	delete m_window_shape;
	m_window_shape = nil;
}

// IM-2014-01-29: [[ HiDPI ]] Placeholder method for Linux HiDPI support
MCRectangle MCStack::view_platform_getwindowrect(void) const
{
	return view_device_getwindowrect();
}

MCRectangle MCStack::view_device_getwindowrect(void) const
{
    x11::Window t_root, t_child, t_parent;
    x11::Window *t_children;
	int32_t t_win_x, t_win_y, t_x_offset, t_y_offset;
	uint32_t t_width, t_height, t_border_width, t_depth, t_child_count;

    x11::Window t_window = x11::gdk_x11_drawable_get_xid(window);
    
    x11::Display *t_display = x11::gdk_x11_display_get_xdisplay(MCdpy);

    // We query for the top-level parent using the X11 functions because the GDK
    // equivalents do not account for re-parenting window managers and will not
    // return the re-parented parent.
    x11::XQueryTree(t_display, t_window, &t_root, &t_parent, &t_children, &t_child_count);
    x11::XFree(t_children);
	while (t_parent != t_root)
	{
		t_window = t_parent;
        x11::XQueryTree(t_display, t_window, &t_root, &t_parent, &t_children, &t_child_count);
        x11::XFree(t_children);
	}

    x11::XGetGeometry(t_display, t_window, &t_root, &t_win_x, &t_win_y, &t_width, &t_height, &t_border_width, &t_depth);
    x11::XTranslateCoordinates(t_display, t_window, t_root, 0, 0, &t_win_x, &t_win_y, &t_child);

	MCRectangle t_rect;
	t_rect.x = t_win_x - t_border_width;
	t_rect.y = t_win_y - t_border_width;
	t_rect.width = t_width + t_border_width * 2;
	t_rect.height = t_height + t_border_width * 2;

	return t_rect;
}

// IM-2014-01-29: [[ HiDPI ]] Placeholder method for Linux HiDPI support
MCRectangle MCStack::view_platform_setgeom(const MCRectangle &p_rect)
{
	return view_device_setgeom(p_rect, minwidth, minheight, maxwidth, maxheight);
}

// IM-2013-08-12: [[ ResIndependence ]] factor out device-specific window-sizing code
// set window rect to p_rect, returns old window rect
// IM-2014-01-29: [[ HiDPI ]] Parameterize min/max width/height
MCRectangle MCStack::view_device_setgeom(const MCRectangle &p_rect,
	uint32_t p_minwidth, uint32_t p_minheight,
	uint32_t p_maxwidth, uint32_t p_maxheight)
{
	// Get the position of the window in root coordinates
    gint t_root_x, t_root_y;
    gdk_window_get_origin(window, &t_root_x, &t_root_y);
    
    // Get the dimensions of the window
    gint t_width, t_height;
    t_width = gdk_window_get_width(window);
    t_height = gdk_window_get_height(window);
    
    MCRectangle t_old_rect;
    t_old_rect = MCU_make_rect(t_root_x, t_root_y, t_width, t_height);
    
    if (!(flags & F_WM_PLACE) || state & CS_BEEN_MOVED)
    {
        GdkGeometry t_geom;
        t_geom.win_gravity = GDK_GRAVITY_STATIC;
        
        if (flags & F_RESIZABLE)
        {
            t_geom.min_width = p_minwidth;
            t_geom.max_width = p_maxwidth;
            t_geom.min_height = p_minheight;
            t_geom.max_height = p_maxheight;
        }
        else
        {
            t_geom.min_width = t_geom.max_width = p_rect.width;
            t_geom.min_height = t_geom.max_height = p_rect.height;
        }
        
        gdk_window_set_geometry_hints(window, &t_geom, GdkWindowHints(GDK_HINT_MIN_SIZE|GDK_HINT_MAX_SIZE|GDK_HINT_WIN_GRAVITY));
        gdk_window_move_resize(window, p_rect.x, p_rect.y, p_rect.width, p_rect.height);
    }
    
    if ((!(flags & F_WM_PLACE) || state & CS_BEEN_MOVED) && (t_root_x != p_rect.x || t_root_y != p_rect.y))
    {
        if (t_width != p_rect.width || t_height != p_rect.height)
            gdk_window_move_resize(window, p_rect.x, p_rect.y, p_rect.width, p_rect.height);
        else
            gdk_window_move(window, p_rect.x, p_rect.y);
    }
    else
    {
        if (t_width != p_rect.width || t_height != p_rect.height)
            gdk_window_resize(window, p_rect.width, p_rect.height);
    }

	return t_old_rect;
}

void MCStack::setgeom()
{
	if (MCnoui || !opened)
		return;
	
	if (window == DNULL)
	{
		state &= ~CS_NEED_RESIZE;
		// MW-2011-08-18: [[ Redraw ]] Update to use redraw.
		MCRedrawLockScreen();
		resize(rect . width, rect . height);
		MCRedrawUnlockScreen();
		mode_setgeom();
		return;
	}

	// IM-2013-10-04: [[ FullscreenMode ]] Use view methods to get / set the stack viewport
	MCRectangle t_old_rect;
	t_old_rect = view_getstackviewport();
	
	rect = view_setstackviewport(rect);
	
	state &= ~CS_NEED_RESIZE;
	
	// IM-2013-10-04: [[ FullscreenMode ]] Return values from view methods are
	// in stack coords so don't need to transform
	if (t_old_rect.x != rect.x || t_old_rect.y != rect.y || t_old_rect.width != rect.width || t_old_rect.height != rect.height)
		resize(t_old_rect.width, t_old_rect.height);
		
	state &= ~CS_ISOPENING;
}

void MCStack::start_externals()
{
    loadexternals();
}

void MCStack::stop_externals()
{
    destroywindowshape();
    unloadexternals();
}
 
void MCStack::openwindow(Boolean override)
{
	if (MCModeMakeLocalWindows())
	{
		// MW-2010-11-29: Make sure we reset the geometry on the window before
		//   it gets mapped - otherwise we will get upward drift due to StaticGravity
		//   being used.
		setgeom();
		MCscreen -> openwindow(window, override);
		setmodalhints();
	}
}

void MCStack::setopacity(unsigned char p_level)
{
	// If the stack is not ours to open, then we do nothing ('runtime' mode/remoteable
	// window).
	if (!MCModeMakeLocalWindows())
		return;

	gdouble t_opacity;
	t_opacity = gdouble(p_level) / 255.0;

    if (p_level == 255)
        gdk_window_set_opacity(window, 1.0);
    else
        gdk_window_set_opacity(window, t_opacity);
}

void MCStack::updatemodifiedmark(void)
{
}

void MCStack::redrawicon(void)
{
}

void MCStack::enablewindow(bool p_enable)
{
	gint t_event_mask;
    
    if (p_enable)
    {
        t_event_mask = GDK_ALL_EVENTS_MASK & ~GDK_POINTER_MOTION_HINT_MASK;
    }
    else
    {
        t_event_mask = GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK | GDK_PROPERTY_CHANGE_MASK;
    }
    
	gdk_window_set_events(window, GdkEventMask(t_event_mask));
}

void MCStack::applyscroll(void)
{
}

void MCStack::clearscroll(void)
{
}

////////////////////////////////////////////////////////////////////////////////

void MCBitmapClearRegion(MCBitmap *p_image, int32_t p_x, int32_t p_y, uint32_t p_width, uint32_t p_height)
{
	uint8_t *t_dst_row = (uint8_t*)gdk_pixbuf_get_pixels(p_image) + p_y * gdk_pixbuf_get_rowstride(p_image) + p_x * sizeof(uint32_t);
    for (uint32_t y = 0; y < p_height; y++)
    {
        MCMemoryClear(t_dst_row, p_width * sizeof(uint32_t));
        t_dst_row += gdk_pixbuf_get_rowstride(p_image);
    }
}

////////////////////////////////////////////////////////////////////////////////

static inline MCRectangle MCGRectangleToMCRectangle(const MCGRectangle &p_rect)
{
	return MCU_make_rect(p_rect.origin.x, p_rect.origin.y, p_rect.size.width, p_rect.size.height);
}

////////////////////////////////////////////////////////////////////////////////

class MCLinuxStackSurface: public MCStackSurface
{
	MCStack *m_stack;
	MCRegionRef m_region;

	bool m_locked;
	MCGContextRef m_locked_context;
	MCRectangle m_locked_area;
	
	MCRegionRef m_redraw_region;
	MCBitmap *m_bitmap;
	MCGRaster m_raster;
	MCRectangle m_area;

public:
	MCLinuxStackSurface(MCStack *p_stack, MCRegionRef p_region)
	{
		m_stack = p_stack;
		m_region = p_region;

		m_locked = false;
		m_locked_context = nil;
		
		m_redraw_region = nil;
		m_bitmap = nil;
	}

	bool Lock(void)
	{
		if (m_bitmap != nil)
			return false;
			
		MCRectangle t_actual_area;
		t_actual_area = MCRegionGetBoundingBox(m_region);
		if (MCU_empty_rect(t_actual_area))
			return false;

        //fprintf(stderr, "MCLinuxStackSurface::lock(): %d,%d,%d,%d\n", t_actual_area.x, t_actual_area.y, t_actual_area.width, t_actual_area.height);
        
		bool t_success = true;

		if (t_success)
			t_success = MCRegionCreate(m_redraw_region);

		if (t_success)
			t_success = nil != (m_bitmap = ((MCScreenDC*)MCscreen)->createimage(32, t_actual_area.width, t_actual_area.height, false, 0x00));

		if (t_success)
		{
			m_raster . format = kMCGRasterFormat_ARGB;
			m_raster . width = t_actual_area . width;
			m_raster . height = t_actual_area . height;
			m_raster . stride = gdk_pixbuf_get_rowstride(m_bitmap);
			m_raster . pixels = gdk_pixbuf_get_pixels(m_bitmap);

			m_area = t_actual_area;

			return true;
		}

		MCRegionDestroy(m_redraw_region);
		m_redraw_region = nil;

		if (m_bitmap != nil)
			((MCScreenDC*)MCscreen)->destroyimage(m_bitmap);
		m_bitmap = nil;

		return false;
	}
	
	void Unlock(void)
	{
		Unlock(true);
	}

	void Unlock(bool p_update)
	{
		if (m_bitmap == nil)
			return;

		if (p_update)
		{
			MCWindowShape *t_mask;
			t_mask = m_stack -> getwindowshape();
			if (t_mask != nil && !t_mask -> is_sharp)
			{
				if (m_area.x + m_area.width > t_mask->width)
					MCBitmapClearRegion(m_bitmap, t_mask->width, 0, m_area.x + m_area.width - t_mask->width, m_area.height);
				if (m_area.y + m_area.height > t_mask->height)
					MCBitmapClearRegion(m_bitmap, 0, t_mask->height, m_area.width, m_area.y + m_area.height - t_mask->height);
					
				uint32_t t_width = 0;
				uint32_t t_height = 0;
				if (t_mask->width > m_area.x)
					t_width = MCMin(t_mask->width - m_area.x, m_area.width);
				if (t_mask->height > m_area.y)
					t_height = MCMin(t_mask->height - m_area.y, m_area.height);
					
				void *t_src_ptr;
				t_src_ptr = t_mask -> data + m_area . y * t_mask -> stride + m_area . x;
				surface_merge_with_alpha(m_raster.pixels, m_raster.stride, t_src_ptr, t_mask -> stride, t_width, t_height);
			}

			
			((MCScreenDC*)MCscreen)->putimage(m_stack->getwindow(), m_bitmap, 0, 0, m_area.x, m_area.y, m_area.width, m_area.height);
		}

        MCRegionDestroy(m_redraw_region);
		((MCScreenDC*)MCscreen)->destroyimage(m_bitmap);
		m_bitmap = nil;
	}
	
	bool LockGraphics(MCRegionRef p_area, MCGContextRef& r_context)
	{
		MCGRaster t_raster;
		if (LockPixels(p_area, t_raster))
		{
			if (MCGContextCreateWithRaster(t_raster, m_locked_context))
			{
				// Set origin
				MCGContextTranslateCTM(m_locked_context, -m_locked_area.x, -m_locked_area.y);
				// Set clipping rect
				MCGContextClipToRect(m_locked_context, MCRectangleToMCGRectangle(m_locked_area));
				
				r_context = m_locked_context;
				
				return true;
			}
			
			UnlockPixels();
		}
		
		return false;
	}

	void UnlockGraphics(void)
	{
		if (m_locked_context == nil)
			return;
		
		MCGContextRelease(m_locked_context);
		m_locked_context = nil;
		
		UnlockPixels();
	}

	bool LockPixels(MCRegionRef p_area, MCGRaster &r_raster)
	{
		if (m_bitmap == nil || m_locked)
			return false;

		MCRectangle t_bounds = MCRegionGetBoundingBox(m_region);
		MCRectangle t_actual_area;
		t_actual_area = MCU_intersect_rect(MCRegionGetBoundingBox(p_area), t_bounds);
		if (MCU_empty_rect(t_actual_area))
			return false;

		/* UNCHECKED */ MCRegionIncludeRect(m_redraw_region, t_actual_area);

		uint8_t *t_bits = (uint8_t*)m_raster.pixels + (t_actual_area.y - t_bounds.y) * m_raster.stride + (t_actual_area.x - t_bounds.x) * sizeof(uint32_t);

		m_locked_area = t_actual_area;

		r_raster . format = kMCGRasterFormat_ARGB;
		r_raster . width = t_actual_area . width;
		r_raster . height = t_actual_area . height;
		r_raster . stride = m_raster.stride;
		r_raster . pixels = t_bits;

		m_locked = true;

		return true;
	}

	void UnlockPixels(void)
	{
		m_locked = false;
	}
	
	bool LockTarget(MCStackSurfaceTargetType p_type, void*& r_context)
	{
		return false;
	}
	
	void UnlockTarget(void)
	{
	}

	bool Composite(MCGRectangle p_dst_rect, MCGImageRef p_src, MCGRectangle p_src_rect, MCGFloat p_alpha, MCGBlendMode p_blend)
	{
		bool t_success = true;

		MCGContextRef t_context = nil;
		MCRegionRef t_region = nil;

		t_success = MCRegionCreate(t_region);

		if (t_success)
			t_success = MCRegionSetRect(t_region, MCGRectangleToMCRectangle(p_dst_rect));

		if (t_success)
			t_success = LockGraphics(t_region, t_context);

		if (t_success)
		{
			// MW-2013-11-08: [[ Bug ]] Make sure we set the blend/alpha on the context.
			MCGContextSetBlendMode(t_context, p_blend);
			MCGContextSetOpacity(t_context, p_alpha);
            // MM-2014-01-27: [[ UpdateImageFilters ]] Updated to use new libgraphics image filter types (was nearest).
			MCGContextDrawRectOfImage(t_context, p_src, p_src_rect, p_dst_rect, kMCGImageFilterNone);
		}

		UnlockGraphics();

		MCRegionDestroy(t_region);

		return t_success;
	}
};

// IM-2014-01-29: [[ HiDPI ]] Placeholder method for Linux HiDPI support
void MCStack::view_platform_updatewindow(MCRegionRef p_region)
{
	view_device_updatewindow(p_region);
}

static bool filter_expose(GdkEvent *p_event, void *p_window)
{
    return p_event->any.window == p_window && (p_event->type == GDK_EXPOSE || p_event->type == GDK_DAMAGE);
}

void MCStack::view_device_updatewindow(MCRegionRef p_region)
{
	MCRegionRef t_update_region;
	t_update_region = nil;

    GdkEvent *t_event;
    while (((MCScreenDC*)MCscreen)->GetFilteredEvent(filter_expose, t_event, window))
    {
        if (t_update_region == nil)
            MCRegionCreate(t_update_region);
        
        MCRegionIncludeRect(t_update_region, MCU_make_rect(t_event->expose.area.x, t_event->expose.area.y, t_event->expose.area.width, t_event->expose.area.height));
    }
    
	if (t_update_region != nil)
		MCRegionUnion(t_update_region, t_update_region, p_region);
	else
		t_update_region = p_region;

	onexpose(t_update_region);

	if (t_update_region != p_region)
		MCRegionDestroy(t_update_region);
}

void MCStack::view_platform_updatewindowwithcallback(MCRegionRef p_region, MCStackUpdateCallback p_callback, void *p_context)
{
	s_update_callback = p_callback;
	s_update_context = p_context;
	view_platform_updatewindow(p_region);
	s_update_callback = nil;
	s_update_context = nil;
}

void MCStack::onexpose(MCRegionRef p_region)
{
	MCLinuxStackSurface t_surface(this, p_region);
	if (t_surface.Lock())
	{
		if (s_update_callback == nil)
			view_surface_redrawwindow(&t_surface, p_region);
		else
			s_update_callback(&t_surface, p_region, s_update_context);
			
		t_surface.Unlock();
	}
}

////////////////////////////////////////////////////////////////////////////////

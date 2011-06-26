/*
    This file is part of darktable,
    copyright (c) 2011 Henrik Andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/debug.h"
#include "control/control.h"
#include "control/conf.h"
#include "develop/develop.h"
#include "libs/lib.h"
#include "gui/gtk.h"

DT_MODULE(1)

#define DT_LIB_SNAPSHOTS_COUNT 4

#define HANDLE_SIZE 0.02

/* a snapshot */
typedef struct dt_lib_snapshot_t
{
  GtkWidget *button;
  float zoom_x, zoom_y, zoom_scale;
  int32_t zoom, closeup;
  char filename[512];
} 
dt_lib_snapshot_t;


typedef struct dt_lib_snapshots_t
{
  GtkWidget *snapshots_box;

  uint32_t selected;

  /* current active snapshots */
  uint32_t num_snapshots;

  /* size of snapshots */
  uint32_t size;

  /* snapshots */
  dt_lib_snapshot_t *snapshot;

  /* snapshot cairo surface */
  cairo_surface_t *snapshot_image;

  
  /* change snapshot overlay controls */
  gboolean dragging,vertical,inverted;
  double vp_width,vp_height,vp_xpointer,vp_ypointer;

}
dt_lib_snapshots_t;

/* callback for take snapshot */
static void _lib_snapshots_add_button_clicked_callback (GtkWidget *widget, gpointer user_data);
static void _lib_snapshots_toggled_callback(GtkToggleButton *widget, gpointer user_data);


const char* name()
{
  return _("snapshots");
}

uint32_t views()
{
  return DT_VIEW_DARKROOM;
}

uint32_t container()
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position()
{
  return 1000;
}


/* expose snapshot over center viewport */
void gui_post_expose(dt_lib_module_t *self, cairo_t *cri, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
{
  dt_lib_snapshots_t *d=(dt_lib_snapshots_t *)self->data;
  if(d->snapshot_image)
  {
    d->vp_width = width;
    d->vp_height = height;

    /* check if mouse pointer is on draggable area */
    double xp = pointerx/d->vp_width;
    double yp = pointery/d->vp_height;
    double xpt = xp*0.01;
    double ypt = yp*0.01;
    gboolean mouse_over_control = d->vertical ? ((xp > d->vp_xpointer-xpt && xp < d->vp_xpointer+xpt)?TRUE:FALSE) :
      ((yp > d->vp_ypointer-ypt && yp < d->vp_ypointer+ypt)?TRUE:FALSE);

    /* set x,y,w,h of surface depending on split align and invert */
    double x = d->vertical ? (d->inverted?width*d->vp_xpointer:0) : 0;
    double y = d->vertical ? 0 : (d->inverted?height*d->vp_ypointer:0);
    double w = d->vertical ? (d->inverted?(width * (1.0 - d->vp_xpointer)):width * d->vp_xpointer) : width;
    double h = d->vertical ? height : (d->inverted?(height * (1.0 - d->vp_ypointer)):height * d->vp_ypointer);

    cairo_set_source_surface(cri, d->snapshot_image, 0, 0);
    //cairo_rectangle(cri, 0, 0, width*d->vp_xpointer, height);
    cairo_rectangle(cri,x,y,w,h);
    cairo_fill(cri);

    /* draw the split line */
    cairo_set_source_rgb(cri, .7, .7, .7);        
    cairo_set_line_width(cri, (mouse_over_control ? 2.0 : 0.5) );
    
    if(d->vertical)
    {
      cairo_move_to(cri, width*d->vp_xpointer, 0.0f);
      cairo_line_to(cri, width*d->vp_xpointer, height);
    } else {
      cairo_move_to(cri, 0.0f,  height*d->vp_ypointer);
      cairo_line_to(cri, width, height*d->vp_ypointer); 
    }
    cairo_stroke(cri);

    /* if mouse over control lets draw center rotate control, hide if split is dragged */
    if(!d->dragging && mouse_over_control)
    {
      cairo_set_line_width(cri,0.5);
      double s = width*HANDLE_SIZE;
      dtgtk_cairo_paint_refresh(cri,
				(d->vertical ? width*d->vp_xpointer : width*0.5)-(s*0.5),
				(d->vertical ? height*0.5 : height*d->vp_ypointer)-(s*0.5),
				s,s,d->vertical?1:0);
    }


  }
}

int button_released(struct dt_lib_module_t *self, double x, double y, int which, uint32_t state)
{
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;
  if(d->snapshot_image)
  {
     d->dragging = FALSE;
     return 1;
  } 
  return 0;
}

static int _lib_snapshot_rotation_cnt = 0;

int button_pressed (struct dt_lib_module_t *self, double x, double y, int which, int type, uint32_t state)
{
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;
  if(d->snapshot_image)
  {
    double xp = x/d->vp_width;
    double yp = y/d->vp_height;
    double xpt = xp*0.01;
    double ypt = yp*0.01;

    /* do the split rotating */
    double hhs = HANDLE_SIZE*0.5;
    if (which==1 && (
	((d->vertical && xp > d->vp_xpointer-hhs && xp <  d->vp_xpointer+hhs) && 
	 yp>0.5-hhs && yp<0.5+hhs) ||
	((yp > d->vp_ypointer-hhs && yp < d->vp_ypointer+hhs) && xp>0.5-hhs && xp<0.5+hhs)
		     ))
    {
      /* let's rotate */
      _lib_snapshot_rotation_cnt++;

      d->vertical = !d->vertical;
      if(_lib_snapshot_rotation_cnt%2)
	d->inverted = !d->inverted;

      d->vp_xpointer = xp;
      d->vp_ypointer = yp;
      dt_control_queue_draw_all();
    }
    /* do the dragging !? */
    else if (which==1 && 
	(
	 (d->vertical && xp > d->vp_xpointer-xpt && xp < d->vp_xpointer+xpt) ||
	 (yp > d->vp_ypointer-ypt && yp < d->vp_ypointer+ypt)
	 ))
    {
      d->dragging = TRUE;
      d->vp_ypointer = yp;
      d->vp_xpointer = xp;
      dt_control_queue_draw_all();
    }
    return 1;
  } 
  return 0;
}

int mouse_moved(dt_lib_module_t *self, double x, double y, int which)
{
 
   dt_lib_snapshots_t *d=(dt_lib_snapshots_t *)self->data;

   if(d->snapshot_image)
   {
     double xp = x/d->vp_width;
     double yp = y/d->vp_height;
     //double xpt = xp*0.01;

     /* update x pointer */
     if(d->dragging) 
     {
       d->vp_xpointer = xp;
       d->vp_ypointer = yp;
     }

     /* is mouse over control or in draggin state?, lets redraw */
     //    if(d->dragging || (xp > d->vp_xpointer-xpt && xp < d->vp_xpointer+xpt))
       dt_control_queue_draw_all();

     return 1;
   } 

   return 0;
}

void gui_reset(dt_lib_module_t *self)
{
  dt_lib_snapshots_t *d=(dt_lib_snapshots_t *)self->data;
  d->num_snapshots = 0;
  d->snapshot_image = NULL;

  for(int k=0;k<d->size;k++)
    gtk_widget_hide(d->snapshot[k].button);

  dt_control_queue_draw_all();
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)g_malloc(sizeof(dt_lib_snapshots_t));
  self->data = (void *)d;
  memset(d,0,sizeof(dt_lib_snapshots_t));
  
  /* initialize snapshot storages */
  d->size = 4;
  d->snapshot = (dt_lib_snapshot_t *)g_malloc(sizeof(dt_lib_snapshot_t)*d->size);
  d->vp_xpointer = 0.5;
  d->vp_ypointer = 0.5;
  memset(d->snapshot,0,sizeof(dt_lib_snapshot_t)*d->size);

  /* initialize ui containers */
  self->widget = gtk_vbox_new(FALSE,2);
  d->snapshots_box = gtk_vbox_new(FALSE,0);
  
  /* create take snapshot button */
  GtkWidget *button = gtk_button_new_with_label(_("take snapshot"));
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(_lib_snapshots_add_button_clicked_callback), self);
  g_object_set(button, "tooltip-text", 
		_("take snapshot to compare with another image or the same image at another stage of development"), 
		(char *)NULL);

  /* 
   * initialize snapshots 
   */
  char wdname[32]={0};
  char localdir[4096]={0};
  dt_get_user_local_dir (localdir,4096);

  for (int k=0;k<d->size;k++) {
    /* create snapshot button */
    d->snapshot[k].button = dtgtk_togglebutton_new_with_label (wdname,NULL,CPF_STYLE_FLAT);
    g_signal_connect(G_OBJECT ( d->snapshot[k].button), "clicked",
                      G_CALLBACK (_lib_snapshots_toggled_callback),
                      self);

    /* assign snapshot number to widget */
    g_object_set_data(G_OBJECT(d->snapshot[k].button),"snapshot",(gpointer)(k+1));

    /* setup filename for snapshot */
    snprintf(d->snapshot[k].filename, 512, "%s/tmp/dt_snapshot_%d.png",localdir,k);
    
    /* add button to snapshot box */
    gtk_box_pack_start(GTK_BOX(d->snapshots_box),d->snapshot[k].button,TRUE,TRUE,0);

    /* prevent widget to show on external show all */
    gtk_widget_set_no_show_all(d->snapshot[k].button, TRUE);
  }

  /* add snapshot box and take snapshot button to widget ui*/
  gtk_box_pack_start(GTK_BOX(self->widget), d->snapshots_box,TRUE,TRUE,0);
  gtk_box_pack_start(GTK_BOX(self->widget), button, TRUE,TRUE,0);
  
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;

  g_free(d->snapshot);

  g_free(self->data);
  self->data = NULL;
}

static void _lib_snapshots_add_button_clicked_callback(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t*)user_data;
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;

  if (!darktable.develop->image) return;
  char wdname[64];

  gchar *label1 = g_strdup(gtk_button_get_label (GTK_BUTTON (d->snapshot[0].button)));
  const gchar *oldfilename = g_strdup(d->snapshot[d->size-1].filename);

  for (int k=1; k<d->size; k++)
  {
    if (k < MIN (d->size,d->num_snapshots+1)) 
      gtk_widget_show(d->snapshot[k].button);

    gchar *label2 = g_strdup(gtk_button_get_label (GTK_BUTTON (d->snapshot[k].button)));
    gtk_button_set_label (GTK_BUTTON (d->snapshot[k].button), label1);
    g_free (label1);
    label1 = label2;

    /* move data */
    GtkWidget *tmp = d->snapshot[k].button;
    d->snapshot[k] = d->snapshot[k-1];
    d->snapshot[k].button = tmp;
  }

  /* rotate filenames, so we don't waste hd space */
  snprintf(d->snapshot[0].filename, 512, "%s", oldfilename);
  g_free(label1);

  /* generate a label */
  char *fname = darktable.develop->image->filename + strlen(darktable.develop->image->filename);
  while(fname > darktable.develop->image->filename && *fname != '/') fname--;
  snprintf(wdname, 64, "%s", fname);
  fname = wdname + strlen(wdname);
  while(fname > wdname && *fname != '.') fname --;
  if(*fname != '.') fname = wdname + strlen(wdname);
  if(wdname + 64 - fname > 4) sprintf(fname, "(%d)", darktable.develop->history_end);
 
  /* set new snapshot button label and display button */
  gtk_button_set_label (GTK_BUTTON (d->snapshot[0].button), wdname);
  gtk_widget_show (d->snapshot[0].button);

  /* get zoom pos from develop */
  dt_lib_snapshot_t *s = d->snapshot + 0;
  DT_CTL_GET_GLOBAL (s->zoom_y, dev_zoom_y);
  DT_CTL_GET_GLOBAL (s->zoom_x, dev_zoom_x);
  DT_CTL_GET_GLOBAL (s->zoom, dev_zoom);
  DT_CTL_GET_GLOBAL (s->closeup, dev_closeup);
  DT_CTL_GET_GLOBAL (s->zoom_scale, dev_zoom_scale);

  /* set take snap bit for darkroom */
  d->num_snapshots++;
  dt_dev_snapshot_request(darktable.develop, (const char *)&d->snapshot[0].filename);

}

static void _lib_snapshots_toggled_callback(GtkToggleButton *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t*)user_data;
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;
  /* get current snapshot index */
  int which = (int)g_object_get_data(G_OBJECT(widget),"snapshot");

  /* check if snapshot is activated or inactivated */
  if(!gtk_toggle_button_get_active(widget) && d->selected == which)
  {
    /* if we have a snapshot surface lets destroy it*/
    if(d->snapshot_image)
    {
      cairo_surface_destroy(d->snapshot_image);
      d->snapshot_image = NULL;
      dt_control_gui_queue_draw();
    }
  }
  else if(gtk_toggle_button_get_active(widget))
  { 
    /* get current snapshot index */
    int which = (int)g_object_get_data(G_OBJECT(widget),"snapshot");

    /* lets inactivate all togglebuttons except for self */
    for(int k=0; k<d->size; k++)
      if(GTK_WIDGET(widget) != d->snapshot[k].button) 
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->snapshot[k].button), FALSE);
    
    /* just in case free surface if any */
    if(d->snapshot_image)
    {
      cairo_surface_destroy(d->snapshot_image);
      d->snapshot_image = NULL;
    }

    /* setup snapshot */
    d->selected = which;
    dt_lib_snapshot_t *s = d->snapshot + (which-1);
    DT_CTL_SET_GLOBAL(dev_zoom_y,     s->zoom_y);
    DT_CTL_SET_GLOBAL(dev_zoom_x,     s->zoom_x);
    DT_CTL_SET_GLOBAL(dev_zoom,       s->zoom);
    DT_CTL_SET_GLOBAL(dev_closeup,    s->closeup);
    DT_CTL_SET_GLOBAL(dev_zoom_scale, s->zoom_scale);

    dt_dev_invalidate(darktable.develop);
    d->snapshot_image = cairo_image_surface_create_from_png(s->filename);
   
    dt_control_gui_queue_draw();
  
  }

}

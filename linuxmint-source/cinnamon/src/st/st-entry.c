/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-entry.c: Plain entry actor
 *
 * Copyright 2008, 2009 Intel Corporation
 * Copyright 2009, 2010 Red Hat, Inc.
 * Copyright 2010 Florian Müllner
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:st-entry
 * @short_description: Widget for displaying text
 *
 * #StEntry is a simple widget for displaying text. It derives from
 * #StWidget to add extra style and placement functionality over
 * #ClutterText. The internal #ClutterText is publicly accessibly to allow
 * applications to set further properties.
 *
 * #StEntry supports the following pseudo style states:
 * <itemizedlist>
 *  <listitem>
 *   <para>focus: the widget has focus</para>
 *  </listitem>
 *  <listitem>
 *   <para>indeterminate: the widget is showing the hint text or actor</para>
 *  </listitem>
 *  <listitem>
 *   <para>hover: the widget is showing the hint text and is underneath the
 *                pointer</para>
 *  </listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>

#include <clutter/clutter.h>

#include "st-entry.h"

// #include "st-im-text.h"
#include "st-icon.h"
#include "st-label.h"
#include "st-widget.h"
#include "st-texture-cache.h"
#include "st-clipboard.h"
#include "st-private.h"

#include "st-widget-accessible.h"

#define HAS_FOCUS(actor) (clutter_actor_get_stage (actor) && clutter_stage_get_key_focus ((ClutterStage *) clutter_actor_get_stage (actor)) == actor)


/* properties */
enum
{
  PROP_0,

  PROP_CLUTTER_TEXT,
  PROP_HINT_TEXT,
  PROP_HINT_ACTOR,
  PROP_TEXT,
};

/* signals */
enum
{
  PRIMARY_ICON_CLICKED,
  SECONDARY_ICON_CLICKED,

  LAST_SIGNAL
};

#define ST_ENTRY_PRIV(x) ((StEntry *) x)->priv

static void         st_entry_check_cursor_blink       (StEntry       *entry);
static void         st_entry_pend_cursor_blink        (StEntry       *entry);
static void         st_entry_reset_blink_time         (StEntry       *entry);

struct _StEntryPrivate
{
  ClutterActor *entry;
  gchar        *hint;

  ClutterActor *primary_icon;
  ClutterActor *secondary_icon;

  ClutterActor *hint_actor;

  gfloat        spacing;

  gboolean      hint_visible;
  gboolean      capslock_warning_shown;
  guint         blink_time;
  guint         blink_timeout;
  gboolean      cursor_visible;

  CoglPipeline *text_shadow_material;
  gfloat        shadow_width;
  gfloat        shadow_height;
};

static guint entry_signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (StEntry, st_entry, ST_TYPE_WIDGET);

static GType st_entry_accessible_get_type (void) G_GNUC_CONST;

static void
st_entry_set_property (GObject      *gobject,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  StEntry *entry = ST_ENTRY (gobject);

  switch (prop_id)
    {
    case PROP_HINT_TEXT:
      st_entry_set_hint_text (entry, g_value_get_string (value));
      break;

     case PROP_HINT_ACTOR:
      st_entry_set_hint_actor (entry, g_value_get_object (value));
      break;

    case PROP_TEXT:
      st_entry_set_text (entry, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
st_entry_get_property (GObject    *gobject,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (gobject);

  switch (prop_id)
    {
    case PROP_CLUTTER_TEXT:
      g_value_set_object (value, priv->entry);
      break;

    case PROP_HINT_TEXT:
      g_value_set_string (value, st_entry_get_hint_text (ST_ENTRY (gobject)));
      break;

    case PROP_HINT_ACTOR:
      g_value_set_object (value, priv->hint_actor);
      break;

    case PROP_TEXT:
      g_value_set_string (value, clutter_text_get_text (CLUTTER_TEXT (priv->entry)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
show_capslock_feedback (StEntry *entry)
{
  if (entry->priv->secondary_icon == NULL)
    {
      ClutterActor *icon = g_object_new (ST_TYPE_ICON,
                                         "style-class", "capslock-warning",
                                         "icon-type", ST_ICON_SYMBOLIC,
                                         "icon-name", "dialog-warning",
                                         NULL);

      st_entry_set_secondary_icon (entry, icon);
      entry->priv->capslock_warning_shown = TRUE;
    }
}

static void
remove_capslock_feedback (StEntry *entry)
{
  if (entry->priv->capslock_warning_shown)
    {
      st_entry_set_secondary_icon (entry, NULL);
      entry->priv->capslock_warning_shown = FALSE;
    }
}

static void
keymap_state_changed (ClutterKeymap *keymap,
                      gpointer   user_data)
{
  StEntry *entry = ST_ENTRY (user_data);

  if (clutter_text_get_password_char (CLUTTER_TEXT (entry->priv->entry)) != 0)
    {
      if (clutter_keymap_get_caps_lock_state (keymap))
        show_capslock_feedback (entry);
      else
        remove_capslock_feedback (entry);
    }
}

static void
st_entry_dispose (GObject *object)
{
  StEntry *entry = ST_ENTRY (object);
  StEntryPrivate *priv = entry->priv;
  ClutterKeymap *keymap;
  ClutterSeat *seat;

  cogl_clear_object (&priv->text_shadow_material);

  if (priv->blink_timeout)
    {
      g_source_remove (priv->blink_timeout);
      priv->blink_timeout = 0;
    }

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  keymap = clutter_seat_get_keymap (seat);
  g_signal_handlers_disconnect_by_func (keymap, keymap_state_changed, entry);

  G_OBJECT_CLASS (st_entry_parent_class)->dispose (object);
}

static void
st_entry_update_hint_visibility (StEntry *self)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (self);
  gboolean hint_visible =
    priv->hint_actor != NULL &&
    strcmp (clutter_text_get_text (CLUTTER_TEXT (priv->entry)), "") == 0;

  if (priv->hint_actor)
    g_object_set (priv->hint_actor, "visible", hint_visible, NULL);

  if (hint_visible)
    st_widget_add_style_pseudo_class (ST_WIDGET (self), "indeterminate");
  else
    st_widget_remove_style_pseudo_class (ST_WIDGET (self), "indeterminate");
}

static void
st_entry_style_changed (StWidget *self)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (self);
  StThemeNode *theme_node;
  ClutterColor color;
  gdouble size;

  cogl_clear_object (&priv->text_shadow_material);

  theme_node = st_widget_get_theme_node (self);

  if (st_theme_node_lookup_length (theme_node, "caret-size", TRUE, &size))
    clutter_text_set_cursor_size (CLUTTER_TEXT (priv->entry), (int)(.5 + size));

  if (st_theme_node_lookup_color (theme_node, "caret-color", TRUE, &color))
    clutter_text_set_cursor_color (CLUTTER_TEXT (priv->entry), &color);

  if (st_theme_node_lookup_color (theme_node, "selection-background-color", TRUE, &color))
    clutter_text_set_selection_color (CLUTTER_TEXT (priv->entry), &color);

  if (st_theme_node_lookup_color (theme_node, "selected-color", TRUE, &color))
    clutter_text_set_selected_text_color (CLUTTER_TEXT (priv->entry), &color);

  _st_set_text_from_style ((ClutterText *)priv->entry, theme_node);

  ST_WIDGET_CLASS (st_entry_parent_class)->style_changed (self);
}

static gboolean
st_entry_navigate_focus (StWidget         *widget,
                         ClutterActor     *from,
                         GtkDirectionType  direction)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (widget);

  /* This is basically the same as st_widget_real_navigate_focus(),
   * except that widget is behaving as a proxy for priv->entry (which
   * isn't an StWidget and so has no can-focus flag of its own).
   */

  if (from == priv->entry)
    return FALSE;
  else if (st_widget_get_can_focus (widget))
    {
      clutter_actor_grab_key_focus (priv->entry);
      return TRUE;
    }
  else
    return FALSE;
}

static void
st_entry_get_preferred_width (ClutterActor *actor,
                              gfloat        for_height,
                              gfloat       *min_width_p,
                              gfloat       *natural_width_p)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (actor);
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (actor));
  gfloat hint_w, icon_w;

  st_theme_node_adjust_for_height (theme_node, &for_height);

  clutter_actor_get_preferred_width (priv->entry, for_height,
                                     min_width_p,
                                     natural_width_p);

  if (priv->hint_actor)
    {
      clutter_actor_get_preferred_width (priv->hint_actor, -1, NULL, &hint_w);

      if (min_width_p && hint_w > *min_width_p)
        *min_width_p = hint_w;

      if (natural_width_p && hint_w > *natural_width_p)
        *natural_width_p = hint_w;
    }

  if (priv->primary_icon)
    {
      clutter_actor_get_preferred_width (priv->primary_icon, -1, NULL, &icon_w);

      if (min_width_p)
        *min_width_p += icon_w + priv->spacing;

      if (natural_width_p)
        *natural_width_p += icon_w + priv->spacing;
    }

  if (priv->secondary_icon)
    {
      clutter_actor_get_preferred_width (priv->secondary_icon,
                                         -1, NULL, &icon_w);

      if (min_width_p)
        *min_width_p += icon_w + priv->spacing;

      if (natural_width_p)
        *natural_width_p += icon_w + priv->spacing;
    }

  st_theme_node_adjust_preferred_width (theme_node, min_width_p, natural_width_p);
}

static void
st_entry_get_preferred_height (ClutterActor *actor,
                               gfloat        for_width,
                               gfloat       *min_height_p,
                               gfloat       *natural_height_p)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (actor);
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (actor));
  gfloat hint_h, icon_h;

  st_theme_node_adjust_for_width (theme_node, &for_width);

  clutter_actor_get_preferred_height (priv->entry, for_width,
                                      min_height_p,
                                      natural_height_p);

  if (priv->hint_actor)
    {
      clutter_actor_get_preferred_height (priv->hint_actor, -1, NULL, &hint_h);

      if (min_height_p && hint_h > *min_height_p)
        *min_height_p = hint_h;

      if (natural_height_p && hint_h > *natural_height_p)
        *natural_height_p = hint_h;
    }

  if (priv->primary_icon)
    {
      clutter_actor_get_preferred_height (priv->primary_icon,
                                          -1, NULL, &icon_h);

      if (min_height_p && icon_h > *min_height_p)
        *min_height_p = icon_h;

      if (natural_height_p && icon_h > *natural_height_p)
        *natural_height_p = icon_h;
    }

  if (priv->secondary_icon)
    {
      clutter_actor_get_preferred_height (priv->secondary_icon,
                                          -1, NULL, &icon_h);

      if (min_height_p && icon_h > *min_height_p)
        *min_height_p = icon_h;

      if (natural_height_p && icon_h > *natural_height_p)
        *natural_height_p = icon_h;
    }

  st_theme_node_adjust_preferred_height (theme_node, min_height_p, natural_height_p);
}

static void
st_entry_allocate (ClutterActor          *actor,
                   const ClutterActorBox *box,
                   ClutterAllocationFlags flags)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (actor);
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (actor));
  ClutterActorBox content_box, child_box, icon_box, hint_box;
  gfloat icon_w, icon_h;
  gfloat hint_w, hint_h;
  gfloat entry_h, min_h, pref_h, avail_h;
  gboolean is_rtl;

  is_rtl = clutter_actor_get_text_direction (actor) == CLUTTER_TEXT_DIRECTION_RTL;

  clutter_actor_set_allocation (actor, box, flags);

  st_theme_node_get_content_box (theme_node, box, &content_box);

  avail_h = content_box.y2 - content_box.y1;

  child_box.x1 = content_box.x1;
  child_box.x2 = content_box.x2;

  if (priv->primary_icon)
    {
      clutter_actor_get_preferred_width (priv->primary_icon,
                                         -1, NULL, &icon_w);
      clutter_actor_get_preferred_height (priv->primary_icon,
                                          -1, NULL, &icon_h);

      icon_box.x1 = content_box.x1;
      icon_box.x2 = icon_box.x1 + icon_w;

      icon_box.y1 = (int) (content_box.y1 + avail_h / 2 - icon_h / 2);
      icon_box.y2 = icon_box.y1 + icon_h;

      clutter_actor_allocate (priv->primary_icon,
                              &icon_box,
                              flags);

      /* reduce the size for the entry */
      child_box.x1 = MIN (child_box.x2, child_box.x1 + icon_w + priv->spacing);
    }

  if (priv->secondary_icon)
    {
      clutter_actor_get_preferred_width (priv->secondary_icon,
                                         -1, NULL, &icon_w);
      clutter_actor_get_preferred_height (priv->secondary_icon,
                                          -1, NULL, &icon_h);

      icon_box.x2 = content_box.x2;
      icon_box.x1 = icon_box.x2 - icon_w;

      icon_box.y1 = (int) (content_box.y1 + avail_h / 2 - icon_h / 2);
      icon_box.y2 = icon_box.y1 + icon_h;

      clutter_actor_allocate (priv->secondary_icon,
                              &icon_box,
                              flags);

      /* reduce the size for the entry */
       child_box.x2 = MAX (child_box.x1, child_box.x2 - icon_w - priv->spacing);
    }

  if (priv->hint_actor)
    {
      /* now allocate the hint actor */
      hint_box = child_box;

      clutter_actor_get_preferred_width (priv->hint_actor, -1, NULL, &hint_w);
      clutter_actor_get_preferred_height (priv->hint_actor, -1, NULL, &hint_h);

      if (is_rtl)
        hint_box.x1 = hint_box.x2 - hint_w;
      else
        hint_box.x2 = hint_box.x1 + hint_w;

      hint_box.y1 = ceil (content_box.y1 + avail_h / 2 - hint_h / 2);
      hint_box.y2 = hint_box.y1 + hint_h;

      clutter_actor_allocate (priv->hint_actor, &hint_box, flags);
    }

  clutter_actor_get_preferred_height (priv->entry, child_box.x2 - child_box.x1,
                                      &min_h, &pref_h);

  entry_h = CLAMP (pref_h, min_h, avail_h);

  child_box.y1 = (int) (content_box.y1 + avail_h / 2 - entry_h / 2);
  child_box.y2 = child_box.y1 + entry_h;

  clutter_actor_allocate (priv->entry, &child_box, flags);
}

#define CURSOR_ON_MULTIPLIER 2
#define CURSOR_OFF_MULTIPLIER 1
#define CURSOR_PEND_MULTIPLIER 3
#define CURSOR_DIVIDER 3

static gboolean
cursor_blinks (StEntry *entry)
{
  StEntryPrivate *priv = entry->priv;

  if (clutter_actor_has_key_focus (CLUTTER_ACTOR (priv->entry)) &&
      clutter_text_get_editable (CLUTTER_TEXT (priv->entry)) &&
      clutter_text_get_selection_bound (CLUTTER_TEXT (priv->entry)) == clutter_text_get_cursor_position (CLUTTER_TEXT (priv->entry)))
    {
      gboolean blink;
      g_object_get (gtk_settings_get_default (), "gtk-cursor-blink", &blink, NULL);

      return blink;
    }
  else
    return FALSE;
}

static gint
get_cursor_time (StEntry *entry)
{
  gint time;
  g_object_get (gtk_settings_get_default (), "gtk-cursor-blink-time", &time, NULL);

  return time;
}

static gint
get_cursor_blink_timeout (StEntry *entry)
{
  gint timeout;
  g_object_get (gtk_settings_get_default (), "gtk-cursor-blink-timeout", &timeout, NULL);

  return timeout;
}

static void
show_cursor (StEntry *entry)
{
  StEntryPrivate *priv = entry->priv;

  if (!priv->cursor_visible)
    {
      priv->cursor_visible = TRUE;
      clutter_text_set_cursor_visible (CLUTTER_TEXT (priv->entry), TRUE);
    }
}

static void
hide_cursor (StEntry *entry)
{
  StEntryPrivate *priv = entry->priv;

  if (priv->cursor_visible)
    {
      priv->cursor_visible = FALSE;
      clutter_text_set_cursor_visible (CLUTTER_TEXT (priv->entry), FALSE);
    }
}

/*
 * Blink!
 */
static gint
blink_cb (gpointer data)
{
  StEntry *entry;
  StEntryPrivate *priv;
  guint blink_timeout;

  entry = ST_ENTRY (data);
  priv = entry->priv;

  if (!clutter_actor_has_key_focus (priv->entry))
    {
      g_warning ("StEntry - did not receive key-focus-out event. If you\n"
         "connect a handler to this signal, it must return\n"
         "FALSE so the StEntry gets the event as well");

      st_entry_check_cursor_blink (entry);

      return FALSE;
    }

  if (clutter_text_get_selection_bound (CLUTTER_TEXT (priv->entry)) != clutter_text_get_cursor_position (CLUTTER_TEXT (priv->entry)))
    {
      st_entry_check_cursor_blink (entry);
      return FALSE;
    }

  blink_timeout = get_cursor_blink_timeout (entry);
  if (priv->blink_time > 1000 * blink_timeout &&
      blink_timeout < G_MAXINT/1000)
    {
      /* we've blinked enough without the user doing anything, stop blinking */
      show_cursor (entry);
      priv->blink_timeout = 0;
    }
  else if (priv->cursor_visible)
    {
      hide_cursor (entry);
      priv->blink_timeout = clutter_threads_add_timeout (get_cursor_time (entry) * CURSOR_OFF_MULTIPLIER / CURSOR_DIVIDER,
                        blink_cb,
                        entry);
    }
  else
    {
      show_cursor (entry);
      priv->blink_time += get_cursor_time (entry);
      priv->blink_timeout = clutter_threads_add_timeout (get_cursor_time (entry) * CURSOR_ON_MULTIPLIER / CURSOR_DIVIDER,
                        blink_cb,
                        entry);
    }

  /* Remove ourselves */
  return FALSE;
}

static void
st_entry_check_cursor_blink (StEntry *entry)
{
  StEntryPrivate *priv = entry->priv;

  if (cursor_blinks (entry))
    {
      if (!priv->blink_timeout)
    {
      show_cursor (entry);
      priv->blink_timeout = clutter_threads_add_timeout (get_cursor_time (entry) * CURSOR_ON_MULTIPLIER / CURSOR_DIVIDER,
                        blink_cb,
                        entry);
    }
    }
  else
    {
      if (priv->blink_timeout)
        {
          g_source_remove (priv->blink_timeout);
          priv->blink_timeout = 0;
        }

      show_cursor (entry);
    }
}

static void
st_entry_pend_cursor_blink (StEntry *entry)
{
  StEntryPrivate *priv = entry->priv;

  if (cursor_blinks (entry))
    {
      if (priv->blink_timeout != 0)
    g_source_remove (priv->blink_timeout);

      priv->blink_timeout = clutter_threads_add_timeout (get_cursor_time (entry) * CURSOR_PEND_MULTIPLIER / CURSOR_DIVIDER,
                                                     blink_cb,
                                                     entry);
      show_cursor (entry);
    }
}

static void
st_entry_reset_blink_time (StEntry *entry)
{
  StEntryPrivate *priv = entry->priv;

  priv->blink_time = 0;
}

static void
clutter_text_focus_in_cb (ClutterText  *text,
                          ClutterActor *actor)
{
  StEntry *entry = ST_ENTRY (actor);
  ClutterSeat *seat;
  ClutterKeymap *keymap;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  keymap = clutter_seat_get_keymap (seat);

  keymap_state_changed (keymap, entry);
  g_signal_connect (keymap, "state-changed",
                    G_CALLBACK (keymap_state_changed), entry);

  st_widget_add_style_pseudo_class (ST_WIDGET (actor), "focus");

  st_entry_reset_blink_time (entry);
  st_entry_check_cursor_blink (entry);
}

static void
clutter_text_focus_out_cb (ClutterText  *text,
                           ClutterActor *actor)
{
  StEntry *entry = ST_ENTRY (actor);
  ClutterKeymap *keymap;
  ClutterSeat *seat;

  st_widget_remove_style_pseudo_class (ST_WIDGET (actor), "focus");

  st_entry_check_cursor_blink (entry);
  remove_capslock_feedback (entry);

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  keymap = clutter_seat_get_keymap (seat);
  g_signal_handlers_disconnect_by_func (keymap, keymap_state_changed, entry);
}

static void
clutter_text_password_char_cb (GObject    *object,
                               GParamSpec *pspec,
                               gpointer    user_data)
{
  StEntry *entry = ST_ENTRY (user_data);

  if (clutter_text_get_password_char (CLUTTER_TEXT (entry->priv->entry)) == 0)
    remove_capslock_feedback (entry);
}

static void
clutter_text_selection_bound_cb (GObject    *object,
                                 GParamSpec *pspec,
                                 gpointer    user_data)
{
  StEntry *entry = ST_ENTRY (user_data);

  st_entry_reset_blink_time (entry);
  st_entry_pend_cursor_blink (entry);
}

static void
clutter_text_cursor_changed (ClutterText *text, ClutterActor *actor)
{
  st_entry_reset_blink_time (ST_ENTRY (actor));
  st_entry_pend_cursor_blink (ST_ENTRY (actor));
}

static void
clutter_text_changed_cb (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  StEntry *entry = ST_ENTRY (user_data);
  StEntryPrivate *priv = ST_ENTRY_PRIV (entry);

  st_entry_update_hint_visibility (entry);

  /* Since the text changed, force a regen of the shadow texture */
  cogl_clear_object (&priv->text_shadow_material);

  g_object_notify (G_OBJECT (entry), "text");
}

static void
st_entry_clipboard_callback (StClipboard *clipboard,
                             const gchar *text,
                             gpointer     data)
{
  ClutterText *ctext;
  gint cursor_pos;

  if (!text)
    return;

  ctext = (ClutterText*)((StEntry *) data)->priv->entry;

  /* delete the current selection before pasting */
  clutter_text_delete_selection (ctext);

  /* "paste" the clipboard text into the entry */
  cursor_pos = clutter_text_get_cursor_position (ctext);
  clutter_text_insert_text (ctext, text, cursor_pos);
}

static gboolean
clutter_text_button_press_event (ClutterActor       *actor,
                                 ClutterButtonEvent *event,
                                 gpointer            user_data)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (user_data);
  GtkSettings *settings = gtk_settings_get_default ();
  gboolean primary_paste_enabled;

  g_object_get (settings,
                "gtk-enable-primary-paste", &primary_paste_enabled,
                NULL);

  if (primary_paste_enabled && event->button == 2
      && clutter_text_get_editable (CLUTTER_TEXT (priv->entry)))
    {
      StClipboard *clipboard;

      clipboard = st_clipboard_get_default ();

      /* By the time the clipboard callback is called,
       * the rest of the signal handlers will have
       * run, making the text cursor to be in the correct
       * place.
       */
      st_clipboard_get_text (clipboard,
                             ST_CLIPBOARD_TYPE_PRIMARY,
                             st_entry_clipboard_callback,
                             user_data);
    }

  return FALSE;
}


static gboolean
st_entry_key_press_event (ClutterActor    *actor,
                          ClutterKeyEvent *event)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (actor);

  st_entry_reset_blink_time (ST_ENTRY (actor));
  st_entry_pend_cursor_blink (ST_ENTRY (actor));

  /* This is expected to handle events that were emitted for the inner
     ClutterText. They only reach this function if the ClutterText
     didn't handle them */

  /* paste */
  if ((event->modifier_state & CLUTTER_CONTROL_MASK)
      && (event->keyval == CLUTTER_KEY_v || event->keyval == CLUTTER_KEY_V))
    {
      StClipboard *clipboard;

      clipboard = st_clipboard_get_default ();

      st_clipboard_get_text (clipboard,
                             ST_CLIPBOARD_TYPE_CLIPBOARD,
                             st_entry_clipboard_callback,
                             actor);

      return TRUE;
    }

  /* copy */
  if ((event->modifier_state & CLUTTER_CONTROL_MASK)
      && (event->keyval == CLUTTER_KEY_c || event->keyval == CLUTTER_KEY_C))
    {
      StClipboard *clipboard;
      gchar *text;

      clipboard = st_clipboard_get_default ();

      text = clutter_text_get_selection ((ClutterText*) priv->entry);

      if (text && strlen (text))
        st_clipboard_set_text (clipboard,
                               ST_CLIPBOARD_TYPE_CLIPBOARD,
                               text);

      return TRUE;
    }


  /* cut */
  if ((event->modifier_state & CLUTTER_CONTROL_MASK)
      && (event->keyval == CLUTTER_KEY_x || event->keyval == CLUTTER_KEY_X))
    {
      StClipboard *clipboard;
      gchar *text;

      clipboard = st_clipboard_get_default ();

      text = clutter_text_get_selection ((ClutterText*) priv->entry);

      if (text && strlen (text))
        {
          st_clipboard_set_text (clipboard,ST_CLIPBOARD_TYPE_CLIPBOARD, text);

          /* now delete the text */
          clutter_text_delete_selection ((ClutterText *) priv->entry);
        }

      return TRUE;
    }

  return CLUTTER_ACTOR_CLASS (st_entry_parent_class)->key_press_event (actor, event);
}

static void
st_entry_key_focus_in (ClutterActor *actor)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (actor);

  /* We never want key focus. The ClutterText should be given first
     pass for all key events */
  clutter_actor_grab_key_focus (priv->entry);
}

static void
st_entry_paint (ClutterActor        *actor,
                ClutterPaintContext *paint_context)
{
  StEntryPrivate *priv = ST_ENTRY_PRIV (actor);
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (actor));
  StShadow *shadow_spec = st_theme_node_get_text_shadow (theme_node);
  ClutterActorClass *parent_class;

  st_widget_paint_background (ST_WIDGET (actor), paint_context);

  if (shadow_spec)
    {
      ClutterActorBox allocation;
      float width, height;

      clutter_actor_get_allocation_box (priv->entry, &allocation);
      clutter_actor_box_get_size (&allocation, &width, &height);

      if (priv->text_shadow_material == NULL ||
          width != priv->shadow_width ||
          height != priv->shadow_height)
        {
          CoglPipeline *material;

          cogl_clear_object (&priv->text_shadow_material);

          material = _st_create_shadow_pipeline_from_actor (shadow_spec,
                                                            priv->entry);

          priv->shadow_width = width;
          priv->shadow_height = height;
          priv->text_shadow_material = material;
        }

      if (priv->text_shadow_material != NULL)
        {
          CoglFramebuffer *framebuffer =
            clutter_paint_context_get_framebuffer (paint_context);

          _st_paint_shadow_with_opacity (shadow_spec,
                                         priv->text_shadow_material,
                                         framebuffer,
                                         &allocation,
                                         clutter_actor_get_paint_opacity (priv->entry));
        }
    }

  /* Since we paint the background ourselves, chain to the parent class
   * of StWidget, to avoid painting it twice.
   * This is needed as we still want to paint children.
   */
  parent_class = g_type_class_peek_parent (st_entry_parent_class);
  parent_class->paint (actor, paint_context);
}

static void
st_entry_class_init (StEntryClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  StWidgetClass *widget_class = ST_WIDGET_CLASS (klass);
  GParamSpec *pspec;

  gobject_class->set_property = st_entry_set_property;
  gobject_class->get_property = st_entry_get_property;
  gobject_class->dispose = st_entry_dispose;

  actor_class->get_preferred_width = st_entry_get_preferred_width;
  actor_class->get_preferred_height = st_entry_get_preferred_height;
  actor_class->allocate = st_entry_allocate;
  actor_class->paint = st_entry_paint;

  actor_class->key_press_event = st_entry_key_press_event;
  actor_class->key_focus_in = st_entry_key_focus_in;

  widget_class->style_changed = st_entry_style_changed;
  widget_class->navigate_focus = st_entry_navigate_focus;
  widget_class->get_accessible_type = st_entry_accessible_get_type;

  pspec = g_param_spec_object ("clutter-text",
             "Clutter Text",
             "Internal ClutterText actor",
             CLUTTER_TYPE_TEXT,
             G_PARAM_READABLE);
  g_object_class_install_property (gobject_class, PROP_CLUTTER_TEXT, pspec);

  pspec = g_param_spec_string ("hint-text",
                               "Hint Text",
                               "Text to display when the entry is not focused "
                               "and the text property is empty",
                               NULL, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_HINT_TEXT, pspec);

  pspec = g_param_spec_object ("hint-actor",
                               "Hint Actor",
                               "An actor to display when the entry is not focused "
                               "and the text property is empty",
                               CLUTTER_TYPE_ACTOR,
                               G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_HINT_ACTOR, pspec);

  pspec = g_param_spec_string ("text",
                               "Text",
                               "Text of the entry",
                               NULL, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_TEXT, pspec);

  /* signals */
  /**
   * StEntry::primary-icon-clicked:
   *
   * Emitted when the primary icon is clicked
   */
  entry_signals[PRIMARY_ICON_CLICKED] =
    g_signal_new ("primary-icon-clicked",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (StEntryClass, primary_icon_clicked),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  /**
   * StEntry::secondary-icon-clicked:
   *
   * Emitted when the secondary icon is clicked
   */
  entry_signals[SECONDARY_ICON_CLICKED] =
    g_signal_new ("secondary-icon-clicked",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (StEntryClass, secondary_icon_clicked),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
st_entry_init (StEntry *entry)
{
  StEntryPrivate *priv;

  priv = entry->priv = st_entry_get_instance_private (entry);

  priv->entry = g_object_new (CLUTTER_TYPE_TEXT,
                              "line-alignment", PANGO_ALIGN_LEFT,
                              "editable", TRUE,
                              "reactive", TRUE,
                              "single-line-mode", TRUE,
                              NULL);

  g_signal_connect (priv->entry, "key-focus-in",
                    G_CALLBACK (clutter_text_focus_in_cb), entry);

  g_signal_connect (priv->entry, "key-focus-out",
                    G_CALLBACK (clutter_text_focus_out_cb), entry);

  g_signal_connect (priv->entry, "notify::password-char",
                    G_CALLBACK (clutter_text_password_char_cb), entry);

  g_signal_connect (priv->entry, "button-press-event",
                    G_CALLBACK (clutter_text_button_press_event), entry);

  g_signal_connect (priv->entry, "notify::selection-bound",
                    G_CALLBACK (clutter_text_selection_bound_cb), entry);

  g_signal_connect (priv->entry, "cursor-changed",
                    G_CALLBACK (clutter_text_cursor_changed), entry);

  g_signal_connect (priv->entry, "notify::text",
                    G_CALLBACK (clutter_text_changed_cb), entry);

  priv->spacing = 6.0f;

  priv->text_shadow_material = NULL;
  priv->shadow_width = -1.;
  priv->shadow_height = -1.;

  clutter_actor_add_child (CLUTTER_ACTOR (entry), priv->entry);
  clutter_actor_set_reactive ((ClutterActor *) entry, TRUE);

  /* set cursor hidden until we receive focus */
  priv->blink_timeout = 0;
  priv->blink_time = 0;
  priv->cursor_visible = FALSE;
  clutter_text_set_cursor_visible ((ClutterText *) priv->entry, FALSE);
}

/**
 * st_entry_new:
 * @text: text to set the entry to
 *
 * Create a new #StEntry with the specified entry
 *
 * Returns: a new #StEntry
 */
StWidget *
st_entry_new (const gchar *text)
{
  StWidget *entry;

  /* add the entry to the stage, but don't allow it to be visible */
  entry = g_object_new (ST_TYPE_ENTRY,
                        "text", text,
                        NULL);

  return entry;
}

/**
 * st_entry_get_text:
 * @entry: a #StEntry
 *
 * Get the text displayed on the entry
 *
 * Returns: the text for the entry. This must not be freed by the application
 */
const gchar *
st_entry_get_text (StEntry *entry)
{
  g_return_val_if_fail (ST_IS_ENTRY (entry), NULL);

  return clutter_text_get_text (CLUTTER_TEXT (entry->priv->entry));
}

/**
 * st_entry_set_text:
 * @entry: a #StEntry
 * @text: (allow-none): text to set the entry to
 *
 * Sets the text displayed on the entry
 */
void
st_entry_set_text (StEntry     *entry,
                   const gchar *text)
{
  StEntryPrivate *priv;

  g_return_if_fail (ST_IS_ENTRY (entry));

  priv = entry->priv;

  clutter_text_set_text (CLUTTER_TEXT (priv->entry), text);

  /* Note: PROP_TEXT will get notfied from our notify::text handler connected
   * to priv->entry. */
}

/**
 * st_entry_get_clutter_text:
 * @entry: a #StEntry
 *
 * Retrieve the internal #ClutterText so that extra parameters can be set
 *
 * Returns: (transfer none): the #ClutterText used by #StEntry. The entry is
 * owned by the #StEntry and should not be unref'ed by the application.
 */
ClutterActor*
st_entry_get_clutter_text (StEntry *entry)
{
  g_return_val_if_fail (ST_ENTRY (entry), NULL);

  return entry->priv->entry;
}

/**
 * st_entry_set_hint_text:
 * @entry: a #StEntry
 * @text: (allow-none): text to set as the entry hint
 *
 * Sets the text to display when the entry is empty and unfocused. When the
 * entry is displaying the hint, it has a pseudo class of "indeterminate".
 * A value of NULL unsets the hint.
 */
void
st_entry_set_hint_text (StEntry     *entry,
                        const gchar *text)
{
  StWidget *label;

  g_return_if_fail (ST_IS_ENTRY (entry));

  label = st_label_new (text);
  st_widget_add_style_class_name (label, "hint-text");

  st_entry_set_hint_actor (ST_ENTRY (entry), CLUTTER_ACTOR (label));
}

/**
 * st_entry_get_hint_text:
 * @entry: a #StEntry
 *
 * Gets the text that is displayed when the entry is empty and unfocused
 *
 * Returns: the current value of the hint property. This string is owned by the
 * #StEntry and should not be freed or modified.
 */
const gchar *
st_entry_get_hint_text (StEntry *entry)
{
  StEntryPrivate *priv;

  g_return_val_if_fail (ST_IS_ENTRY (entry), NULL);

  priv = entry->priv;

  if (priv->hint_actor != NULL && ST_IS_LABEL (priv->hint_actor))
    return st_label_get_text (ST_LABEL (priv->hint_actor));

  return NULL;
}

static gboolean
_st_entry_icon_press_cb (ClutterActor       *actor,
                         ClutterButtonEvent *event,
                         StEntry            *entry)
{
  StEntryPrivate *priv = entry->priv;

  if (actor == priv->primary_icon)
    g_signal_emit (entry, entry_signals[PRIMARY_ICON_CLICKED], 0);
  else
    g_signal_emit (entry, entry_signals[SECONDARY_ICON_CLICKED], 0);

  return FALSE;
}

static void
_st_entry_set_icon (StEntry       *entry,
                    ClutterActor **icon,
                    ClutterActor  *new_icon)
{
  if (*icon)
    {
      g_signal_handlers_disconnect_by_func (*icon,
                                            _st_entry_icon_press_cb,
                                            entry);
      clutter_actor_remove_child (CLUTTER_ACTOR (entry), *icon);
      *icon = NULL;
    }

  if (new_icon)
    {
      *icon = g_object_ref (new_icon);

      clutter_actor_set_reactive (*icon, TRUE);
      clutter_actor_add_child (CLUTTER_ACTOR (entry), *icon);
      g_signal_connect (*icon, "button-release-event",
                        G_CALLBACK (_st_entry_icon_press_cb), entry);
    }

  clutter_actor_queue_relayout (CLUTTER_ACTOR (entry));
}

static void
_st_entry_set_icon_from_file (StEntry       *entry,
                              ClutterActor **icon,
                              const gchar   *filename)
{
  ClutterActor *new_icon = NULL;

  if (filename)
    {
      StTextureCache *cache;
      GFile *file;
      gchar *uri;

      cache = st_texture_cache_get_default ();
      file = g_file_new_for_path (filename);
      uri = g_file_get_uri (file);

      g_object_unref (file);

      new_icon = (ClutterActor*) st_texture_cache_load_uri_async (cache, uri, -1, -1);
      g_free (uri);
    }

  _st_entry_set_icon  (entry, icon, new_icon);
}

/**
 * st_entry_set_primary_icon:
 * @entry: a #StEntry
 * @icon: (allow-none): a #ClutterActor
 *
 * Set the primary icon of the entry to @icon
 */
void
st_entry_set_primary_icon (StEntry      *entry,
                           ClutterActor *icon)
{
  StEntryPrivate *priv;

  g_return_if_fail (ST_IS_ENTRY (entry));

  priv = entry->priv;

  _st_entry_set_icon (entry, &priv->primary_icon, icon);
}

/**
 * st_entry_set_secondary_icon:
 * @entry: a #StEntry
 * @icon: (allow-none): an #ClutterActor
 *
 * Set the secondary icon of the entry to @icon
 */
void
st_entry_set_secondary_icon (StEntry      *entry,
                             ClutterActor *icon)
{
  StEntryPrivate *priv;

  g_return_if_fail (ST_IS_ENTRY (entry));

  priv = entry->priv;

  _st_entry_set_icon (entry, &priv->secondary_icon, icon);
}

/**
 * st_entry_set_primary_icon_from_file:
 * @entry: a #StEntry
 * @filename: (allow-none): filename of an icon
 *
 * Set the primary icon of the entry to the given filename
 */
void
st_entry_set_primary_icon_from_file (StEntry     *entry,
                                     const gchar *filename)
{
  StEntryPrivate *priv;

  g_return_if_fail (ST_IS_ENTRY (entry));

  priv = entry->priv;

  _st_entry_set_icon_from_file (entry, &priv->primary_icon, filename);

}

/**
 * st_entry_set_secondary_icon_from_file:
 * @entry: a #StEntry
 * @filename: (allow-none): filename of an icon
 *
 * Set the primary icon of the entry to the given filename
 */
void
st_entry_set_secondary_icon_from_file (StEntry     *entry,
                                       const gchar *filename)
{
  StEntryPrivate *priv;

  g_return_if_fail (ST_IS_ENTRY (entry));

  priv = entry->priv;

  _st_entry_set_icon_from_file (entry, &priv->secondary_icon, filename);

}

/**
 * st_entry_set_hint_actor:
 * @entry: a #StEntry
 * @hint_actor: (allow-none): a #ClutterActor
 *
 * Set the hint actor of the entry to @hint_actor
 */
void
st_entry_set_hint_actor (StEntry      *entry,
                         ClutterActor *hint_actor)
{
  StEntryPrivate *priv;

  g_return_if_fail (ST_IS_ENTRY (entry));

  priv = entry->priv;

  if (priv->hint_actor != NULL)
    {
      clutter_actor_remove_child (CLUTTER_ACTOR (entry), priv->hint_actor);
      priv->hint_actor = NULL;
    }

  if (hint_actor != NULL)
    {
      priv->hint_actor = hint_actor;
      clutter_actor_add_child (CLUTTER_ACTOR (entry), priv->hint_actor);
    }

  st_entry_update_hint_visibility (entry);

  clutter_actor_queue_relayout (CLUTTER_ACTOR (entry));
}

/**
 * st_entry_get_hint_actor:
 * @entry: a #StEntry
 *
 * Returns: (transfer none): a #ClutterActor
 */
ClutterActor *
st_entry_get_hint_actor (StEntry *entry)
{
  StEntryPrivate *priv;

  g_return_val_if_fail (ST_IS_ENTRY (entry), NULL);

  priv = entry->priv;
  return priv->hint_actor;
}

/******************************************************************************/
/*************************** ACCESSIBILITY SUPPORT ****************************/
/******************************************************************************/

#define ST_TYPE_ENTRY_ACCESSIBLE         (st_entry_accessible_get_type ())
#define ST_ENTRY_ACCESSIBLE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), ST_TYPE_ENTRY_ACCESSIBLE, StEntryAccessible))
#define ST_IS_ENTRY_ACCESSIBLE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), ST_TYPE_ENTRY_ACCESSIBLE))
#define ST_ENTRY_ACCESSIBLE_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c),    ST_TYPE_ENTRY_ACCESSIBLE, StEntryAccessibleClass))
#define ST_IS_ENTRY_ACCESSIBLE_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),    ST_TYPE_ENTRY_ACCESSIBLE))
#define ST_ENTRY_ACCESSIBLE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o),  ST_TYPE_ENTRY_ACCESSIBLE, StEntryAccessibleClass))

typedef struct _StEntryAccessible  StEntryAccessible;
typedef struct _StEntryAccessibleClass  StEntryAccessibleClass;

struct _StEntryAccessible
{
  StWidgetAccessible parent;
};

struct _StEntryAccessibleClass
{
  StWidgetAccessibleClass parent_class;
};

G_DEFINE_TYPE (StEntryAccessible, st_entry_accessible, ST_TYPE_WIDGET_ACCESSIBLE)

static void
st_entry_accessible_init (StEntryAccessible *self)
{
  /* initialization done on AtkObject->initialize */
}

static void
st_entry_accessible_initialize (AtkObject *obj,
                                gpointer   data)
{
  ATK_OBJECT_CLASS (st_entry_accessible_parent_class)->initialize (obj, data);

  /* StEntry is behaving as a StImText container */
  atk_object_set_role (obj, ATK_ROLE_PANEL);
}

static gint
st_entry_accessible_get_n_children (AtkObject *obj)
{
  StEntry *entry = NULL;

  g_return_val_if_fail (ST_IS_ENTRY_ACCESSIBLE (obj), 0);

  entry = ST_ENTRY (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (obj)));

  if (entry == NULL)
    return 0;

  if (entry->priv->entry == NULL)
    return 0;
  else
    return 1;
}

static AtkObject*
st_entry_accessible_ref_child (AtkObject *obj,
                               gint       i)
{
  StEntry *entry = NULL;
  AtkObject *result = NULL;

  g_return_val_if_fail (ST_IS_ENTRY_ACCESSIBLE (obj), NULL);
  g_return_val_if_fail (i == 0, NULL);

  entry = ST_ENTRY (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (obj)));

  if (entry == NULL)
    return NULL;

  if (entry->priv->entry == NULL)
    return NULL;

  result = clutter_actor_get_accessible (entry->priv->entry);
  g_object_ref (result);

  return result;
}


static void
st_entry_accessible_class_init (StEntryAccessibleClass *klass)
{
  AtkObjectClass *atk_class = ATK_OBJECT_CLASS (klass);

  atk_class->initialize = st_entry_accessible_initialize;
  atk_class->get_n_children = st_entry_accessible_get_n_children;
  atk_class->ref_child= st_entry_accessible_ref_child;
}

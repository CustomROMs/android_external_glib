/* gbinding.c: Binding for object properties
 *
 * Copyright (C) 2010  Intel Corp.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

/**
 * SECTION:gbinding
 * @Title: GBinding
 * @Short_Description: Bind two object properties
 *
 * #GBinding is the representation of a binding between a property on a
 * #GObject instance (or source) and another property on another #GObject
 * instance (or target). Whenever the source property changes, the same
 * value is applied to the target property; for instance, the following
 * binding:
 *
 * |[
 *   g_object_bind_property (object1, "property-a",
 *                           object2, "property-b",
 *                           G_BINDING_DEFAULT);
 * ]|
 *
 * will cause <emphasis>object2:property-b</emphasis> to be updated every
 * time g_object_set() or the specific accessor changes the value of
 * <emphasis>object1:property-a</emphasis>.
 *
 * It is possible to create a bidirectional binding between two properties
 * of two #GObject instances, so that if either property changes, the
 * other is updated as well, for instance:
 *
 * |[
 *   g_object_bind_property (object1, "property-a",
 *                           object2, "property-b",
 *                           G_BINDING_BIDIRECTIONAL);
 * ]|
 *
 * will keep the two properties in sync.
 *
 * It is also possible to set a custom transformation function (in both
 * directions, in case of a bidirectional binding) to apply a custom
 * transformation from the source value to the target value before
 * applying it; for instance, the following binding:
 *
 * |[
 *   g_object_bind_property_full (adjustment1, "value",
 *                                adjustment2, "value",
 *                                G_BINDING_BIDIRECTIONAL,
 *                                celsius_to_fahrenheit,
 *                                fahrenheit_to_celsius,
 *                                NULL, NULL);
 * ]|
 *
 * will keep the <emphasis>value</emphasis> property of the two adjustments
 * in sync; the <function>celsius_to_fahrenheit</function> function will be
 * called whenever the <emphasis>adjustment1:value</emphasis> property changes
 * and will transform the current value of the property before applying it
 * to the <emphasis>adjustment2:value</emphasis> property; vice versa, the
 * <function>fahrenheit_to_celsius</function> function will be called whenever
 * the <emphasis>adjustment2:value</emphasis> property changes, and will
 * transform the current value of the property before applying it to the
 * <emphasis>adjustment1:value</emphasis>.
 *
 * Note that #GBinding does not resolve cycles by itself; a cycle like
 *
 * |[
 *   object1:propertyA -> object2:propertyB
 *   object2:propertyB -> object3:propertyC
 *   object3:propertyC -> object1:propertyA
 * ]|
 *
 * might lead to an infinite loop. The loop, in this particular case,
 * can be avoided if the objects emit the #GObject::notify signal only
 * if the value has effectively been changed. A binding is implemented
 * using the #GObject::notify signal, so it is susceptible to all the
 * various ways of blocking a signal emission, like g_signal_stop_emission()
 * or g_signal_handler_block().
 *
 * A binding will be severed, and the resources it allocates freed, whenever
 * either one of the #GObject instances it refers to are finalized, or when
 * the #GBinding instance loses its last reference.
 *
 * #GBinding is available since GObject 2.26
 */

#include "config.h"

#include <string.h>

#include "gbinding.h"
#include "genums.h"
#include "gobject.h"
#include "gsignal.h"
#include "gparamspecs.h"
#include "gvaluetypes.h"

#include "glibintl.h"

#include "gobjectalias.h"

GType
g_binding_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { G_BINDING_DEFAULT, "G_BINDING_DEFAULT", "default" },
        { G_BINDING_BIDIRECTIONAL, "G_BINDING_BIDIRECTIONAL", "bidirectional" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_flags_register_static (g_intern_static_string ("GBindingFlags"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

#define G_BINDING_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_BINDING, GBindingClass))
#define G_IS_BINDING_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_BINDING))
#define G_BINDING_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_BINDING, GBindingClass))

typedef struct _GBindingClass           GBindingClass;

struct _GBinding
{
  GObject parent_instance;

  /* no reference is held on the objects, to avoid cycles */
  GObject *source;
  GObject *target;

  /* the property names are interned, so they should not be freed */
  gchar *source_property;
  gchar *target_property;

  GParamSpec *source_pspec;
  GParamSpec *target_pspec;

  GBindingTransformFunc transform_s2t;
  GBindingTransformFunc transform_t2s;

  GBindingFlags flags;

  guint source_notify;
  guint target_notify;

  gpointer transform_data;
  GDestroyNotify notify;

  /* a guard, to avoid loops */
  guint is_frozen : 1;
};

struct _GBindingClass
{
  GObjectClass parent_class;
};

enum
{
  PROP_0,

  PROP_SOURCE,
  PROP_TARGET,
  PROP_SOURCE_PROPERTY,
  PROP_TARGET_PROPERTY,
  PROP_FLAGS
};

static GQuark quark_gbinding = 0;

G_DEFINE_TYPE (GBinding, g_binding, G_TYPE_OBJECT);

static inline void
add_binding_qdata (GObject  *gobject,
                   GBinding *binding)
{
  GList *bindings;

  bindings = g_object_get_qdata (gobject, quark_gbinding);
  if (bindings == NULL)
    {
      bindings = g_list_prepend (NULL, binding);
      g_object_set_qdata (gobject, quark_gbinding, bindings);
    }
  else
    bindings = g_list_prepend (bindings, binding);
}

static inline void
remove_binding_qdata (GObject  *gobject,
                      GBinding *binding)
{
  GList *bindings;

  bindings = g_object_get_qdata (gobject, quark_gbinding);
  bindings = g_list_remove (bindings, binding);
}

static void
weak_unbind (gpointer  user_data,
             GObject  *where_the_object_was)
{
  GBinding *binding = user_data;

  if (binding->source == where_the_object_was)
    binding->source = NULL;
  else
    {
      if (binding->source_notify != 0)
        g_signal_handler_disconnect (binding->source, binding->source_notify);

      g_object_weak_unref (binding->source, weak_unbind, user_data);
      remove_binding_qdata (binding->source, binding);
      binding->source = NULL;
    }

  if (binding->target == where_the_object_was)
    binding->target = NULL;
  else
    {
      if (binding->target_notify != 0)
        g_signal_handler_disconnect (binding->target, binding->target_notify);

      g_object_weak_unref (binding->target, weak_unbind, user_data);
      remove_binding_qdata (binding->target, binding);
      binding->target = NULL;
    }

  g_object_unref (binding);
}

static inline gboolean
default_transform (const GValue *value_a,
                   GValue       *value_b)
{
  /* if it's not the same type, try to convert it using the GValue
   * transformation API; otherwise just copy it
   */
  if (!g_type_is_a (G_VALUE_TYPE (value_a), G_VALUE_TYPE (value_b)))
    {
      /* are these two types compatible (can be directly copied)? */
      if (g_value_type_compatible (G_VALUE_TYPE (value_a),
                                   G_VALUE_TYPE (value_b)))
        {
          g_value_copy (value_a, value_b);
          goto done;
        }

      if (g_value_type_transformable (G_VALUE_TYPE (value_a),
                                      G_VALUE_TYPE (value_b)))
        {
          if (g_value_transform (value_a, value_b))
            goto done;

          g_warning ("%s: Unable to convert a value of type %s to a "
                     "value of type %s",
                     G_STRLOC,
                     g_type_name (G_VALUE_TYPE (value_a)),
                     g_type_name (G_VALUE_TYPE (value_b)));

          return FALSE;
        }
    }
  else
    g_value_copy (value_a, value_b);

done:
  return TRUE;
}

static gboolean
default_transform_to (GBinding     *binding G_GNUC_UNUSED,
                      const GValue *value_a,
                      GValue       *value_b,
                      gpointer      user_data G_GNUC_UNUSED)
{
  return default_transform (value_a, value_b);
}

static gboolean
default_transform_from (GBinding     *binding G_GNUC_UNUSED,
                        const GValue *value_a,
                        GValue       *value_b,
                        gpointer      user_data G_GNUC_UNUSED)
{
  return default_transform (value_a, value_b);
}

static void
on_source_notify (GObject    *gobject,
                  GParamSpec *pspec,
                  GBinding   *binding)
{
  const gchar *p_name;
  GValue source_value = { 0, };
  GValue target_value = { 0, };
  gboolean res;

  if (binding->is_frozen)
    return;

  if (pspec->flags & G_PARAM_STATIC_NAME)
    p_name = g_intern_static_string (pspec->name);
  else
    p_name = g_intern_string (pspec->name);

  if (p_name != binding->source_property)
    return;

  g_value_init (&source_value, G_PARAM_SPEC_VALUE_TYPE (binding->source_pspec));
  g_value_init (&target_value, G_PARAM_SPEC_VALUE_TYPE (binding->target_pspec));

  g_object_get_property (binding->source, binding->source_pspec->name, &source_value);

  res = binding->transform_s2t (binding,
                                &source_value,
                                &target_value,
                                binding->transform_data);
  if (res)
    {
      binding->is_frozen = TRUE;

      g_param_value_validate (binding->target_pspec, &target_value);
      g_object_set_property (binding->target, binding->target_pspec->name, &target_value);

      binding->is_frozen = FALSE;
    }

  g_value_unset (&source_value);
  g_value_unset (&target_value);
}

static void
on_target_notify (GObject    *gobject,
                  GParamSpec *pspec,
                  GBinding   *binding)
{
  const gchar *p_name;
  GValue source_value = { 0, };
  GValue target_value = { 0, };
  gboolean res;

  if (binding->is_frozen)
    return;

  if (pspec->flags & G_PARAM_STATIC_NAME)
    p_name = g_intern_static_string (pspec->name);
  else
    p_name = g_intern_string (pspec->name);

  if (p_name != binding->target_property)
    return;

  g_value_init (&source_value, G_PARAM_SPEC_VALUE_TYPE (binding->target_pspec));
  g_value_init (&target_value, G_PARAM_SPEC_VALUE_TYPE (binding->source_pspec));

  g_object_get_property (binding->target, binding->target_pspec->name, &source_value);

  res = binding->transform_t2s (binding,
                                &source_value,
                                &target_value,
                                binding->transform_data);
  if (res)
    {
      binding->is_frozen = TRUE;

      g_param_value_validate (binding->source_pspec, &target_value);
      g_object_set_property (binding->source, binding->source_pspec->name, &target_value);

      binding->is_frozen = FALSE;
    }

  g_value_unset (&source_value);
  g_value_unset (&target_value);
}

static void
g_binding_finalize (GObject *gobject)
{
  GBinding *binding = G_BINDING (gobject);

  if (binding->notify != NULL)
    {
      binding->notify (binding->transform_data);

      binding->transform_data = NULL;
      binding->notify = NULL;
    }

  if (binding->source != NULL)
    {
      if (binding->source_notify != 0)
        g_signal_handler_disconnect (binding->source, binding->source_notify);

      g_object_weak_unref (binding->source, weak_unbind, binding);
      remove_binding_qdata (binding->source, binding);
    }

  if (binding->target != NULL)
    {
      if (binding->target_notify != 0)
        g_signal_handler_disconnect (binding->target, binding->target_notify);

      g_object_weak_unref (binding->target, weak_unbind, binding);
      remove_binding_qdata (binding->target, binding);
    }

  G_OBJECT_CLASS (g_binding_parent_class)->finalize (gobject);
}

static void
g_binding_set_property (GObject      *gobject,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  GBinding *binding = G_BINDING (gobject);

  switch (prop_id)
    {
    case PROP_SOURCE:
      binding->source = g_value_get_object (value);
      break;

    case PROP_SOURCE_PROPERTY:
      binding->source_property = g_intern_string (g_value_get_string (value));
      break;

    case PROP_TARGET:
      binding->target = g_value_get_object (value);
      break;

    case PROP_TARGET_PROPERTY:
      binding->target_property = g_intern_string (g_value_get_string (value));
      break;

    case PROP_FLAGS:
      binding->flags = g_value_get_flags (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
g_binding_get_property (GObject    *gobject,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  GBinding *binding = G_BINDING (gobject);

  switch (prop_id)
    {
    case PROP_SOURCE:
      g_value_set_object (value, binding->source);
      break;

    case PROP_SOURCE_PROPERTY:
      g_value_set_string (value, binding->source_property);
      break;

    case PROP_TARGET:
      g_value_set_object (value, binding->target);
      break;

    case PROP_TARGET_PROPERTY:
      g_value_set_string (value, binding->target_property);
      break;

    case PROP_FLAGS:
      g_value_set_flags (value, binding->flags);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
g_binding_constructed (GObject *gobject)
{
  GBinding *binding = G_BINDING (gobject);

  /* assert that we were constructed correctly */
  g_assert (binding->source != NULL);
  g_assert (binding->target != NULL);
  g_assert (binding->source_property != NULL);
  g_assert (binding->target_property != NULL);

  /* we assume a check was performed prior to construction - since
   * g_object_bind_property_full() does it; we cannot fail construction
   * anyway, so it would be hard for use to properly warn here
   */
  binding->source_pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (binding->source), binding->source_property);
  binding->target_pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (binding->target), binding->target_property);
  g_assert (binding->source_pspec != NULL);
  g_assert (binding->target_pspec != NULL);

  /* set the default transformation functions here */
  binding->transform_s2t = default_transform_to;
  binding->transform_t2s = default_transform_from;

  binding->transform_data = NULL;
  binding->notify = NULL;

  binding->source_notify = g_signal_connect (binding->source, "notify",
                                             G_CALLBACK (on_source_notify),
                                             binding);

  g_object_weak_ref (binding->source, weak_unbind, binding);
  add_binding_qdata (binding->source, binding);

  if (binding->flags & G_BINDING_BIDIRECTIONAL)
    binding->target_notify = g_signal_connect (binding->target, "notify",
                                               G_CALLBACK (on_target_notify),
                                               binding);

  g_object_weak_ref (binding->target, weak_unbind, binding);
  add_binding_qdata (binding->target, binding);

}

static void
g_binding_class_init (GBindingClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  quark_gbinding = g_quark_from_static_string ("g-binding");

  gobject_class->constructed = g_binding_constructed;
  gobject_class->set_property = g_binding_set_property;
  gobject_class->get_property = g_binding_get_property;
  gobject_class->finalize = g_binding_finalize;

  /**
   * GBinding:source:
   *
   * The #GObject that should be used as the source of the binding
   *
   * Since: 2.26
   */
  g_object_class_install_property (gobject_class, PROP_SOURCE,
                                   g_param_spec_object ("source",
                                                        P_("Source"),
                                                        P_("The source of the binding"),
                                                        G_TYPE_OBJECT,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * GBinding:target:
   *
   * The #GObject that should be used as the target of the binding
   *
   * Since: 2.26
   */
  g_object_class_install_property (gobject_class, PROP_TARGET,
                                   g_param_spec_object ("target",
                                                        P_("Target"),
                                                        P_("The target of the binding"),
                                                        G_TYPE_OBJECT,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * GBinding:source-property:
   *
   * The name of the property of #GBinding:source that should be used
   * as the source of the binding
   *
   * Since: 2.26
   */
  g_object_class_install_property (gobject_class, PROP_SOURCE_PROPERTY,
                                   g_param_spec_string ("source-property",
                                                        P_("Source Property"),
                                                        P_("The property on the source to bind"),
                                                        NULL,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * GBinding:target-property:
   *
   * The name of the property of #GBinding:target that should be used
   * as the target of the binding
   *
   * Since: 2.26
   */
  g_object_class_install_property (gobject_class, PROP_TARGET_PROPERTY,
                                   g_param_spec_string ("target-property",
                                                        P_("Target Property"),
                                                        P_("The property on the target to bind"),
                                                        NULL,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * GBinding:flags:
   *
   * Flags to be used to control the #GBinding
   *
   * Since: 2.26
   */
  g_object_class_install_property (gobject_class, PROP_FLAGS,
                                   g_param_spec_flags ("flags",
                                                       P_("Flags"),
                                                       P_("The binding flags"),
                                                       G_TYPE_BINDING_FLAGS,
                                                       G_BINDING_DEFAULT,
                                                       G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));
}

static void
g_binding_init (GBinding *binding)
{
}

/**
 * g_binding_get_flags:
 * @binding: a #GBinding
 *
 * Retrieves the flags passed when constructing the #GBinding
 *
 * Return value: the #GBindingFlags used by the #GBinding
 *
 * Since: 2.26
 */
GBindingFlags
g_binding_get_flags (GBinding *binding)
{
  g_return_val_if_fail (G_IS_BINDING (binding), G_BINDING_DEFAULT);

  return binding->flags;
}

/**
 * g_binding_get_source:
 * @binding: a #GBinding
 *
 * Retrieves the #GObject instance used as the source of the binding
 *
 * Return value: (transfer none): the source #GObject
 *
 * Since: 2.26
 */
GObject *
g_binding_get_source (GBinding *binding)
{
  g_return_val_if_fail (G_IS_BINDING (binding), NULL);

  return binding->source;
}

/**
 * g_binding_get_target:
 * @binding: a #GBinding
 *
 * Retrieves the #GObject instance used as the target of the binding
 *
 * Return value: (transfer none): the target #GObject
 *
 * Since: 2.26
 */
GObject *
g_binding_get_target (GBinding *binding)
{
  g_return_val_if_fail (G_IS_BINDING (binding), NULL);

  return binding->target;
}

/**
 * g_binding_get_source_property:
 * @binding: a #GBinding
 *
 * Retrieves the name of the property of #GBinding:source used as the source
 * of the binding
 *
 * Return value: the name of the source property
 *
 * Since: 2.26
 */
G_CONST_RETURN gchar *
g_binding_get_source_property (GBinding *binding)
{
  g_return_val_if_fail (G_IS_BINDING (binding), NULL);

  return binding->source_property;
}

/**
 * g_binding_get_target_property:
 * @binding: a #GBinding
 *
 * Retrieves the name of the property of #GBinding:target used as the target
 * of the binding
 *
 * Return value: the name of the target property
 *
 * Since: 2.26
 */
G_CONST_RETURN gchar *
g_binding_get_target_property (GBinding *binding)
{
  g_return_val_if_fail (G_IS_BINDING (binding), NULL);

  return binding->target_property;
}

/**
 * g_object_bind_property_full:
 * @source: the source #GObject
 * @source_property: the property on @source to bind
 * @target: the target #GObject
 * @target_property: the property on @target to bind
 * @flags: flags to pass to #GBinding
 * @transform_to: (scope notified) (allow-none): the transformation function
 *   from the @source to the @target, or %NULL to use the default
 * @transform_from: (scope notified) (allow-none): the transformation function
 *   from the @target to the @source, or %NULL to use the default
 * @user_data: custom data to be passed to the transformation functions,
 *   or %NULL
 * @notify: function to be called when disposing the binding, to free the
 *   resources used by the transformation functions
 *
 * Complete version of g_object_bind_property().
 *
 * Creates a binding between @source_property on @source and @target_property
 * on @target, allowing you to set the transformation functions to be used by
 * the binding.
 *
 * If @flags contains %G_BINDING_BIDIRECTIONAL then the binding will be mutual:
 * if @target_property on @target changes then the @source_property on @source
 * will be updated as well. The @transform_from function is only used in case
 * of bidirectional bindings, otherwise it will be ignored
 *
 * The binding will automatically be removed when either the @source or the
 * @target instances are finalized. To remove the binding without affecting the
 * @source and the @target you can just call g_object_unref() on the returned
 * #GBinding instance.
 *
 * A #GObject can have multiple bindings.
 *
 * Return value: (transfer none): the #GBinding instance representing the
 *   binding between the two #GObject instances. The binding is released
 *   whenever the #GBinding reference count reaches zero.
 *
 * Since: 2.26
 */
GBinding *
g_object_bind_property_full (gpointer               source,
                             const gchar           *source_property,
                             gpointer               target,
                             const gchar           *target_property,
                             GBindingFlags          flags,
                             GBindingTransformFunc  transform_to,
                             GBindingTransformFunc  transform_from,
                             gpointer               user_data,
                             GDestroyNotify         notify)
{
  GParamSpec *pspec;
  GBinding *binding;

  g_return_val_if_fail (G_IS_OBJECT (source), NULL);
  g_return_val_if_fail (source_property != NULL, NULL);
  g_return_val_if_fail (G_IS_OBJECT (target), NULL);
  g_return_val_if_fail (target_property != NULL, NULL);

  if (source == target && g_strcmp0 (source_property, target_property) == 0)
    {
      g_warning ("Unable to bind the same property on the same instance");
      return NULL;
    }

  if (transform_to == NULL)
    transform_to = default_transform_to;

  if (transform_from == NULL)
    transform_from = default_transform_from;

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (source), source_property);
  if (pspec == NULL)
    {
      g_warning ("%s: The source object of type %s has no property called '%s'",
                 G_STRLOC,
                 G_OBJECT_TYPE_NAME (source),
                 source_property);
      return NULL;
    }

  if (!(pspec->flags & G_PARAM_READABLE))
    {
      g_warning ("%s: The source object of type %s has no readable property called '%s'",
                 G_STRLOC,
                 G_OBJECT_TYPE_NAME (source),
                 source_property);
      return NULL;
    }

  if ((flags & G_BINDING_BIDIRECTIONAL) &&
      ((pspec->flags & G_PARAM_CONSTRUCT_ONLY) || !(pspec->flags & G_PARAM_WRITABLE)))
    {
      g_warning ("%s: The source object of type %s has no writable property called '%s'",
                 G_STRLOC,
                 G_OBJECT_TYPE_NAME (source),
                 source_property);
      return NULL;
    }

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (target), target_property);
  if (pspec == NULL)
    {
      g_warning ("%s: The target object of type %s has no property called '%s'",
                 G_STRLOC,
                 G_OBJECT_TYPE_NAME (target),
                 target_property);
      return NULL;
    }

  if ((pspec->flags & G_PARAM_CONSTRUCT_ONLY) || !(pspec->flags & G_PARAM_WRITABLE))
    {
      g_warning ("%s: The target object of type %s has no writable property called '%s'",
                 G_STRLOC,
                 G_OBJECT_TYPE_NAME (target),
                 target_property);
      return NULL;
    }

  if ((flags & G_BINDING_BIDIRECTIONAL) &&
      !(pspec->flags & G_PARAM_READABLE))
    {
      g_warning ("%s: The starget object of type %s has no writable property called '%s'",
                 G_STRLOC,
                 G_OBJECT_TYPE_NAME (target),
                 target_property);
      return NULL;
    }

  binding = g_object_new (G_TYPE_BINDING,
                          "source", source,
                          "source-property", source_property,
                          "target", target,
                          "target-property", target_property,
                          "flags", flags,
                          NULL);

  /* making these properties would be awkward, though not impossible */
  binding->transform_s2t = transform_to;
  binding->transform_t2s = transform_from;
  binding->transform_data = user_data;
  binding->notify = notify;

  return binding;
}

/**
 * g_object_bind_property:
 * @source: the source #GObject
 * @source_property: the property on @source to bind
 * @target: the target #GObject
 * @target_property: the property on @target to bind
 * @flags: flags to pass to #GBinding
 *
 * Creates a binding between @source_property on @source and @target_property
 * on @target. Whenever the @source_property is changed the @target_property is
 * updated using the same value. For instance:
 *
 * |[
 *   g_object_bind_property (action, "active", widget, "sensitive", 0);
 * ]|
 *
 * Will result in the "sensitive" property of the widget #GObject instance to be
 * updated with the same value of the "active" property of the action #GObject
 * instance.
 *
 * If @flags contains %G_BINDING_BIDIRECTIONAL then the binding will be mutual:
 * if @target_property on @target changes then the @source_property on @source
 * will be updated as well.
 *
 * The binding will automatically be removed when either the @source or the
 * @target instances are finalized. To remove the binding without affecting the
 * @source and the @target you can just call g_object_unref() on the returned
 * #GBinding instance.
 *
 * A #GObject can have multiple bindings.
 *
 * Return value: (transfer none): the #GBinding instance representing the
 *   binding between the two #GObject instances. The binding is released
 *   whenever the #GBinding reference count reaches zero.
 *
 * Since: 2.26
 */
GBinding *
g_object_bind_property (gpointer       source,
                        const gchar   *source_property,
                        gpointer       target,
                        const gchar   *target_property,
                        GBindingFlags  flags)
{
  /* type checking is done in g_object_bind_property_full() */

  return g_object_bind_property_full (source, source_property,
                                      target, target_property,
                                      flags,
                                      NULL,
                                      NULL,
                                      NULL, NULL);
}

#define __G_BINDING_C__
#include "gobjectaliasdef.c"
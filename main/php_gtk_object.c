/*
 * PHP-GTK - The PHP language bindings for GTK+
 *
 * Copyright (C) 2001,2002 Andrei Zmievski <andrei@php.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* $Id$ */


#include "php_gtk.h"

#if HAVE_PHP_GTK

#include "ext/gtk+/php_gtk+.h"

typedef struct {
	zend_bool have_getter : 1;
	zend_bool have_setter : 1;
	char **prop_names;
} prop_desc_t;

static const char *php_gtk_wrapper_key  = "php_gtk::wrapper";

HashTable php_gtk_prop_getters;
HashTable php_gtk_prop_setters;
HashTable php_gtk_rsrc_hash; 
HashTable php_gtk_type_hash;
HashTable php_gtk_prop_desc;
HashTable php_gtk_callback_hash;


PHP_GTK_API zend_object_handlers *php_gtk_handlers;

static inline void php_gtk_destroy_object(php_gtk_object *object, zend_object_handle handle TSRMLS_DC)
{
    //FIXME zend_objects_call_destructor(object, handle TSRMLS_CC);
    /* Nuke the object */
    zend_hash_destroy(object->zobj.properties);
    FREE_HASHTABLE(object->zobj.properties);
	if (object->obj && object->dtor)
		object->dtor(object->obj);
    efree(object);
}


/* Generic get/set property handlers. */
static inline zval* invoke_getter(zval *object, char *property)
{
	zend_class_entry *ce;
	prop_getter_t *getter;
	zval *result = NULL;
	int found = FAILURE;
	TSRMLS_FETCH();
	ALLOC_ZVAL(result);

	for (ce = Z_OBJCE_P(object); ce != NULL && found != SUCCESS; ce = ce->parent) {

		if (zend_hash_index_find(&php_gtk_prop_getters, (long)ce, (void **)&getter) == SUCCESS) {
			(*getter)(result, object, property, &found);
		}
	}


	if (found == SUCCESS) {
		return result;
		 
	}
	MAKE_STD_ZVAL(result);
	ZVAL_NULL(result);
	return result;
}

static inline int invoke_setter(zval *object, char *property, zval *value)
{
	zend_class_entry *ce;
	prop_setter_t *setter;
	int found = FAILURE;
	TSRMLS_FETCH();

	for (ce = Z_OBJCE_P(object); ce != NULL && found != SUCCESS; ce = ce->parent) {
		if (zend_hash_index_find(&php_gtk_prop_setters, (long)ce, (void **)&setter) == SUCCESS) {
			found = (*setter)(object, property, value);
		}
	}

	return found;
}
/* 
this is not used - (the replacement php_g_object_get_properties is in 
ext/gtk/php_gtk+_types.c
*/
static HashTable* php_gtk_get_properties(zval *object TSRMLS_DC)
{
	prop_desc_t *prop_desc;
	prop_getter_t *getter;
	zend_class_entry *ce;
	zval *prop;
	php_gtk_object *wrapper;
	char **ptr;
	int found;
	
	printf("get properties called: \n");
	
	wrapper = (php_gtk_object *) zend_object_store_get_object(object TSRMLS_CC);
	for (ce = Z_OBJCE_P(object); ce != NULL; ce = ce->parent) {
		zend_hash_index_find(&php_gtk_prop_desc, (long)ce, (void **)&prop_desc);
		if (prop_desc->prop_names &&
			zend_hash_index_find(&php_gtk_prop_getters, (long)ce, (void **)&getter) == SUCCESS) {
			for (ptr = prop_desc->prop_names; *ptr != NULL; ptr++) {
				MAKE_STD_ZVAL(prop);
				ZVAL_NULL(prop);
				(*getter)(prop, object, *ptr, &found);
				zval_add_ref(&prop); /* ugly hack until prop getters are fixed */
				zend_hash_update(wrapper->zobj.properties, *ptr, strlen(*ptr)+1, &prop, sizeof(zval *), NULL);
			}
		}
	}

	return wrapper->zobj.properties;
}

static zval **php_gtk_get_property_ptr_ptr(zval *object, zval *member TSRMLS_DC)
{
	return zend_object_create_proxy(object, member TSRMLS_CC);
}

static void php_gtk_write_property(zval *object, zval *member, zval *value TSRMLS_DC)
{
	php_gtk_object *wrapper;
	zval tmp_member;

 	if (member->type != IS_STRING) {
		tmp_member = *member;
		zval_copy_ctor(&tmp_member);
		convert_to_string(&tmp_member);
		member = &tmp_member;
	}

	wrapper = (php_gtk_object *) zend_object_store_get_object(object TSRMLS_CC);
	if (invoke_setter(object, Z_STRVAL_P(member), value) == FAILURE) {
		std_object_handlers.write_property(object, member, value TSRMLS_CC);
	}

	if (member == &tmp_member) {
		zval_dtor(member);
	}
}

static zval *php_gtk_read_property(zval *object, zval *member, zend_bool silent TSRMLS_DC)
{
	php_gtk_object *wrapper;
	zval tmp_member;
	zval *rv = NULL;

 	if (member->type != IS_STRING) {
		tmp_member = *member;
		zval_copy_ctor(&tmp_member);
		convert_to_string(&tmp_member);
		member = &tmp_member;
	}
	wrapper = (php_gtk_object *) zend_object_store_get_object(object TSRMLS_CC);
	rv = invoke_getter(object, Z_STRVAL_P(member));
	if (!rv) {
		/* printf("could not find object member %s: \n",Z_STRVAL_P(member)); */
		rv = std_object_handlers.read_property(object, member,  silent TSRMLS_CC);
	}
	
	if (member == &tmp_member) {
		zval_dtor(member);
	}

	return rv;
}

static zend_object_value create_php_gtk_object(zend_class_entry *ce TSRMLS_DC)
{
	zend_object_value zov;
	php_gtk_object *object;
	prop_desc_t *prop_desc;

	zov.handlers = php_gtk_handlers;

	if (zend_hash_index_find(&php_gtk_prop_desc, (long)ce, (void **)&prop_desc) == SUCCESS) {
		if (prop_desc->have_getter) {
			zov.handlers->read_property        = php_gtk_read_property;
			zov.handlers->get_properties       = php_gtk_get_properties;
			zov.handlers->get_property_ptr_ptr = php_gtk_get_property_ptr_ptr;
		}
		if (prop_desc->have_setter) {
			zov.handlers->write_property = php_gtk_write_property;
		}
	}

	object = emalloc(sizeof(php_gtk_object));
	ALLOC_HASHTABLE(object->zobj.properties);
	zend_hash_init(object->zobj.properties, 0, NULL, ZVAL_PTR_DTOR, 0);
	object->zobj.ce = ce;
	object->zobj.in_get = 0;
	object->zobj.in_set = 0;
	object->obj  = NULL;
	object->dtor = NULL;
	zov.handle = zend_objects_store_put(object, (zend_objects_store_dtor_t) php_gtk_destroy_object, NULL TSRMLS_CC);

	return zov;
}

PHP_GTK_API zend_class_entry* php_gtk_register_class(const char *class_name, function_entry *class_functions, zend_class_entry *parent, zend_bool have_getter, zend_bool have_setter, char **class_props TSRMLS_DC)
{
	zend_class_entry ce, *real_ce;
	prop_desc_t prop_desc = { have_getter, have_setter, class_props },
				*parent_prop_desc;

	memset(&ce, 0, sizeof(ce));

	ce.name = (char *)class_name;
	ce.name_length = strlen(class_name);
	ce.builtin_functions = class_functions;
	ce.create_object = create_php_gtk_object;
	
	real_ce = zend_register_internal_class_ex(&ce, parent, NULL TSRMLS_CC);
	/*printf("Registered Class %s as %i (parent = %i)\n",class_name, real_ce, parent); */
	if (zend_hash_index_find(&php_gtk_prop_desc, (long)real_ce->parent, (void **)&parent_prop_desc) == SUCCESS) {
		if (parent_prop_desc->have_getter)
			prop_desc.have_getter = 1;
		if (parent_prop_desc->have_setter)
			prop_desc.have_setter = 1;
	}
	zend_hash_index_update(&php_gtk_prop_desc, (long)real_ce, (void *)&prop_desc, sizeof(prop_desc_t), NULL);

	return real_ce;
}

PHP_GTK_API void php_gtk_object_init(GtkObject *obj, zval *zobj)
{
	gtk_object_ref(obj);
	gtk_object_sink(obj);
	// defined in php_gtk.h
	php_gtk_set_object(zobj, obj, (php_gtk_dtor_t)gtk_object_unref, 0);
}

    /**
    * Get an GObject from a zval
    * 
    * @param   zval*    pointer to zval (with object in it)
    * @param   int type of object (the ce?)
    * 
    *
    * @return   void* pointer to object;
    */




PHP_GTK_API void *php_gtk_get_object(zval *zobj, int rsrc_type)
{
	//void *obj;
	//int type;
	php_gtk_object *wrapper;
	TSRMLS_FETCH();
	
	if (Z_TYPE_P(zobj) != IS_OBJECT) {
		php_error(E_ERROR, "Wrapper is not an object");
	}

	wrapper = zend_object_store_get_object(zobj TSRMLS_CC);
	if (!wrapper->obj) {
		php_error(E_ERROR, "Underlying object missing");
	}

	/*
	if (zend_hash_index_find(Z_OBJPROP_P(zobj), 0, (void **)&handle) == FAILURE) {
		php_error(E_ERROR, "Underlying object missing");
	}
	obj = zend_list_find(Z_LVAL_PP(handle), &type);
	if (!obj || type != rsrc_type) {
		php_error(E_ERROR, "Underlying object missing or of invalid type");
	}
	*/

	return wrapper->obj;
}

int php_gtk_get_simple_enum_value(zval *enum_val, int *result)
{
	if (!enum_val)
		return 0;

	if (Z_TYPE_P(enum_val) == IS_LONG) {
		*result = Z_LVAL_P(enum_val);
		return 1;
	}

	php_error(E_WARNING, "simple enum values must be integers");
	return 0;
}

PHP_GTK_API int php_gtk_get_enum_value(GtkType enum_type, zval *enum_val, int *result)
{
	if (!enum_val)
		return 0;

	if (Z_TYPE_P(enum_val) == IS_LONG) {
		*result = Z_LVAL_P(enum_val);
		return 1;
	} else if (Z_TYPE_P(enum_val) == IS_STRING) {
		GtkEnumValue *info = gtk_type_enum_find_value(enum_type, Z_STRVAL_P(enum_val));
		if (!info) {
			php_error(E_WARNING, "Could not translate enum value '%s'", Z_STRVAL_P(enum_val));
			return 0;
		}
		*result = info->value;
		return 1;
	}

	php_error(E_WARNING, "enum values must be integers or strings");
	return 0;
}

int php_gtk_get_flag_value(GtkType flag_type, zval *flag_val, int *result)
{
	if (!flag_val)
		return 0;

	if (Z_TYPE_P(flag_val) == IS_LONG) {
		*result = Z_LVAL_P(flag_val);
		return 1;
	} else if (Z_TYPE_P(flag_val) == IS_STRING) {
		GtkFlagValue *info = gtk_type_flags_find_value(flag_type, Z_STRVAL_P(flag_val));
		if (!info) {
			php_error(E_WARNING, "Could not translate flag value '%s'", Z_STRVAL_P(flag_val));
			return 0;
		}
		*result = info->value;
		return 1;
	} else if (Z_TYPE_P(flag_val) == IS_ARRAY) {
		zval **flag;

		for (zend_hash_internal_pointer_reset(Z_ARRVAL_P(flag_val));
			 zend_hash_get_current_data(Z_ARRVAL_P(flag_val), (void **)&flag) == SUCCESS;
			 zend_hash_move_forward(Z_ARRVAL_P(flag_val))) {
			if (Z_TYPE_PP(flag) == IS_LONG)
				*result |= Z_LVAL_PP(flag);
			else if (Z_TYPE_PP(flag) == IS_STRING) {
				GtkFlagValue *info = gtk_type_flags_find_value(flag_type, Z_STRVAL_PP(flag));
				if (!info) {
					php_error(E_WARNING, "Could not translate flag value '%s'", Z_STRVAL_PP(flag));
					return 0;
				}
				*result |= info->value;
			} else {
				php_error(E_WARNING, "flag arrays can contain only integers or strings");
				return 0;
			}
		}

		return 1;
	}

	php_error(E_WARNING, "flag values must be integers or strings");
	return 0;
}
  
PHP_GTK_API void php_gtk_destroy_notify(gpointer user_data)
{
	zval *value = (zval *)user_data;
	zval_ptr_dtor(&value);
}

PHP_GTK_API zval *php_gtk_new_base_gobject(GObject *obj)
{
	zval *zobj;
	php_gtk_object *wrapper = NULL;
	zend_class_entry *ce;
	GType type;
	TSRMLS_FETCH();
	
	if (!obj) {
		MAKE_STD_ZVAL(zobj);
		ZVAL_NULL(zobj);
		return zobj;
	}

        if ((zobj = (zval *) g_object_get_data(obj, php_gtk_wrapper_key))) {
		zval_add_ref(&zobj);
		return zobj;
	}

	type = G_OBJECT_TYPE(obj);
	while ((ce = g_hash_table_lookup(php_gtk_class_hash, g_type_name(type))) == NULL)
		type = g_type_parent(type);

	MAKE_STD_ZVAL(zobj);
	object_init_ex(zobj, ce);
	g_object_ref(obj);
	wrapper = zend_object_store_get_object(zobj TSRMLS_CC);
	wrapper->obj = obj;
	wrapper->dtor = (php_gtk_dtor_t)gtk_object_unref;
	zend_objects_store_add_ref(zobj TSRMLS_CC);
	g_object_set_data(obj, php_gtk_wrapper_key, zobj);
        zval_add_ref(&zobj);
	return zobj;
}

PHP_GTK_API zval *php_gtk_new(GtkObject *obj)
{
	return (zval *)php_gtk_new_base_gobject(G_OBJECT(obj));

}

zval *php_gtk_args_as_hash(int nargs, const GValue *args)
{
	zval *hash;
	zval *item;
	int i;

	MAKE_STD_ZVAL(hash);
	array_init(hash);
	for (i = 0; i < nargs; i++) {
		item = php_gtk_arg_as_value(&args[i]);
		if (!item) {
			MAKE_STD_ZVAL(item);
			ZVAL_NULL(item);
		}
		zend_hash_next_index_insert(Z_ARRVAL_P(hash), &item, sizeof(zval *), NULL);
	}

	return hash;
}
#ifdef DISABLED
/*

*/
GtkArg *php_gtk_hash_as_args(zval *hash, GtkType type, gint *nargs)
{
	int i;
	zval **item;
	gchar *err, buf[255];
	ulong num_key;
	GtkArg *arg = NULL;
	GtkArgInfo *info;
	HashTable *ht = HASH_OF(hash);

	gtk_type_class(type);
	*nargs = zend_hash_num_elements(ht);
	arg = g_new(GtkArg, *nargs);

	for (zend_hash_internal_pointer_reset(ht), i = 0;
		 zend_hash_get_current_data(ht, (void **)&item) == SUCCESS;
		 zend_hash_move_forward(ht), i++) {
		if (zend_hash_get_current_key(ht, &arg[i].name, &num_key, 0) != HASH_KEY_IS_STRING) {
			php_error(E_WARNING, "array keys must be strings");
			g_free(arg);
			return NULL;
		}

		err = gtk_object_arg_get_info(type, arg[i].name, &info);
		if (!info) {
			php_error(E_WARNING, err);
			g_free(err);
			g_free(arg);
			return NULL;
		}

		arg[i].type = info->type;
		arg[i].name = info->name;

		if (!php_gtk_arg_from_value(&arg[i], *item)) {
			g_snprintf(buf, 255, "argument '%s': expected type %s, found %s",
					   arg[i].name, gtk_type_name(arg[i].type),
					   php_gtk_zval_type_name(*item));
			php_error(E_WARNING, buf);
			g_free(arg);
			return NULL;
		}
	}

	return arg;
}

int php_gtk_args_from_hash(GtkArg *args, int nparams, zval *hash)
{
	zval **item;
	HashTable *ht = HASH_OF(hash);
	int i;

	for (zend_hash_internal_pointer_reset(ht), i = 0;
		 i < nparams && zend_hash_get_current_data(ht, (void **)&item) == SUCCESS;
		 zend_hash_move_forward(ht), i++) {
		if (!php_gtk_arg_from_value(&args[i], *item)) {
			gchar buf[512];

			if (args[i].name == NULL)
				g_snprintf(buf, 511, "argument %d: expected %s, %s found", i+1,
						   gtk_type_name(args[i].type),
						   php_gtk_zval_type_name(*item));
			else
				g_snprintf(buf, 511, "argument %d: expected %s, %s found", i+1,
						   gtk_type_name(args[i].type),
						   php_gtk_zval_type_name(*item));
			php_error(E_WARNING, buf);
			return 0;
		}
	}

	return 1;
}

#endif

zval *php_gtk_arg_as_value(const GValue *arg)
{
	zval *value;
	TSRMLS_FETCH();
	/* printf("got arg type %s \n",  g_type_name(G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(arg)))); */
	switch (G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(arg))) {

		case G_TYPE_INVALID:
		case G_TYPE_NONE:
			MAKE_STD_ZVAL(value);
			ZVAL_NULL(value);
			break;

		case G_TYPE_CHAR:
		case G_TYPE_UCHAR:{
 	            	gint8 val = g_value_get_char(arg);
 			MAKE_STD_ZVAL(value);
			ZVAL_STRINGL(value, (char *)&val, 1, 1);
			break;
		}
		case G_TYPE_BOOLEAN:
			MAKE_STD_ZVAL(value);
			ZVAL_BOOL(value, g_value_get_boolean(arg));
			break;

		case G_TYPE_ENUM:
		case G_TYPE_FLAGS:
		case G_TYPE_INT:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, g_value_get_int(arg));
			break;

		case G_TYPE_LONG:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, g_value_get_long(arg));
			break;

		case G_TYPE_UINT:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, g_value_get_uint(arg));
			break;

		case G_TYPE_ULONG:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, g_value_get_ulong(arg));
			break;

		case G_TYPE_FLOAT:
			MAKE_STD_ZVAL(value);
			ZVAL_DOUBLE(value, g_value_get_float(arg));
			break;

		case G_TYPE_DOUBLE:
			MAKE_STD_ZVAL(value);
			ZVAL_DOUBLE(value, g_value_get_double(arg));
			break;

		case G_TYPE_STRING: {
			const gchar *str = g_value_get_string(arg);
			MAKE_STD_ZVAL(value);
			if (str != NULL) {
				ZVAL_STRING(value, (char *)str, 1);
			} else
				ZVAL_NULL(value);
			break;
		}
/*  Does this exist? - probably PARAM..?
		case G_TYPE_ARGS:
			value = php_gtk_args_as_hash(GTK_VALUE_ARGS(arg).n_args,
										 GTK_VALUE_ARGS(arg).args);
			break;
*/
		case G_TYPE_OBJECT:
			/* just guessing the api here.. - g_value_get_pointer:?? - cast to G_OBJECT!? */
			value = php_gtk_new_base_gobject(g_value_get_object(arg));
			break;

		case G_TYPE_POINTER:
			/* if you get this error.. - a callback probably needs to use the callback registry */
			php_error(E_NOTICE, "%s(): internal error: G_TYPE_POINTER unsupported",
					  get_active_function_name(TSRMLS_C));
			MAKE_STD_ZVAL(value);
			ZVAL_NULL(value);
			break;

		case G_TYPE_BOXED:
			
			/* TODO ... this needs more investigation..
			if (G_VALUE_HOLDS(*arg, GDK_TYPE_EVENT))
				value = php_gdk_event_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GDK_TYPE_WINDOW)
				value = php_gdk_window_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GDK_TYPE_COLOR)
				value = php_gdk_color_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GDK_TYPE_COLORMAP)
				value = php_gdk_colormap_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GDK_TYPE_VISUAL)
				value = php_gdk_visual_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GDK_TYPE_FONT)
				value = php_gdk_font_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GDK_TYPE_DRAG_CONTEXT)
				value = php_gdk_drag_context_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GTK_TYPE_ACCEL_GROUP)
				value = php_gtk_accel_group_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GTK_TYPE_STYLE)
				value = php_gtk_style_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GTK_TYPE_SELECTION_DATA)
				value = php_gtk_selection_data_new(G_VALUE_BOXED(*arg));
			else if (arg->type == GTK_TYPE_CTREE_NODE)
				value = php_gtk_ctree_node_new(G_VALUE_BOXED(*arg));
			else
				return NULL;
			break;
*/

		default:
			//g_assert_not_reached();
			php_error(E_NOTICE, "internal error: Type %d unsupported in %s Line:%d",
				(int) G_VALUE_TYPE(arg), __FILE__, __LINE__	);
			return NULL;
	}

	return value;
}


#ifdef DISABLED
int php_gtk_arg_from_value(GtkArg *arg, zval *value)
{
	switch (GTK_FUNDAMENTAL_TYPE(arg->type)) {
		case GTK_TYPE_NONE:
		case GTK_TYPE_INVALID:
			GTK_VALUE_INT(*arg) = 0;
			break;

		case GTK_TYPE_BOOL:
			convert_to_boolean(value);
			GTK_VALUE_BOOL(*arg) = Z_BVAL_P(value);
			break;

		case GTK_TYPE_CHAR:
		case GTK_TYPE_UCHAR:
			convert_to_string(value);
			GTK_VALUE_CHAR(*arg) = Z_STRVAL_P(value)[0];
			break;

		case GTK_TYPE_ENUM:
			if (!php_gtk_get_enum_value(arg->type, value, &(GTK_VALUE_ENUM(*arg))))
				return 0;
			break;

		case GTK_TYPE_FLAGS:
			if (!php_gtk_get_flag_value(arg->type, value, &(GTK_VALUE_FLAGS(*arg))))
				return 0;
			break;

		case GTK_TYPE_INT:
			convert_to_long(value);
			GTK_VALUE_INT(*arg) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_UINT:
			convert_to_long(value);
			GTK_VALUE_UINT(*arg) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_LONG:
			convert_to_long(value);
			GTK_VALUE_LONG(*arg) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_ULONG:
			convert_to_long(value);
			GTK_VALUE_ULONG(*arg) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_FLOAT:
			convert_to_double(value);
			GTK_VALUE_FLOAT(*arg) = (gfloat)Z_DVAL_P(value);
			break;

		case GTK_TYPE_DOUBLE:
			convert_to_double(value);
			GTK_VALUE_DOUBLE(*arg) = Z_DVAL_P(value);
			break;

		case GTK_TYPE_STRING:
			convert_to_string(value);
			GTK_VALUE_STRING(*arg) = Z_STRVAL_P(value);
			break;

		case GTK_TYPE_OBJECT:
			if (Z_TYPE_P(value) == IS_OBJECT && php_gtk_check_class(value, gtk_object_ce))
				GTK_VALUE_OBJECT(*arg) = PHP_GTK_GET(value);
			else
				return 0;
			break;

		case GTK_TYPE_BOXED:
			if (arg->type == GTK_TYPE_GDK_EVENT) {
				if (php_gtk_check_class(value, gdk_event_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GDK_EVENT_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_GDK_WINDOW) {
				if (php_gtk_check_class(value, gdk_window_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GDK_WINDOW_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_GDK_COLOR) {
				if (php_gtk_check_class(value, gdk_color_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GDK_COLOR_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_GDK_COLORMAP) {
				if (php_gtk_check_class(value, gdk_colormap_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GDK_COLORMAP_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_GDK_VISUAL) {
				if (php_gtk_check_class(value, gdk_visual_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GDK_VISUAL_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_GDK_FONT) {
				if (php_gtk_check_class(value, gdk_font_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GDK_FONT_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_GDK_DRAG_CONTEXT) {
				if (php_gtk_check_class(value, gdk_drag_context_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GDK_DRAG_CONTEXT_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_ACCEL_GROUP) {
				if (php_gtk_check_class(value, gtk_accel_group_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GTK_ACCEL_GROUP_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_STYLE) {
				if (php_gtk_check_class(value, gtk_style_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GTK_STYLE_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_SELECTION_DATA) {
				if (php_gtk_check_class(value, gtk_selection_data_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GTK_SELECTION_DATA_GET(value);
				else
					return 0;
			} else if (arg->type == GTK_TYPE_CTREE_NODE) {
				if (php_gtk_check_class(value, gtk_ctree_node_ce))
					GTK_VALUE_BOXED(*arg) = PHP_GTK_CTREE_NODE_GET(value);
				else
					return 0;
			} else
				return 0;
			break;

		case GTK_TYPE_FOREIGN:
			zval_add_ref(&value);
			GTK_VALUE_FOREIGN(*arg).data = value;
			GTK_VALUE_FOREIGN(*arg).notify = php_gtk_destroy_notify;
			break;

		case GTK_TYPE_SIGNAL:
			if (php_gtk_is_callable(value, 1, NULL)) {
				zval_add_ref(&value);
				GTK_VALUE_SIGNAL(*arg).f = NULL;
				GTK_VALUE_SIGNAL(*arg).d = value;
			} else
				return 0;
			break;

		case GTK_TYPE_CALLBACK:
			if (php_gtk_is_callable(value, 1, NULL)) {
				zval_add_ref(&value);
				GTK_VALUE_CALLBACK(*arg).marshal = php_gtk_callback_marshal;
				GTK_VALUE_CALLBACK(*arg).data = value;
				GTK_VALUE_CALLBACK(*arg).notify = php_gtk_destroy_notify;
			} else
				return 0;
			break;

		case GTK_TYPE_ARGS:
		case GTK_TYPE_POINTER:
		case GTK_TYPE_C_CALLBACK:
			php_error(E_WARNING, "Unsupported type");
			g_assert_not_reached();
			return 0;
	}

	return 1;
}

zval *php_gtk_ret_as_value(GtkArg *ret)
{
	zval *value;
	TSRMLS_FETCH();

	switch (GTK_FUNDAMENTAL_TYPE(ret->type)) {
		case GTK_TYPE_INVALID:
		case GTK_TYPE_NONE:
			MAKE_STD_ZVAL(value);
			ZVAL_NULL(value);
			break;

		case GTK_TYPE_CHAR:
		case GTK_TYPE_UCHAR:
			MAKE_STD_ZVAL(value);
			ZVAL_STRINGL(value, GTK_RETLOC_CHAR(*ret), 1, 1);
			break;

		case GTK_TYPE_BOOL:
			MAKE_STD_ZVAL(value);
			ZVAL_BOOL(value, *GTK_RETLOC_BOOL(*ret));
			break;

		case GTK_TYPE_ENUM:
		case GTK_TYPE_FLAGS:
		case GTK_TYPE_INT:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, *GTK_RETLOC_INT(*ret));
			break;

		case GTK_TYPE_LONG:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, *GTK_RETLOC_LONG(*ret));
			break;

		case GTK_TYPE_UINT:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, *GTK_RETLOC_UINT(*ret));
			break;

		case GTK_TYPE_ULONG:
			MAKE_STD_ZVAL(value);
			ZVAL_LONG(value, *GTK_RETLOC_ULONG(*ret));
			break;

		case GTK_TYPE_FLOAT:
			MAKE_STD_ZVAL(value);
			ZVAL_DOUBLE(value, *GTK_RETLOC_FLOAT(*ret));
			break;

		case GTK_TYPE_DOUBLE:
			MAKE_STD_ZVAL(value);
			ZVAL_DOUBLE(value, *GTK_RETLOC_DOUBLE(*ret));
			break;

		case GTK_TYPE_STRING:
			MAKE_STD_ZVAL(value);
			if (*GTK_RETLOC_STRING(*ret) != NULL) {
				ZVAL_STRING(value, *GTK_RETLOC_STRING(*ret), 1);
			} else
				ZVAL_NULL(value);
			break;

		case GTK_TYPE_ARGS:
			return NULL;

		case GTK_TYPE_OBJECT:
			value = php_gtk_new(*GTK_RETLOC_OBJECT(*ret));
			break;

		case GTK_TYPE_POINTER:
			php_error(E_NOTICE, "%s(): internal error: GTK_TYPE_POINTER unsupported",
					  get_active_function_name(TSRMLS_C));
			MAKE_STD_ZVAL(value);
			ZVAL_NULL(value);
			break;

		case GTK_TYPE_BOXED:
			if (ret->type == GTK_TYPE_GDK_EVENT)
				value = php_gdk_event_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_GDK_WINDOW)
				value = php_gdk_window_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_GDK_COLOR)
				value = php_gdk_color_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_GDK_COLORMAP)
				value = php_gdk_colormap_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_GDK_VISUAL)
				value = php_gdk_visual_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_GDK_FONT)
				value = php_gdk_font_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_GDK_DRAG_CONTEXT)
				value = php_gdk_drag_context_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_ACCEL_GROUP)
				value = php_gtk_accel_group_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_STYLE)
				value = php_gtk_style_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_SELECTION_DATA)
				value = php_gtk_selection_data_new(*GTK_RETLOC_BOXED(*ret));
			else if (ret->type == GTK_TYPE_CTREE_NODE)
				value = php_gtk_ctree_node_new(*GTK_RETLOC_BOXED(*ret));
			else
				return NULL;
			break;

		default:
			g_assert_not_reached();
			return NULL;
	}

	return value;
}

void php_gtk_ret_from_value(GtkArg *ret, zval *value)
{
	switch (GTK_FUNDAMENTAL_TYPE(ret->type)) {
		case GTK_TYPE_NONE:
		case GTK_TYPE_INVALID:
			break;

		case GTK_TYPE_BOOL:
			convert_to_boolean(value);
			*GTK_RETLOC_BOOL(*ret) = Z_BVAL_P(value);
			break;

		case GTK_TYPE_CHAR:
			convert_to_string(value);
			*GTK_RETLOC_BOOL(*ret) = Z_STRVAL_P(value)[0];
			break;

		case GTK_TYPE_ENUM:
			if (!php_gtk_get_enum_value(ret->type, value, GTK_RETLOC_ENUM(*ret)))
				*GTK_RETLOC_ENUM(*ret) = 0;
			break;

		case GTK_TYPE_FLAGS:
			if (!php_gtk_get_flag_value(ret->type, value, GTK_RETLOC_FLAGS(*ret)))
				*GTK_RETLOC_FLAGS(*ret) = 0;
			break;

		case GTK_TYPE_INT:
			convert_to_long(value);
			*GTK_RETLOC_INT(*ret) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_UINT:
			convert_to_long(value);
			*GTK_RETLOC_UINT(*ret) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_LONG:
			convert_to_long(value);
			*GTK_RETLOC_LONG(*ret) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_ULONG:
			convert_to_long(value);
			*GTK_RETLOC_ULONG(*ret) = Z_LVAL_P(value);
			break;

		case GTK_TYPE_FLOAT:
			convert_to_double(value);
			*GTK_RETLOC_FLOAT(*ret) = (gfloat)Z_DVAL_P(value);
			break;

		case GTK_TYPE_DOUBLE:
			convert_to_double(value);
			*GTK_RETLOC_DOUBLE(*ret) = Z_DVAL_P(value);
			break;

		case GTK_TYPE_STRING:
			convert_to_string(value);
			*GTK_RETLOC_STRING(*ret) = g_strdup(Z_STRVAL_P(value));
			break;

		case GTK_TYPE_OBJECT:
			if (Z_TYPE_P(value) == IS_OBJECT && php_gtk_check_class(value, gtk_object_ce))
				*GTK_RETLOC_OBJECT(*ret) = PHP_GTK_GET(value);
			else
				*GTK_RETLOC_OBJECT(*ret) = NULL;
			break;

		case GTK_TYPE_BOXED:
			if (ret->type == GTK_TYPE_GDK_EVENT) {
				if (php_gtk_check_class(value, gdk_event_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GDK_EVENT_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_GDK_WINDOW) {
				if (php_gtk_check_class(value, gdk_window_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GDK_WINDOW_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_GDK_COLOR) {
				if (php_gtk_check_class(value, gdk_color_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GDK_COLOR_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_GDK_COLORMAP) {
				if (php_gtk_check_class(value, gdk_colormap_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GDK_COLORMAP_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_GDK_VISUAL) {
				if (php_gtk_check_class(value, gdk_visual_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GDK_VISUAL_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_GDK_FONT) {
				if (php_gtk_check_class(value, gdk_font_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GDK_FONT_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_GDK_DRAG_CONTEXT) {
				if (php_gtk_check_class(value, gdk_drag_context_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GDK_DRAG_CONTEXT_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_ACCEL_GROUP) {
				if (php_gtk_check_class(value, gtk_accel_group_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GTK_ACCEL_GROUP_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_STYLE) {
				if (php_gtk_check_class(value, gtk_style_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GTK_STYLE_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_SELECTION_DATA) {
				if (php_gtk_check_class(value, gtk_selection_data_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GTK_SELECTION_DATA_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else if (ret->type == GTK_TYPE_CTREE_NODE) {
				if (php_gtk_check_class(value, gtk_ctree_node_ce))
					*GTK_RETLOC_BOXED(*ret) = PHP_GTK_CTREE_NODE_GET(value);
				else
					*GTK_RETLOC_BOXED(*ret) = NULL;
			} else
				*GTK_RETLOC_BOXED(*ret) = NULL;
			break;

		case GTK_TYPE_POINTER:
			php_error(E_WARNING, "internal error: GTK_TYPE_POINTER not supported");
			break;

		default:
			g_assert_not_reached();
			break;
	}
}
#endif

#ifdef DISABLED
PHP_GTK_API zval php_gtk_get_property(zend_property_reference *property_reference)
{
	zval foo;
	return foo;
}
#endif



PHP_GTK_API zval php_gtk_get_property(zend_property_reference *property_reference)
{
	zval 			result;
	zval 			*result_ptr 	= &result;
	zval 			**prop_result;
	zend_overloaded_element *overloaded_property;
	zend_llist_element 	*element;
	zval			object 		= *property_reference->object;
	int 			getter_retval;

	for (element=property_reference->elements_list->head; element; element=element->next) {
		overloaded_property = (zend_overloaded_element *) element->data;

		getter_retval = FAILURE;
		ZVAL_NULL(&result);
		if (Z_TYPE_P(overloaded_property) == OE_IS_OBJECT) {
			char *element_name;
			printf("get Val? %s\n",Z_STRVAL(overloaded_property->element));			
			/* Trying to access a property on a non-object. */
			if (Z_TYPE(object) != IS_OBJECT ||
				Z_TYPE(overloaded_property->element) != IS_STRING) {
				return result;
			}
#ifdef DISABLED
			if ((result = invoke_getter(&object, Z_STRVAL_P(overloaded_property->element))) == FAILURE)) {
				if ((getter_retval = zend_hash_find(Z_OBJPROP(object),
									Z_STRVAL(overloaded_property->element),
									Z_STRLEN(overloaded_property->element)+1,
									(void **)&prop_result)) == SUCCESS) {
					result = **prop_result;
				}
			}
#endif
		} else if (Z_TYPE_P(overloaded_property) == OE_IS_ARRAY) {
			/* Trying to access index on a non-array. */
			if (Z_TYPE(object) != IS_ARRAY) {
				return result;
			}

			if (Z_TYPE(overloaded_property->element) == IS_STRING) {
				getter_retval = zend_hash_find(Z_ARRVAL(object),
									Z_STRVAL(overloaded_property->element),
									Z_STRLEN(overloaded_property->element)+1,
									(void **)&prop_result);
			} else if (Z_TYPE(overloaded_property->element) == IS_LONG) {
				getter_retval = zend_hash_index_find(Z_ARRVAL(object),
									Z_LVAL(overloaded_property->element),
									(void **)&prop_result);
			}
			if (getter_retval == SUCCESS)
				result = **prop_result;
		}

		zval_dtor(&overloaded_property->element);

		object = result;

		if (getter_retval == FAILURE) {
			return result;
		}
	}

	zval_add_ref(&result_ptr);
	SEPARATE_ZVAL(&result_ptr);
	return *result_ptr;
}

#ifdef DISABLED
PHP_GTK_API int php_gtk_set_property(zend_property_reference *property_reference, zval *value)
{
	zval result, temp;
	zval *temp_ptr = &temp;
	zval *new_val;
	zend_overloaded_element *overloaded_property;
	zend_llist_element *element;
	zend_llist_element *stop_element;
	zval **object = &property_reference->object;
	int setter_retval, getter_retval;
	TSRMLS_FETCH();

	/*
	 * We want to stop at the last overloaded object reference - the rest can
	 * contain array references, that's fine.
	 */
	for (stop_element=property_reference->elements_list->tail;
		 stop_element && Z_TYPE_P((zend_overloaded_element *)stop_element->data) == OE_IS_ARRAY;
		 stop_element=stop_element->prev);

	for (element=property_reference->elements_list->head; element; element=element->next) {
		overloaded_property = (zend_overloaded_element *) element->data;

		getter_retval = FAILURE;
		if (Z_TYPE_P(overloaded_property) == OE_IS_OBJECT) {
			if (Z_TYPE_PP(object) == IS_NULL ||
				(Z_TYPE_PP(object) == IS_BOOL && Z_LVAL_PP(object) == 0) ||
				(Z_TYPE_PP(object) == IS_STRING && Z_STRLEN_PP(object) == 0)) {
				object_init(*object);
			}

			/* Trying to access a property on a non-object. */
			if (Z_TYPE_PP(object) != IS_OBJECT) {
				return FAILURE;
			} 

			if (element == stop_element) {
				if ((setter_retval = invoke_setter(*object, value, &element)) == SUCCESS)
					return SUCCESS;
				else if (setter_retval == PG_ERROR)
					return FAILURE;
				else if ((getter_retval = invoke_getter(*object, &result, &element)) == SUCCESS) {
					php_error(E_WARNING, "Cannot assign to overloaded property '%s'",
							  Z_STRVAL(overloaded_property->element));
					return FAILURE;
				}
			}
			
			getter_retval = invoke_getter(*object, &result, &element);

			if (getter_retval == SUCCESS) {
				temp = result;
				object = &temp_ptr;
			} else {
				if ((getter_retval = zend_hash_find(Z_OBJPROP_PP(object),
											   Z_STRVAL(overloaded_property->element),
											   Z_STRLEN(overloaded_property->element)+1,
											   (void **)&object)) == FAILURE) {
					MAKE_STD_ZVAL(new_val);
					ZVAL_NULL(new_val);
					zend_hash_update(Z_OBJPROP_PP(object),
									 Z_STRVAL(overloaded_property->element),
									 Z_STRLEN(overloaded_property->element)+1,
									 &new_val, sizeof(void *), (void **)&object);
				}
			}
		} else if (Z_TYPE_P(overloaded_property) == OE_IS_ARRAY) {
			if (Z_TYPE_PP(object) == IS_NULL ||
				(Z_TYPE_PP(object) == IS_BOOL && Z_LVAL_PP(object) == 0) ||
				(Z_TYPE_PP(object) == IS_STRING && Z_STRLEN_PP(object) == 0)) {
				array_init(*object);
			}

			/* Trying to access index on a non-array. */
			if (Z_TYPE_PP(object) != IS_ARRAY) {
				return FAILURE;
			}

			if (Z_TYPE(overloaded_property->element) == IS_STRING) {
				getter_retval = zend_hash_find(Z_ARRVAL_PP(object),
											   Z_STRVAL(overloaded_property->element),
											   Z_STRLEN(overloaded_property->element)+1,
											   (void **)&object);
			} else if (Z_TYPE(overloaded_property->element) == IS_LONG) {
				getter_retval = zend_hash_index_find(Z_ARRVAL_PP(object),
													 Z_LVAL(overloaded_property->element),
													 (void **)&object);
			}

			if (getter_retval == FAILURE) {
				MAKE_STD_ZVAL(new_val);
				ZVAL_NULL(new_val);
				if (Z_TYPE(overloaded_property->element) == IS_STRING) {
					zend_hash_update(Z_ARRVAL_PP(object),
									  Z_STRVAL(overloaded_property->element),
									  Z_STRLEN(overloaded_property->element)+1,
									  &new_val, sizeof(void *), (void **)&object);
				} else if (Z_TYPE(overloaded_property->element) == IS_LONG) {
					zend_hash_index_update(Z_ARRVAL_PP(object),
										   Z_LVAL(overloaded_property->element),
										   &new_val, sizeof(void *), (void **)&object);
				}
			}
		}

		zval_dtor(&overloaded_property->element);
	}

	REPLACE_ZVAL_VALUE(object, value, 1);
	return SUCCESS;
}


void php_gtk_call_function(INTERNAL_FUNCTION_PARAMETERS, zend_property_reference *property_reference)
{
	zval result;
	zend_overloaded_element *overloaded_property;
	zval method_name = ((zend_overloaded_element *)property_reference->elements_list->tail->data)->element;
	zend_llist_element *element;
	zend_llist_element *stop_element;
	zval *object = property_reference->object;
	prop_getter_t *getter;
	zend_class_entry *ce;
	int found;

	/*
	 * We want to stop at the last overloaded object reference - the rest can
	 * contain array and method references, that's fine.
	 */
	for (stop_element=property_reference->elements_list->tail;
		 stop_element &&
		 (Z_TYPE_P((zend_overloaded_element *)stop_element->data) == OE_IS_ARRAY ||
		  Z_TYPE_P((zend_overloaded_element *)stop_element->data) == OE_IS_METHOD);
		 stop_element=stop_element->prev);

	for (element=property_reference->elements_list->head; element && element!=stop_element; element=element->next) {
		overloaded_property = (zend_overloaded_element *) element->data;
		if (Z_TYPE_P(overloaded_property) != OE_IS_OBJECT ||
			Z_TYPE(overloaded_property->element) != IS_STRING ||
			Z_TYPE_P(object) != IS_OBJECT) {
			php_error(E_WARNING, "Error invoking method '%s'", Z_STRVAL(method_name));
			return;
		}

		found = FAILURE;
		for (ce = Z_OBJCE_P(object); ce != NULL && found != SUCCESS; ce = ce->parent) {
			if (zend_hash_index_find(&php_gtk_prop_getters, (long)ce, (void **)&getter) == SUCCESS) {
				(*getter)(&result, object, &element, &found);
			}
		}
		if (found == FAILURE) {
			php_error(E_WARNING, "Error invoking method '%s' on property '%s'", Z_STRVAL(method_name), Z_STRVAL(overloaded_property->element));
			return;
		}
		*object = result;

		zval_dtor(&overloaded_property->element);
	}

	zval_dtor(&method_name);
}
#endif
 
PHP_GTK_API int php_gtk_set_property(zend_property_reference *property_reference, zval *value)
{
	return FAILURE;
}

PHP_GTK_API void php_gtk_register_prop_getter(zend_class_entry *ce, prop_getter_t getter)
{
	zend_hash_index_update(&php_gtk_prop_getters, (long)ce, (void*)&getter,
						   sizeof(prop_getter_t), NULL);
}

PHP_GTK_API void php_gtk_register_prop_setter(zend_class_entry *ce, prop_setter_t setter)
{
	zend_hash_index_update(&php_gtk_prop_setters, (long)ce, (void*)&setter,
						   sizeof(prop_setter_t), NULL);
}

    /**
    * Register a hardcoded callback handler.
    *
    * @param string eg .'GtkNotebook::change_page'
    * @param GClosureMarshal a marshel handler.
    *
    * @access   private
    */




PHP_GTK_API void php_gtk_register_callback(char *class_and_method, GClosureMarshal  call_function)
{
       char *lcname;

       lcname = emalloc(strlen(class_and_method)+1);

       strncpy(lcname, class_and_method, strlen(class_and_method));
       zend_str_tolower(lcname, strlen(class_and_method));
       zend_hash_update(&php_gtk_callback_hash,  lcname, strlen(class_and_method),(void**)  &call_function,
                                                  sizeof(void*), NULL);
}




    /**
    * this is the new marshal code for closures - 
    *
    * @see GClosureMarshal() in the gclosure API
    *
    * @access   private
    */
  





PHP_GTK_API void php_gtk_closure_marshal(
					GClosure 	*gobject_closure,
					GValue 		*return_value,
					guint 		n_param_values,
					const GValue 	*param_values,
					// this is the standard signal parameters..
					// the first of which is usually the object..
					gpointer 	invocation_hint,
					gpointer 	marshal_data
					)
{
	zval *gtk_args;
	php_gtk_closure  *closure = (php_gtk_closure *)gobject_closure;
	zval *wrapper = NULL;
	zval *params;
	zval *retval = NULL;
	zval ***signal_args;
	char *callback_name;
	 
	TSRMLS_FETCH();
	 
	/* Callback is always passed as the first element. */
	//zend_hash_index_find(Z_ARRVAL_P(callback_data), 0, (void **)&callback);

	/*
	 * If there is more than one element, it will be an array of:
	 *  [1] an array of extra arguments
	 *  [2] a flag indidicating whether the object should be passed to the
	 *      callback
	 *  [3] the filename where the callback was specified
	 *  [4] the line number where the callback was specified
	 */
	 
	if (!zend_is_callable(closure->callback, 0, &callback_name)) {
		if (closure->callback_filename) {
			php_error(E_WARNING, "Unable to call signal callback '%s' specified in %s on line %d", 
				callback_name, 
				closure->callback_filename, 
				closure->callback_lineno
			);
		}  else {
			php_error(E_WARNING, "Unable to call callback '%s'", callback_name);
		}
		efree(callback_name);
		return;
	}
	  
	
	gtk_args = php_gtk_args_as_hash((int) n_param_values,  param_values);
	
	 
	/* Otherwise, the only parameters will be GTK signal arguments. */
	if (!closure->pass_object && zend_hash_index_exists(HASH_OF(gtk_args), 0)) {
		 zend_hash_index_del(HASH_OF(gtk_args), 0);
	}
     	params = gtk_args;
	
	  
	/*
	 * If there are extra arguments specified by user, add them to the parameter
	 * array.
	 */
	
	if (closure->extra) {
		php_array_merge(Z_ARRVAL_P(params), Z_ARRVAL_P(closure->extra), 0 TSRMLS_CC);
	}
	 
	// convert hash into args... - util function.
	signal_args = php_gtk_hash_as_array(params);
	
	
	
	
	
	

	call_user_function_ex(EG(function_table), NULL, closure->callback, &retval, zend_hash_num_elements(Z_ARRVAL_P(params)), signal_args, 0, NULL TSRMLS_CC);

	if (retval) {
		//if (args) {
			// need to work out how to return stuff...
			// we should put it in 
			//php_gtk_ret_from_value(return_value, retval);
		//}
		zval_ptr_dtor(&retval);
	}

	efree(signal_args);
	zval_ptr_dtor(&params);
}



    /**
    * basically handler for $object->connect('signal',$callback,$otherargs....)
    *
    * See where this goes - it uses g_closure to connect a marshel (the reciever)
    * 
    * 
    * @param   INTERNAL_FUNCTION_PARAMETERS - relayed data from a PHP_FUNCTION DEF.
    * @param   int pass_object - should it send the calling object as the first argument of the callback
    * @param   int after       - is it a connect_after()
    *
    * @return   long - id of connect handle..
    * @access   private
    */
  


PHP_GTK_API void php_gtk_signal_connect_impl(INTERNAL_FUNCTION_PARAMETERS, int pass_object, int after)
{
        char *name = NULL;
        zval *callback = NULL;
        zval *extra;
        zval *data;
        char *callback_filename;
        uint callback_lineno;
        char *lookup;
        zend_class_entry *ce;
        //GtkSignalFunc *callback_cfunction;
        int lookup_len;
	GClosure *closure;
	GQuark detail = 0;
	guint   sigid, len;
	
	GClosureMarshal *callback_cfunction;

        NOT_STATIC_METHOD();
 
    
 
        if (ZEND_NUM_ARGS() < 2) {
                php_error(E_WARNING, "%s() requires at least 2 arguments, %d given",
                                  get_active_function_name(TSRMLS_C), ZEND_NUM_ARGS());
                return;
        }
	
        if (!php_gtk_parse_args(2, "sV", &name, &callback)) {
			return;
	}
	
	
	if (!g_signal_parse_name(name, G_OBJECT_TYPE(PHP_GTK_GET(this_ptr)),
                 			     &sigid, &detail, TRUE)) {
		php_error(E_WARNING, "%s() Unknown signal name '%s'",
			get_active_function_name(TSRMLS_C), name);
			return; 		     
	}
	closure = g_closure_new_simple(sizeof(php_gtk_closure), NULL);	
	zval_add_ref(&callback);
	
	//printf("callback set: %x\n",callback);
	
	((php_gtk_closure *)closure)->callback 			= callback;
	
	((php_gtk_closure *)closure)->callback_filename 	= zend_get_executed_filename(TSRMLS_C);
        ((php_gtk_closure *)closure)->callback_lineno 		= zend_get_executed_lineno(TSRMLS_C);
        ((php_gtk_closure *)closure)->extra 			= php_gtk_func_args_as_hash(ZEND_NUM_ARGS(), 2, ZEND_NUM_ARGS());
	((php_gtk_closure *)closure)->pass_object		= pass_object;
	((php_gtk_closure *)closure)->object			= pass_object ? G_OBJECT(PHP_GTK_GET(this_ptr)) : NULL;
	
	// this check to see if we have registered a manual handler...
	ce = Z_OBJCE_P(this_ptr);
        lookup_len = ce->name_length + strlen(name) + 2;
        lookup = emalloc(lookup_len + 1);
        sprintf(lookup, "%s::%s", ce->name, name);
                                                                                                                                                                                  
        lookup[lookup_len] = '\0';
        zend_str_tolower(lookup,lookup_len);
 
        /* now see if it's a manually handled object::callback.. */
        if (zend_hash_find(&php_gtk_callback_hash, lookup, lookup_len , (void **) &callback_cfunction) == SUCCESS) {
		g_closure_set_marshal(closure, (GClosureMarshal)  *callback_cfunction);
		RETURN_LONG(g_signal_connect_closure_by_id
			(G_OBJECT(PHP_GTK_GET(this_ptr)), 
			sigid, 
			detail,
			closure, 
			(gboolean) after
		));
	  
      
        }
 
	g_closure_set_marshal(closure, php_gtk_closure_marshal);
	
	RETURN_LONG(g_signal_connect_closure_by_id
		(G_OBJECT(PHP_GTK_GET(this_ptr)), 
		sigid, 
		detail,
		closure, 
		(gboolean) after
	));
	
 

}
 







#endif  /* HAVE_PHP_GTK */

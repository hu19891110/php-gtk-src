/*
 * PHP-GTK - The PHP language bindings for GTK+
 *
 * Copyright (C) 2001-2004 Andrei Zmievski <andrei@php.net>
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

#include "php_gtk.h"

#if HAVE_PHP_GTK

/*
 * Closures API and helper functions
 */

struct _phpg_closure_t {
	GClosure closure;
	zval *callback;
	zval *user_args;
	zend_bool use_signal_object;
	char *src_filename;
	uint src_lineno;
};

static void phpg_closure_invalidate(gpointer data, GClosure *closure)
{
    phpg_closure_t *phpg_closure = (phpg_closure_t *)closure;

    zval_ptr_dtor(&phpg_closure->callback);
    if (phpg_closure->user_args) {
        zval_ptr_dtor(&phpg_closure->user_args);
    }
    efree(phpg_closure->src_filename);

    phpg_closure->callback = NULL;
    phpg_closure->user_args = NULL;
    phpg_closure->use_signal_object = 0;
    phpg_closure->src_filename = NULL;
    phpg_closure->src_lineno = 0;
}

static void phpg_closure_marshal(GClosure *closure,
                                 GValue *return_value,
                                 guint n_param_values,
                                 const GValue *param_values,
                                 gpointer invocation_hint,
                                 gpointer marshal_data)
{
    phpg_closure_t *phpg_closure = (phpg_closure_t *)closure;
	char *callback_name;
    zval ***params = NULL;
    zval *retval = NULL;
    uint n_params = 0, i;
	TSRMLS_FETCH();

	if (!zend_is_callable(phpg_closure->callback, 0, &callback_name)) {
		if (phpg_closure->src_filename)
			php_error(E_WARNING, "Unable to invoke signal callback '%s' specified in %s on line %d", callback_name,
                      phpg_closure->src_filename, phpg_closure->src_lineno);
		else
			php_error(E_WARNING, "Unable to invoke signal callback '%s'", callback_name);
		efree(callback_name);
		return;
	}

	if (!phpg_closure->use_signal_object) {
        /* skip first parameter */
        n_param_values--;
        param_values++;
    }
    n_params = n_param_values;
    
    if (phpg_closure->user_args) {
        n_params += zend_hash_num_elements(Z_ARRVAL_P(phpg_closure->user_args));
    }

	params = (zval ***)emalloc(n_params * sizeof(zval **));
    for (i = 0; i < n_param_values; i++) {
        params[i] = (zval **) emalloc(sizeof(zval *));
        *(params[i]) = NULL;
        if (phpg_gvalue_to_zval(&param_values[i], params[i], FALSE TSRMLS_CC) != SUCCESS) {
            goto err_marshal;
		}
    }

    if (phpg_closure->user_args) {
        for (zend_hash_internal_pointer_reset(Z_ARRVAL_P(phpg_closure->user_args));
             zend_hash_get_current_data(Z_ARRVAL_P(phpg_closure->user_args), (void **)&params[i]) == SUCCESS;
             zend_hash_move_forward(Z_ARRVAL_P(phpg_closure->user_args)), i++);
        /* empty body */
    }
    assert(i == n_params);

    call_user_function_ex(EG(function_table), NULL, phpg_closure->callback,
                          &retval, n_params, params, 0, NULL TSRMLS_CC);

	if (retval) {
		if (return_value) {
			if (phpg_gvalue_from_zval(return_value, retval) == FAILURE) {
                php_error(E_WARNING, "Could not convert return value of signal callback '%s' to '%s'",
                          callback_name, g_type_name(G_VALUE_TYPE(return_value)));
            }
        }
		zval_ptr_dtor(&retval);
	}

err_marshal:
    efree(callback_name);
    for (i = 0; i < n_param_values; i++) {
        zval_ptr_dtor(params[i]);
        efree(params[i]);
    }
    efree(params);
}

PHP_GTK_API GClosure* phpg_closure_new(zval *callback, zval *user_args, zend_bool use_signal_object TSRMLS_DC)
{
    GClosure *closure;
    phpg_closure_t *phpg_closure;

    g_return_val_if_fail(callback != NULL, NULL);
    closure = g_closure_new_simple(sizeof(phpg_closure_t), NULL);
    g_closure_add_invalidate_notifier(closure, NULL, phpg_closure_invalidate);
    g_closure_set_marshal(closure, phpg_closure_marshal);

    phpg_closure = (phpg_closure_t *) closure;
    zval_add_ref(&callback);
    phpg_closure->callback = callback;
	phpg_closure->src_filename = estrdup(zend_get_executed_filename(TSRMLS_C));
	phpg_closure->src_lineno = zend_get_executed_lineno(TSRMLS_C);

    if (user_args) {
        zval_add_ref(&user_args);
        if (Z_TYPE_P(user_args) != IS_ARRAY) {
            convert_to_array(user_args);
        }
        phpg_closure->user_args = user_args;
    } else {
        phpg_closure->user_args = NULL;
    }

    phpg_closure->use_signal_object = use_signal_object;

    return closure;
}

#endif /* HAVE_PHP_GTK */

/* vim: set fdm=marker et sts=4: */

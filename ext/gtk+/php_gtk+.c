/*
 * PHP-GTK - The PHP language bindings for GTK+
 *
 * Copyright (C) 2001 Andrei Zmievski <andrei@php.net>
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
 
/* $Id$: */

#include "php_gtk.h"
#include "SAPI.h"

#if HAVE_PHP_GTK

#include "php_gtk+.h"

int le_gtk_object;

static void init_gtk(void)
{
	HashTable *symbol_table;
	zval **z_argv = NULL, **z_argc = NULL, **entry;
	zval *tmp;
	char **argv;
	int argc, i;
	zend_bool no_argc = 0;
	PLS_FETCH();
	SLS_FETCH();

	/* We check request_method to see if we've been called from command line or
	   Web server. Running GUI apps through a Web module can be dangerous to
	   your health. */
	if (SG(request_info).request_method != NULL) {
		php_error(E_ERROR, "php-gtk: PHP GTK+ support is not available under Web servers");
		return;
	}
 
	/*
	 * Since track_vars is always on, we just get the argc/argv values from
	 * there.
	 */
	symbol_table = PG(http_globals)[TRACK_VARS_SERVER]->value.ht;

	zend_hash_find(symbol_table, "argc", sizeof("argc"), (void **)&z_argc);
	zend_hash_find(symbol_table, "argv", sizeof("argv"), (void **)&z_argv);
	if (!z_argc || !z_argv || Z_TYPE_PP(z_argc) != IS_LONG || Z_TYPE_PP(z_argv) != IS_ARRAY) {
		php_error(E_ERROR, "php-gtk: argc/argv are corrupted");
	}

	argc = Z_LVAL_PP(z_argc);

	/*
	 * If the script was called via -f switch and no further arguments were
	 * given, argc will be 0 and that's not good for gtk_init_check(). We use
	 * the path to the script as the only argument, and remember that we won't
	 * have to update symbol table after gtk_init_check().
	 */
	if (argc == 0) {
		argc = 1;
		no_argc = 1;
		argv = (char **)g_new(char *, argc);
		argv[0] = g_strdup(SG(request_info).path_translated);
	} else {
		argv = (char **)g_new(char *, argc);
		i = 0;
		for (zend_hash_internal_pointer_reset(Z_ARRVAL_PP(z_argv));
			 zend_hash_get_current_data(Z_ARRVAL_PP(z_argv), (void **)&entry) == SUCCESS;
			 zend_hash_move_forward(Z_ARRVAL_PP(z_argv))) {
			argv[i++] = g_strndup(Z_STRVAL_PP(entry), Z_STRLEN_PP(entry));
		}
	}
	
	/* Ok, this is a hack. GTK+/GDK calls g_atexit() to set up a couple of cleanup
	   functions that should be called when the main program exits. However, if
	   we're compiled as .so library, libgtk.so will be unloaded first and the
	   pointers to the cleanup functions will be invalid. So we load libgtk.so
	   one more time to make sure it stays in memory even after our .so is
	   unloaded. */
	//DL_LOAD("libgtk.so");
			   
	if (!gtk_init_check(&argc, &argv)) {
		if (argv != NULL) {
			for (i = 0; i < argc; i++)
				g_free(argv[i]);
			g_free(argv);
		}
		php_error(E_ERROR, "php-gtk: Could not open display");
		return;
	}

	/*
	   We must always call gtk_set_locale() in order to get GTK+/GDK
	   correctly initialize multilingual support. Otherwise, application
	   will refuse any letters outside ASCII and font metrics will
	   be broken.
	 */
	gtk_set_locale();

	if (no_argc) {
		/* The -f switch case, simple. */
		g_free(argv[0]);
	} else {
		/* We always clean the argv array. */
		zend_hash_clean(Z_ARRVAL_PP(z_argv));

		/* Then if there are any arguments left after processing with
		   gtk_init_check(), we put them back into PHP's argv array and update
		   argc as well. */
		if (argv != NULL) {
			for (i = 0; i < argc; i++) {
				ALLOC_ZVAL(tmp);
				tmp->type = IS_STRING;
				tmp->value.str.len = strlen(argv[i]);
				tmp->value.str.val = estrndup(argv[i], tmp->value.str.len);
				INIT_PZVAL(tmp);
				zend_hash_next_index_insert(Z_ARRVAL_PP(z_argv), &tmp, sizeof(zval *), NULL);
			}
			g_free(argv);

			Z_LVAL_PP(z_argc) = argc;
		}
	}
}

static void release_gtk_object_rsrc(zend_rsrc_list_entry *rsrc)
{
	GtkObject *obj = (GtkObject *)rsrc->ptr;
	gtk_object_unref(obj);
}

PHP_GTK_XINIT_FUNCTION(gtk_plus)
{
	le_gtk_object = zend_register_list_destructors_ex(release_gtk_object_rsrc, NULL, "GtkObject", module_number);

	init_gtk();
	php_gtk_register_constants();
	php_gdk_register_constants();
	php_gtk_register_classes();
	php_gdk_register_classes();
	php_gtk_plus_register_types(module_number);
}

PHP_GTK_XSHUTDOWN_FUNCTION(gtk_plus)
{
	gtk_exit(0);
}

php_gtk_ext_entry gtk_plus_ext_entry = {
	"gtk+",
	PHP_GTK_XINIT(gtk_plus),
	PHP_GTK_XSHUTDOWN(gtk_plus)
};

#endif	/* HAVE_PHP_GTK */
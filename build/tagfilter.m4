AC_DEFUN([GP_CHECK_TAGFILTER],
[
    GP_ARG_DISABLE([TagFilter], [auto])

    GP_CHECK_PLUGIN_DEPS([TagFilter], [TAGFILTER],
                         [$GP_GTK_PACKAGE >= 2.16
                          glib-2.0 >= 2.4])

    GP_COMMIT_PLUGIN_STATUS([TagFilter])

    AC_CONFIG_FILES([
        tagfilter/Makefile
        tagfilter/src/Makefile
    ])
])

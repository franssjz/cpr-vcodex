__license__ = "GPL v3"
__copyright__ = "2025, Hugo Batista <intellireading at hugobatista.com>"
# pylint: disable=import-error

# The class that all Interface Action plugin wrappers must inherit from
from calibre.customize import (
    InterfaceActionBase,
)


class InterfacePluginWrapper(InterfaceActionBase):
    def config_widget(self):
        """
        Implement this method and return a QWidget for configuring this plugin.
        The widget can have an optional validate() method that takes no arguments
        and is called immediately after the user clicks OK. Changes are applied
        if and only if the method returns True.
        """
        from calibre_plugins.metaguideinterface.config_ui import ConfigWidget

        return ConfigWidget(None)

    name = "EBook Metaguider (intellireading)"
    description = (
        "Adds a button to toolbar and context menu, to convert epub and kepub files "
        "to a metaguided format, improving your focus and reading speed "
        "(sometimes called bionic reading)."
    )
    supported_platforms = ["windows", "osx", "linux"]
    author = "Hugo Batista"
    version = (2, 0, 1)
    minimum_calibre_version = (8, 4, 0)

    #: This field defines the GUI plugin class that contains all the code
    #: that actually does something. Its format is module_path:class_name
    #: The specified class must be defined in the specified module.
    actual_plugin = "calibre_plugins.metaguideinterface.action:InterfacePlugin"

    def is_customizable(self):
        return True

    def customization_help(self, gui=False):
        return (
            "This plugin can be customized to change the default action when clicking the toolbar button. "
            "You can choose whether clicking the button should create a metaguided epub or kepub file."
        )

    def save_settings(self, config_widget):
        config_widget.save_settings()
        # Apply the changes
        ac = self.actual_plugin_
        if ac is not None:
            ac.apply_settings()
